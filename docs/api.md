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

---
## Constants 

* **`AF`** — address family: `UNSPEC=0`, `UNIX=1`, `INET=2`, `INET6=10` 
* **`SOCK_TYPE`** — `STREAM=1`, `DGRAM=2`, `RAW=3`
* **`SOCK_FLAGS`** — flags for `net_open`: `CLOEXEC`, `NONBLOCK`
* **`INADDR`** — for `net_listen` bind: `ANY=0`, `BROADCAST=-1`
* **`MSG_FLAG`** — for `net_send`/`net_recv`: `PEEK`, `WAITALL`, `DONTWAIT`, `NOSIGNAL` (pass `0` for no flags)
* **`SHUT`** — for `net_shutdown`: `RD`, `WR`, `RDWR`
* **`POLL`** — for `net_poll` events: `IN` (`POLLIN`), `OUT` (`POLLOUT`), `ERR`, `HUP`, `NVAL`
* **`ERR`** — `AGAIN` (`EAGAIN), `WOULDBLOCK`, `TIMEDOUT` (`ETIMEDOUT`), `BADURL` ( malformed url)
* **`SOMAXCONN`** — `4096`
* **`CR_LF`** — `"'0D0A'x"`, `MAX_HOST_LEN=256`, `MAX_IP_LEN=46`

`%replace` types: `sock_fd_t`/`size_t`/`port_t` `fixed bin(31)`, `backlog_t` `fixed bin(15)`, `flags_t`/`shut_how_t` `fixed bin(31)`.

---
## Client

### Opening & connecting
* **`net_open(conn, family, socktype, flag)`** (`source/net.pli:27`) — `c_socket` (`include/c_bridge.inc:5` → `socket`), zeroes `read_timeout`/`write_timeout`. `flag` is `SOCK_FLAGS` or `0`. Check `oncode()` if it signals `neterror`.
* **`net_connect(conn, ip, port)`** (`source/net.pli:46`) — `ip` can be `'93.184.216.34'`, `port` is `80`. Honors `write_timeout` via `apply_timeout` → `c_set_timeout`. Updates `conn.host_name`/`port`/`is_connected`.
* **`net_dial(conn, url, family)`** (`source/net.pli:74`) — `url` like `'example.com:80'` or `'127.0.0.1:18080'`. Splits on `:`, auto-resolves via `net_resolve` if host contains letters, then `net_connect`. Signals `oncode(ERR.BADURL)` if no `:`.
* **`net_resolve(conn, hostname)`** (`source/net.pli:310`) — `c_resolve_hostname` (`include/c_bridge.inc:12` → `resolve_hostname` `source/c_bridge.c:43`) with `conn.addr_family`, fills `conn.ip_addr`/`host_name`. Use when you want `AF` control.

### Writing
* **`net_write(conn, buffer)`** (`source/net.pli:200`) — single `c_write` (`include/c_bridge.inc:72`). Respects `write_timeout`.
* **`net_write_all(conn, buffer)`** (`source/net.pli:166`) — loops `c_write` until `length(buffer)` sent. Use for HTTP requests.
* **`net_send(conn, request, flags)`** (`source/net.pli:117`) — `c_send` (`include/c_bridge.inc:56` → `send`) with `MSG_FLAG`. Respects `write_timeout`. `flags=0` means no flags (`tests/send_recv.pli:21`).

### Reading
* **`net_read(conn, buffer)`** (`source/net.pli:222`) — single `c_read` (`include/c_bridge.inc:72`) up to `length(buffer)`. Returns `0` on `EOF` (not `neterror`), `>0` bytes actually read and `buffer` is truncated to that length. Respects `read_timeout`. Use for chunking (`examples/read_unknown.pli:38`).
* **`net_read_all(conn, p)`** (`source/net.pli:248`) — modern-dynamic read until EOF, hides loop+`allocate`. `p` is `pointer` to `char(32767) varying based(p)` heap (`examples/read_unknown.pli:21` pattern, now inside). Caller `allocate` is done inside, caller `free` after (`free response;`). Returns `length(response)` (`0`=EOF), `>0` bytes, signals `neterror` `ERR.AGAIN`/`ERR.TIMEDOUT`/`ERR.TOOLARGE` (`32767` cap) `include/type_defs.inc:50`. Respects `read_timeout` once before loop. `net_read` stays for `poll`/`NONBLOCK` chunking.
* **`net_recv(conn, response, flags)`** (`source/net.pli:142`) — `c_receive` (`include/c_bridge.inc:64` → `recv`) with `MSG_FLAG`. Respects `read_timeout`, truncates `response` on success (`tests/send_recv.pli:28`).

### Closing
* **`net_close(conn)`** (`source/net.pli:279`) — `c_close` (`include/c_bridge.inc:86`) if `fd >=0`, clears `is_connected`. Returns `0` ok, `<0` error (also signals `neterror`).
* **`net_shutdown(conn, how)`** (`source/net.pli:296`) — `c_shutdown` (`include/c_bridge.inc:23` → `shutdown`). `how` is `SHUT.RD` (`0`) / `SHUT.WR` (`1`) / `SHUT.RDWR` (`2`).

### Polling
* **`net_poll(conn, timeout, events)`** (`source/net.pli:323`) — `c_poll` (`include/c_bridge.inc:106` → `c_poll` `source/c_bridge.c:114`, `poll(2)` with `EINTR` retry). `timeout` is ms (`-1` block forever, `0` return immediately), `events` is `POLL.IN`/`OUT` mask. Returns `0` on timeout, `>0` `revents` mask (`POLL.IN` etc), `<0` on error (also signals `neterror` with `oncode()` = `errno`). Use to wait for `POLL.IN` before `net_read`/`net_accept` instead of blocking or `read_timeout`. Example: `if net_poll(client, 500, POLL.IN) = 0 then /* timeout */; else bytes = net_read(client, buf);`.

### Non-blocking
* **`net_set_nonblocking(conn)`** (`source/net.pli:353`) — `c_set_nonblocking(...,1)` (`include/c_bridge.inc:119` → `c_set_nonblocking` `source/c_bridge.c:125`, `fcntl O_NONBLOCK`), signals `neterror` on error, sets `conn.flags` `SOCK_FLAGS.NONBLOCK`. Blocking is default (`net_open` creates blocking socket).
* **`net_set_default(conn)`** (`source/net.pli:366`) — `c_set_nonblocking(...,0)` restores blocking, clears `SOCK_FLAGS.NONBLOCK`, signals `neterror` on error. Pair with `net_set_nonblocking` for explicit toggling: `call net_set_nonblocking(conn); if net_poll(conn, 0, POLL.IN) >0 then bytes = net_read(conn, buf); call net_set_default(conn);` — `net_read`/`net_write`/`net_recv`/`net_send` that would block signal `neterror` `ERR.AGAIN` (`ERR.AGAIN`) same as timeout, so poll first or handle `oncode 11`. 
Per-call alternative: `net_recv(conn, buf, MSG_FLAG.DONTWAIT)` / `net_send(..., MSG_FLAG.DONTWAIT)` without changing mode.

### Errors
Single condition `neterror` (`include/net_errors.inc:1`). Every failing call signals with `oncode()` as the error number. Example:

```pli
 on condition(neterror) begin;
   display('Networking error, code =' || oncode());
   goto done;
 end;
```

`oncode(ERR.BADURL)` is the only lib-specific code (malformed `url` in `net_dial`).

---
## Server

* **`net_listen(conn, port, backlog)`** (`source/net_server.pli:25`) — `c_bind` (`include/c_bridge.inc:40` → `bind_to_port` `source/c_bridge.c:30`) with `INADDR.ANY` and `c_listen` (`include/c_bridge.inc:34`). Sets `conn.port`/`is_connected`. If `port=0`, requests an ephemeral port — kernel-assigned port is written back to `conn.port` via `c_getsockname` (`include/c_bridge.inc:98` → `c_getsockname` `source/c_bridge.c:104`). See `examples/ephemeral.pli:25` and `tests/ephemeral.pli:19`.
* **`net_accept(server, client)`** (`source/net_server.pli:57`) — `c_accept` (`include/c_bridge.inc:29` → `default_accept` `source/c_bridge.c:14`, EINTR retry), respects `server.read_timeout` via `apply_timeout`, inits `client` with `fd`/`is_connected` and zeroes its timeouts, then fills `client.ip_addr`/`port`/`host_name`/`addr_family`/`sock_type` via `c_getpeername` (`include/c_bridge.inc:106` → `c_getpeername` `source/c_bridge.c:114`). See `tests/ephemeral.pli:1`.

Typical loop:

```pli
 call net_open(server, AF.INET, SOCK_TYPE.STREAM, 0);
 call net_listen(server, 18080, 5);
 /* or ephemeral: call net_listen(server, 0, 5); display(server.port); */
 call net_accept(server, client);
 /* client.ip_addr / client.port now filled via getpeername */
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

Applied automatically in `net_read`/`net_read_all`/`net_recv` (`read_timeout`), `net_write`/`net_write_all`/`net_send`/`net_connect` (`write_timeout`), and `net_accept` (`server.read_timeout`). `c_set_timeout` (`source/c_bridge.c:89`) maps ms → `setsockopt(SO_RCVTIMEO/SO_SNDTIMEO)`. See `tests/timeout.pli:28` — timeout surfaces as `neterror` with `oncode 11` (`ERR.AGAIN`) or `110` (`ETIMEDOUT`) on Linux. For explicit readiness waiting without changing socket timeout, use `net_poll` `source/net.pli:323` (`poll` timeout `-1`/`0`/`>0` independent of `conn.read_timeout`). Non-blocking `net_set_nonblocking`/`net_set_default` `source/net.pli:353` bypasses `SO_RCVTIMEO` — would-block also surfaces as `11`.

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
   dcl p pointer init(null);
   dcl resp char(32767) varying based(p);
   dcl n size_t;
   on condition(neterror) begin;
     display('neterror ' || oncode()); /* ERR.TOOLARGE 75 if >32767 */
     if p ¬= null then free resp;
     stop;
   end;
   call net_dial(conn, 'example.com:80', AF.INET);
   call net_write_all(conn, req);
   n = net_read_all(conn, p); /* allocates heap, loops until EOF */
   display(resp); /* varying already length=n, no substr */
   free resp;
   call net_close(conn);
 end main;
```

With flags, poll and non-blocking:

```pli
 bytes = net_send(conn, request, 0);          /* or MSG_FLAG.NOSIGNAL */
 bytes = net_recv(conn, response, MSG_FLAG.PEEK);
 call net_shutdown(conn, SHUT.RDWR);
 if net_poll(conn, 1000, POLL.IN) > 0 then bytes = net_read(conn, buf); /* 1s poll */
 call net_set_nonblocking(conn);              /* O_NONBLOCK via fcntl */
 if net_poll(conn, 0, POLL.IN) > 0 then bytes = net_read(conn, buf); /* non-blocking poll+read */
```

More in `examples/readme_usage` (`net_dial` + `net_write_all`/`net_read_all`), `examples/use_socket` (`net_open`/`net_connect` + `net_write`/`net_read`), `examples/ephemeral` (`port 0` + peer via `getpeername`), `examples/client_server` pair, `tests/timeout` (direct `read_timeout`), `tests/send_recv` (`net_send`/`net_recv` `flags=0`), `tests/ephemeral` (`c_getsockname`/`c_getpeername`), `tests/poll` (`net_poll` `POLL.IN` timeout vs readable), `tests/nonblocking` (`net_set_nonblocking` + `POLL.IN`/`MSG_FLAG.DONTWAIT`).
