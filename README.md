# libnet — PL/I-centric socket library (thin C, PL/I does the work)

A connection-oriented socket library whose design inverts the usual split:

- **minimal C** (`source/c_bridge.c`): only the raw syscalls PL/I cannot
  perform — `socket/bind/listen/accept/connect/send/recv/close/shutdown/
  settimeout/setnonblock/poll/getpeername` plus an `errno` cache. Reached from
  PL/I through pli-llvm's by-value C FFI.
- **most processing in PL/I** (`include/net_base.inc`, `include/net_server.inc`):
  the connection pool, receive buffering, delimiter scanning, retry loops,
  timeout and error policy — all in PL/I, using `CONTROLLED` storage and
  pointer handles.

Callers hold only a `POINTER` handle; the connection structure layout is
private to the library. This is the classic PL/I "fat runtime / task context"
pattern — well-suited to AI-authored PL/I, where a handful of thin PL/I calls
replace a page of socket bookkeeping.

## Style

The PL/I follows classic PL/I conventions drawn from the reference corpus
(`references/text/PL:I Programming Style.txt`, Iron Spring samples):

- `CONTROLLED` connection records declared `ALIGNED` for efficient member
  access;
- a module header (name / author / purpose / calling sequence) atop every
  include;
- declarations grouped by kind (parameters, automatic, builtins) and
  commented by section, with `%page`-style block separators;
- errno exposed as `%replace` named constants (`errno.inc`, after Iron
  Spring's `lib/include/errno.inc`) rather than magic numbers;
- ON-units kept to a single action and re-arming the condition (`ON ... SYSTEM;`)
  inside the unit so a failure cannot recurse;
- `SELECT` over laddered `IF` for multi-way dispatch.

## Naming

Names follow classic PL/I convention: short meaningful module prefixes (the
Iron Spring runtime `_pli_`, MULTICS networking `net_`), lowercase verbs for
internal procedures, and descriptive constants.

| Layer | Prefix / form | Example |
|---|---|---|
| C bridge (system primitives) | `netc_` | `netc_socket`, `netc_recv`, `netc_sockopt` |
| Public PL/I procedures | `net_` | `net_open`, `net_connect`, `net_read_until` |
| Option codes | `NETOPT_*` | `NETOPT_REUSEADDR`, `NETOPT_KEEPALIVE`, `NETOPT_NODELAY` |
| Status codes | `NET_*` | `NET_OK`, `NET_ERR`, `NET_EOF`, `NET_TIMEOUT` |
| errno constants | `E*` | `EAGAIN`, `EBADF`, `ECONNREFUSED` (in `errno.inc`) |
| POSIX mirror | as-is | `AF_INET`, `SOCK_STREAM`, `SHUT_WR`, `POLLIN` |
| Conditions | lowercase | `neterror`, `nettimeout` |
| Internal pointers | role-based | `pconn` (connection handle), `psrv` (server handle) |
| Internal helpers | lowercase verbs | `raise_err`, `raise_timeout` |

## Layout

| Path | Contents |
|---|---|
| `source/c_bridge.c` | the ONLY C — thin syscall wrappers (`netc_*`) |
| `include/type_defs.inc` | constants (`AF_*`, `SOCK_*`, `NET_*`), sizes |
| `include/errno.inc` | POSIX errno as `%replace` named constants |
| `include/c_bridge.inc` | by-value FFI declarations for the C bridge |
| `include/net_errors.inc` | `condition neterror` / `condition nettimeout`, `net_errtext` |
| `include/net_base.inc` | client API (handle-based, multi-entry read/send) |
| `include/net_server.inc` | server API (`net_listen` / `net_accept`) |
| `include/net.inc` | master include — `%include net;` gets everything |
| `docs/api.md` | structured reference of every function (signature, returns, raises) |
| `tests/c_bridge.c` | C regression test for the bridge bindings |
| `examples/echo_server.pli` | echo server + client demo |
| `examples/client.pli` | minimal TCP client |
| `examples/resolve.pli` | DNS resolution demo |

## Build

```bash
make            # builds libnet.a (the C bridge) + dist/net.inc
make test       # builds + runs the C bridge regression test (works today)
make example    # tries the demo; shows the wishlist gaps today
```

`make all` succeeds now. Compiling a *program* (which pulls in the PL/I library
via `%include net;`) requires the pli-llvm features listed in
[`WISHLIST.md`](WISHLIST.md). Once those land, build a program with:

```bash
make build-prog SRC=examples/echo_server.pli OUT=echo_server
```

## API sketch

```pli
%include net;
dcl conn pointer;

conn = net_open(AF_INET, SOCK_STREAM, 0);        /* library allocates (CONTROLLED) */
rc   = net_connect(conn, '127.0.0.1', 8090);
rc   = net_setopt(conn, NETOPT_KEEPALIVE, 1);    /* socket options */
bytes = net_write(conn, 'hello');
bytes = net_read_until(conn, buf, '0A'x);        /* read a line */
call net_close(conn);                            /* frees (CONTROLLED pop) */

/* errors/timeouts via ON conditions */
on condition(neterror) begin; ... end;
on condition(nettimeout) begin; ... end;
```

## Production readiness

The C bridge and PL/I layer cover the core of what a production socket client
or single-client server needs:

- **socket options** — `net_setopt` (`NETOPT_REUSEADDR` / `KEEPALIVE` /
  `NODELAY`) and `net_set_linger`; platform values live in C, not PL/I;
- **addresses** — `net_peer` (remote) and `net_local` (bound local addr/port);
- **DNS** — `net_resolve` (host ↔ dotted quad), plus connect-time resolution;
- **nonblocking connect** — `net_connect_nb` + `net_connect_finish`
  (`EINPROGRESS` → poll writable → check `SO_ERROR`);
- **UDP** — `net_sendto` / `net_recvfrom` datagram send/receive;
- **errors** — `net_strerror` (full errno text) and `net_errtext`; named
  errno constants (`errno.inc`);
- **buffer safety** — `net_read_all` / `net_read_until` bound appends to the
  caller buffer (`maxlength`), so a long stream cannot overrun.

Still to add for a fully production-grade library (out of current scope):

- **multi-client event loop** — a server must `select`/`poll` across many
  connections; today only single-fd `net_poll` exists, and the CONTROLLED
  LIFO pool makes many-outstanding-connections awkward;
- **connect-timeout orchestration** — wiring `net_connect_nb` + a poll-with-
  deadline into a single blocking `net_connect_to(h, host, port, ms)`;
- **TLS** (out of scope; pair libnet with OpenSSL at a higher layer);
- **thread-safety** — the CONTROLLED pool is per-thread; cross-thread sharing
  of a handle is not guarded.

## Error model

- `condition neterror` — hard error; `oncode()` = `errno`.
- `condition nettimeout` — read/write timeout or `EAGAIN`/`EINTR`.

## Status

The implementation is written idiomatic and complete **as if the pli-llvm
wishlist is already implemented**. Today `make all` builds the C bridge, and
`make test` runs the `netc_*` bridge regression test (passing). The PL/I
program path blocks on the wishlist; the deprecated original (Iron Spring
`linux/386`) is preserved under `deprecated/`.
