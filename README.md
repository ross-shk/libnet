# libnet — PL/I-centric socket library (thin C, PL/I does the work)

## Requirements

- **pli-llvm** — modern open-source PL/I compiler targetting LLVM
  - GitHub: [https://github.com/pli-llvm/pli-llvm](https://github.com/pli-llvm/pli-llvm)
  - Required for compiling `source/net.pli` and linking PL/I programs against
    the C bridge (`source/c_bridge.c`)
- **C compiler** (clang/gcc/MSVC/MinGW) — for the C bridge
- **CMake ≥ 3.16** — portable build system (Linux, macOS, Windows)
- **make** — CMake generator backends (Ninja, Unix Makefiles, MSBuild, etc.)
  are all compatible; a working backend is needed automatically

## Quick Start

```bash
# Build the library
cmake -S . -B build && cmake --build build

# Compile and run the simple example (plic required)
cmake --build build --target simple_usage
./build/simple_usage
```

The example (see `examples\simple_usage.pli`) connects to `example.com:80`, sends an HTTP GET request, and reads the full response using `net_read_all`:

```pli
%include net;

dcl conn      pointer;
dcl resp      char(*) varying controlled;

conn = net_open(AF_INET, SOCK_STREAM, 0);
call net_set_timeout(conn, 10000, 10000);

call net_connect(conn, 'example.com', 80);
call net_send_all(conn, 'GET / HTTP/1.0' || '0D0A'x
   || 'Host: example.com' || '0D0A'x || 'Connection: close' || '0D0A0A'x);

len = net_read_all(conn, resp);

put skip list('fetched', len, 'bytes');
call net_close(conn);
```

## Architecture

A connection-oriented socket library whose design inverts the usual split:

- **minimal C** (`source/c_bridge.c`): only the raw syscalls PL/I cannot perform — `socket/bind/listen/accept/connect/send/recv/close/shutdown/ settimeout/setnonblock/poll/getpeername` plus an `errno` cache. Reached from PL/I through pli-llvm's by-value C FFI.
- **most processing in PL/I** (`source/net.pli`, a compiled module): the connection pool, receive buffering, delimiter scanning, retry loops, timeout and error policy — all in PL/I, using `CONTROLLED` storage and pointer handles. The module is archived into `libnet.a` alongside the C bridge, so programs link it like any library instead of `%include`ing the implementation.

Callers hold only a `POINTER` handle; the connection structure layout is private to the library. This is the classic PL/I "fat runtime / task context" pattern — well-suited to AI-authored PL/I, where a handful of thin PL/I calls replace a page of socket bookkeeping.

### Style

The PL/I follows classic PL/I conventions:

- `CONTROLLED` connection records declared `ALIGNED` for efficient member access;
- a module header (name / author / purpose / calling sequence) atop every include;
- declarations grouped by kind (parameters, automatic, builtins) and commented by section;
- errno exposed as `%replace` named constants (`errno.inc`) rather than magic numbers;
- ON-units kept to a single action and re-arming the condition (`ON ... SYSTEM;`) inside the unit so a failure cannot recurse;
- `SELECT` over laddered `IF` for multi-way dispatch.

### Naming

Names follow classic PL/I convention: short meaningful module prefixes, lowercase verbs for internal procedures, and descriptive constants.

| Layer                        | Prefix / form   | Example                                                                 |
| ---------------------------- | --------------- | ----------------------------------------------------------------------- |
| C bridge (system primitives) | `netc_`         | `netc_socket`, `netc_recv`, `netc_sockopt`                              |
| Public PL/I procedures       | `net_`          | `net_open`, `net_connect`, `net_read_until`                             |
| Option codes                 | `NETOPT_*`      | `NETOPT_REUSEADDR`, `NETOPT_KEEPALIVE`, `NETOPT_NODELAY`                |
| Status codes                 | `NET_ERR_*`     | `NET_ERR_EOF`, `NET_ERR_TIMEOUT`, `NET_ERR_OVERFLOW` (oncode sentinels) |
| errno constants              | `E*`            | `EAGAIN`, `EBADF`, `ECONNREFUSED` (in `errno.inc`)                      |
| POSIX mirror                 | as-is           | `AF_INET`, `SOCK_STREAM`, `SHUT_WR`, `POLLIN`                           |
| Conditions                   | lowercase       | `net_error`, `net_timeout`, `net_eof`, `net_overflow`                   |
| Internal helpers             | lowercase verbs | `raise_err`, `raise_timeout`, `raise_eof`, `raise_overflow`             |

### Layout

| Path                                                      | Contents                                                                                |
| --------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| `source/c_bridge.c`                                       | the ONLY C — thin syscall wrappers (`netc_*`)                                           |
| `source/net.pli`                                          | the compiled PL/I module — client + server API, connection pool, error policy           |
| `include/net.inc`                                         | interface include — constants + conditions + external `net_*` entries (`%include net;`) |
| `include/type_defs.inc`                                   | constants (`AF_*`, `SOCK_*`, `NET_*`), sizes                                            |
| `include/errno.inc`                                       | POSIX errno as `%replace` named constants                                               |
| `include/c_bridge.inc`                                    | by-value FFI declarations for the C bridge (module-only)                                |
| `docs/api.md`                                             | structured reference of every function (signature, returns, raises)                     |
| `tests/c_bridge.c`                                        | C regression test for the bridge bindings                                               |
| `examples/echo_server.pli`                                | echo server + client demo                                                               |
| `examples/client.pli`                                     | minimal TCP client                                                                      |
| `examples/resolve.pli`                                    | DNS resolution demo                                                                     |
| `examples/fetch.pli`                                      | fetch example.com via `net_read_all` (bounded auto-accumulation)                        |
| `examples/fetch_dyn.pli`                                  | fetch example.com with a manual `CONTROLLED` grow-loop                                  |
| `examples/http_client.pli`                                | fetch example.com via `net_read_all` (auto-growing buffer)                              |
| `examples/test_read_all.pli`, `examples/test_resolve.pli` | scratch checks for `net_read_all` / `net_resolve`                                       |

## Build

```bash
# Build the library (C bridge + PL/I module if plic available) + dist/net.inc
cmake -S . -B build
cmake --build build

# Run the C bridge regression test
ctest --test-dir build --output-on-failure

# Trial-compile all examples (requires plic)
cmake --build build --target libnet-examples
```

### CMake (portable, incl. Windows)

```powershell
# MinGW-w64 GCC needs its bin dir (libwinpthread-1.dll) on PATH first:
$env:PATH = "C:\...\mingw64\bin;" + $env:PATH

cmake -S . -B build -G "MinGW Makefiles"   # or "Visual Studio 17 2022", Ninja, ...
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix C:\libnet   # lib/libnet.a, include/net.inc, ...
```

- Use `-DCMAKE_BUILD_TYPE=Release` for release builds (enables `-O2`/`-O3`
  optimization; CMake does not optimize by default in non-release builds).
- The C bridge links Winsock2 (`ws2_32`) automatically on Windows; no
  manual `WSAStartup` is needed — the bridge initialises Winsock lazily.
- `plic` is **optional**: when it is found (via `PATH`,
  `-DPLIC_EXECUTABLE=...`, or `-DPLI_LLVM=...`), `source/net.pli` is
  compiled and archived into the static lib; when it is absent (typical on
  Windows) the C bridge and its regression test still build and run. Extra
  `plic` flags: `-DLIBNET_PLIFLAGS="..."`.
- `cmake --build build --target libnet-examples` trial-compiles the
  examples (needs `plic`); `libnet_add_pli_program(name src)` (see
  `CMakeLists.txt`) compiles and links a single PL/I program against the
  library.
- `cmake --build build --target libnet_uninstall` removes all installed
  files (mirrors the uninstall step).

The library is a real linked module: `source/net.pli` is compiled once to
`net.o` and archived into `libnet.a` beside the C bridge. A program pulls in
only the **interface** (`%include net;`), then links `-lnet + libpli.a` in a
single `plic` invocation. The `simple_usage` CMake target mirrors this:

```bash
cmake --build build --target simple_usage
```

`ctest --test-dir build --output-on-failure` runs the `netc_*` bridge regression
test (passing). `cmake --build build` builds the C bridge, then compiles
`source/net.pli` when plic is available.

`cmake --build build --target libnet-examples` trial-compiles every example
(compile-only).

## API sketch

```pli
%include net;
dcl conn pointer;

conn = net_open(AF_INET, SOCK_STREAM, 0);        /* library allocates (CONTROLLED) */
call net_connect(conn, '127.0.0.1', 8090);
call net_setopt(conn, NETOPT_KEEPALIVE, 1);      /* socket options */
bytes = net_write(conn, 'hello');
bytes = net_read_until(conn, buf, '0A'x);        /* read a line; net_eof ends it */
call net_close(conn);                            /* frees (CONTROLLED pop) */

/* All failures arrive as conditions; oncode() recovers the detail. */
on condition(net_error) begin; ... end;
on condition(net_timeout) begin; ... end;
on condition(net_eof) begin; ... end;
on condition(net_overflow) begin; ... end;
```

## Production readiness

The C bridge and PL/I layer cover the core of what a production socket client or single-client server needs:

- **socket options** — `net_setopt` (`NETOPT_REUSEADDR` / `KEEPALIVE` / `NODELAY`) and `net_set_linger`; platform values live in C, not PL/I;
- **addresses** — `net_peer` (remote) and `net_local` (bound local addr/port);
- **DNS** — `net_resolve` (host ↔ dotted quad), plus connect-time resolution;
- **nonblocking connect** — `net_connect_nb` + `net_connect_finish `(`EINPROGRESS` → poll writable → check `SO_ERROR`);
- **UDP** — `net_sendto` / `net_recvfrom` datagram send/receive;
- **errors** — conditions-only model (`net_error` / `net_timeout` / `net_eof` / `net_overflow`), `net_strerror` (full errno text) and `net_errtext`; named errno constants (`errno.inc`).

Still to add for a fully production-grade library (out of current scope):

- **multi-client event loop** — a server must `select`/`poll` across many connections; today only single-fd `net_poll` exists, and the CONTROLLED LIFO pool makes many-outstanding-connections awkward;
- **connect-timeout orchestration** — wiring `net_connect_nb` + a poll-with-deadline into a single blocking `net_connect_to(h, host, port, ms)`;
- **TLS** (out of scope; pair libnet with OpenSSL at a higher layer);
- **thread-safety** — the CONTROLLED pool is per-thread; cross-thread sharing of a handle is not guarded.

## Error model

`libnet` reports **all** failures through conditions (the idiomatic PL/I mechanism), never through return codes. A returned value is **data only**: a byte count, a delimiter position, a readiness mask, or a pointer handle.

A procedure that can fail raises one of these conditions, intercepted with `ON`; `oncode()` recovers the detail.

- `condition net_error` — hard error; `oncode()` = POSIX `errno`.
- `condition net_timeout` — read/write timeout or `EAGAIN`/`EINTR`;  
`oncode()` = `NET_ERR_TIMEOUT`.
- `condition net_eof` — peer closed / end of stream; `oncode()` = `NET_ERR_EOF`.
- `condition net_overflow` — caller buffer filled mid-stream;  
`oncode()` = `NET_ERR_OVERFLOW`.

Use `net_errtext()` for human-readable error messages.

Note `net_poll` does **not** raise `net_timeout` on a timeout — a poll timeout is the normal "nothing ready" result (returns 0).

## Status

The library is a compiled module rather than `%include`d source.  
`ctest --test-dir build --output-on-failure` runs the `netc_*` bridge regression
test (passing);

`cmake --build build` builds the C bridge, then compiles `source/net.pli`
when plic is available;

`cmake --build build --target libnet-examples` trial-compiles the examples.
