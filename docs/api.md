# libnet API — Human-friendly Reference

`libnet` is a tiny PL/I networking library. You work with a single struct — `conncb` — and a handful of verbs.

```pli
 dcl conn like conncb;
 call net_open(conn, AF.INET, SOCK_TYPE.STREAM, 0);
 call net_connect(conn, '93.184.216.34', 80);
```

All names are lowercase. Errors signal `neterror` — handle once with `on condition(neterror)` and `oncode()`.

---
## The connection: `conncb`

Always declare as `dcl conn like conncb;`. It's `aligned based`.

* `fd` — socket descriptor, `-1` empty
* `ip_addr` `char(46)` — last resolved or connected IP (big enough for IPv6)
* `host_name` `char(256)` — last hostname or IP you connected to
* `port` — last port
* `is_connected` `bit(1)` — `'1'b` after connect/accept
* `addr_family` / `sock_type` / `flags` / `ipv4_addr` — as passed to `net_open`
* `read_timeout` / `write_timeout` — `0` means block forever, otherwise milliseconds. Set directly (`conn.read_timeout = 500;`) — honored on the next read/write/connect/accept.

Constants: `AF.INET` (`2`), `AF.INET6` (`10`), `SOCK_TYPE.STREAM` (`1`), `INADDR.ANY` (`0`).

---
## Client

### Opening & connecting
* **`net_open(conn, family, socktype, flag)`** — creates a socket and zeroes timeouts. Check `oncode()` if it signals `neterror`.
* **`net_connect(conn, ip, port)`** — `ip` can be `'93.184.216.34'`, `port` is `80`. Honors `write_timeout` for the connect itself.
* **`net_dial(conn, url, family)`** — `url` like `'example.com:80'` or `'127.0.0.1:18080'`. Splits on `:`, auto-resolves if host looks like a name, then connects. Signals `oncode 61` if no `:`.
* **`net_resolve(conn, hostname)`** — resolves `hostname` and fills `conn.ip_addr`/`host_name`. Use when you want `AF` control.

### Writing
* **`net_write(conn, buffer)`** — sends one chunk. Respects `write_timeout`.
* **`net_write_all(conn, buffer)`** — loops until `length(buffer)` sent. Use for HTTP requests.
* **`net_send(conn, request, flags)`** — like `net_write` but with `flags`.

### Reading
* **`net_read(conn, buffer)`** — reads up to `length(buffer)`. Returns `0` on `EOF` (not `neterror`), `>0` bytes actually read and `buffer` is truncated to that length.
* **`net_read_all(conn, buffer)`** — loops until `length(buffer)` filled or `EOF`. Use when you know the size.
* **`net_recv(conn, response, flags)`** — like `net_read` but with `flags`.

### Closing
* **`net_close(conn)`** — closes `fd` if `>=0`, clears `is_connected`. Returns `0` ok, `<0` error (also signals).
* **`net_shutdown(conn, how)`** — `how`: `0` read, `1` write, `2` both.

### Errors
Single condition `neterror`. Every failing call signals with `oncode()` as the error number. Example:

```pli
 on condition(neterror) begin;
   display('Networking error, code =' || oncode());
   goto done;
 end;
```

`oncode 61` is the only lib-specific code (malformed `url` in `net_dial`).

---
## Server

* **`net_listen(conn, port, backlog)`** — binds `INADDR.ANY` and listens. Sets `conn.port`/`is_connected`.
* **`net_accept(server, client)`** — accepts a client, respects `server.read_timeout` for accept timeout, inits `client` with `fd`/`is_connected` and zeroes its timeouts.

Typical loop:

```pli
 call net_open(server, AF.INET, SOCK_TYPE.STREAM, 0);
 call net_listen(server, 18080, 5);
 call net_accept(server, client);
 bytes = net_read(client, buffer);
```

---
## Timeouts — direct `conncb` fields

No separate setter — just assign:

```pli
 call net_open(conn, AF.INET, SOCK_TYPE.STREAM, 0);
 conn.read_timeout = 500;   /* ms, 0 = block forever */
 conn.write_timeout = 500;
 call net_connect(conn, '127.0.0.1', 18080); /* honors write_timeout */
 bytes = net_read(conn, buf); /* honors read_timeout -> neterror */
```

Applied automatically in every `net_read`/`net_write`/`net_accept`/`net_connect`. See `tests/timeout`.

---
## Diagnostics

* Build with `plic -C -dELF -O -i include`, check `*.lst` with `grep -E '\(ERR|WRN\)'` — `rc 0` ok, `4` warning, `8` error. `Makefile` tolerates `4`.
* Use `display('...' || oncode())` in `on neterror` to see the error number.

---
## Minimal complete client

```pli
 main: proc options(main);
 %include net;
   dcl conn like conncb;
   dcl req char(256) varying init('GET / HTTP/1.1' || CR_LF || 'Host: example.com' || CR_LF || CR_LF);
   dcl resp char(4096);
   dcl n size_t;
   on condition(neterror) begin; display('neterror ' || oncode()); stop; end;
   call net_dial(conn, 'example.com:80', AF.INET);
   call net_write_all(conn, req);
   n = net_read_all(conn, resp);
   display(substr(resp,1,n));
   call net_close(conn);
 end main;
```

More in `examples/readme_usage` (`net_dial` + `net_write_all`/`net_read_all`), `examples/use_socket` (`net_open`/`net_connect`), `examples/client_server` pair, `tests/timeout` (direct `read_timeout`).
