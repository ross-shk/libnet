# libnet — PL/I-centric socket library (thin C, PL/I does the work)

## Requirements

- **libsock** — the socket compatibility layer (separate project, consumed as a black-box dependency)
  - Build and install libsock first; libnet finds it via `-DLIBSOCK_ROOT=<prefix>` (auto-detected from `/usr/local` or `/opt/libsock`)
- **pli-llvm** — modern open-source PL/I compiler targetting LLVM
  - GitHub: [https://github.com/pli-llvm/pli-llvm](https://github.com/pli-llvm/pli-llvm)
  - Required for compiling `source/net.pli` and linking PL/I programs against
    the library
- **C compiler** (clang/gcc/MSVC/MinGW) — for the libsock C bridge
- **CMake ≥ 3.16** — portable build system (Linux, macOS, Windows)
- **make** — CMake generator backends (Ninja, Unix Makefiles, MSBuild, etc.)
  are all compatible; a working backend is needed automatically

> **Platform note:** `libsock`'s `include/errno.inc` and `include/type_defs.inc`
> currently contain macOS arm64 socket/errno values. The compiled PL/I layer
> therefore targets macOS arm64; Linux/Windows need matching constant files.

## Quick Start

```bash
# 1. Build and install libsock (the socket compatibility layer)
cmake -S /path/to/libsock -B build-libsock
cmake --build build-libsock
cmake --install build-libsock                  # to /usr/local by default

# 2. Build libnet against the installed libsock
cmake -S . -B build -DLIBSOCK_ROOT=/path/to/libsock-install
cmake --build build

# 3. Compile and run the simple example (pli-llvm required)
cmake --build build --target simple_usage
./build/simple_usage
```

> **Windows note:** The Visual Studio CMake generator invokes MSBuild, which
> sets up the MSVC/Windows SDK environment automatically — no separate
> Developer Command Prompt is needed for `cmake --build`. When invoking `pli-llvm`
> directly (not through CMake), run from a *Developer Command Prompt for VS*
> so the linker can find `ws2_32.lib` and other SDK libs.

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

- **libsock** (separate project): the socket compatibility layer.
  - `source/c_bridge.c` — the ONLY C, thin syscall wrappers (`c_*` naming) around Winsock2 on Windows; on POSIX, libc symbols resolve directly via pli-llvm's by-value FFI.
  - `source/sock.pli` — PL/I wrappers (`sock_*`) over the raw `c_*` bindings.
  - `include/{sock,c_bridge,errno,type_defs}.inc` — interface declarations and constants.
- **libnet** (this project): consumes `libsock.a` + headers as a black-box dependency.
  - `source/net.pli` — the compiled PL/I module: client + server API, connection pool, receive buffering, delimiter scanning, retry loops, timeout and error policy. Archived into `libnet.a`, so programs link it like any library instead of `%include`ing the implementation.

Callers hold only a `POINTER` handle; the connection structure layout is private to the library. This is the classic PL/I "fat runtime / task context" pattern – well-suited to AI-authored PL/I, where a handful of thin PL/I calls replace a page of socket bookkeeping.

### Style

The PL/I follows classic PL/I conventions:

- per-handle `BASED` connection records, so `net_close` frees the exact allocation;
- a module header (name / author / purpose / calling sequence) atop every include;
- declarations grouped by kind (parameters, automatic, builtins) and commented by section;
- errno exposed as `%replace` named constants (`errno.inc`) rather than magic numbers;
- ON-units kept to a single action and re-arming the condition (`ON ... SYSTEM;`) inside the unit so a failure cannot recurse;
- `SELECT` over laddered `IF` for multi-way dispatch.

### Naming

Names follow classic PL/I convention: short meaningful module prefixes, lowercase verbs for internal procedures, and descriptive constants.

| Layer                        | Prefix / form   | Example                                                                 |
| ---------------------------- | --------------- | ----------------------------------------------------------------------- |
| C bridge (system primitives) | `c_`            | `c_socket`, `c_recv`, `c_setsockopt` (in libsock)                       |
| sock wrappers (libc)         | `sock_`         | `sock_socket`, `sock_poll`, `sock_getaddrinfo` (in libsock)             |
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
| `libnet/source/net.pli`                                   | the compiled PL/I module — client + server API, connection pool, error policy           |
| `libnet/include/net.inc`                                  | interface include — constants + conditions + external `net_*` entries (`%include net;`) |
| `libsock/source/c_bridge.c`                               | the ONLY C — thin syscall wrappers (`c_*`) wrapping Winsock2 on Windows                 |
| `libsock/source/sock.pli`                                 | PL/I wrappers (`sock_*`) over the raw `c_*` syscalls                                    |
| `libsock/include/{sock,c_bridge,errno,type_defs}.inc`    | interface declarations and constants (socket domain, errno, sockaddr sizes, etc.)       |
| `docs/api.md`                                             | structured reference of every function (signature, returns, raises)                     |
| `examples/echo_server.pli`                                | echo server + client demo                                                               |
| `examples/client.pli`                                     | minimal TCP client                                                                      |
| `examples/resolve.pli`                                    | DNS resolution demo                                                                     |
| `examples/fetch.pli`                                      | fetch example.com via `net_read_all` (bounded auto-accumulation)                        |
| `examples/fetch_dyn.pli`                                  | fetch example.com with a manual `CONTROLLED` grow-loop                                  |
| `examples/http_client.pli`                                | fetch example.com via `net_read_all` (auto-growing buffer)                              |
| `examples/test_read_all.pli`, `examples/test_resolve.pli` | scratch checks for `net_read_all` / `net_resolve`                                       |

## Build

```bash
# Build the library (PL/I module if pli-llvm is available) + dist/net.inc
cmake -S . -B build -DLIBSOCK_ROOT=/usr/local   # auto-detected by default
cmake --build build

# Trial-compile all examples (requires pli-llvm)
cmake --build build --target libnet-examples
```

### CMake (portable, incl. Windows)

```powershell
# MinGW-w64 GCC needs its bin dir (libwinpthread-1.dll) on PATH first:
$env:PATH = "C:\...\mingw64\bin;" + $env:PATH

cmake -S . -B build -G "MinGW Makefiles" -DLIBSOCK_ROOT=C:\libsock   # or "Visual Studio 17 2022", Ninja, ...
cmake --build build
cmake --install build --prefix C:\libnet   # lib/net.lib or libnet.a, include/net.inc, ...
```

- Use `-DCMAKE_BUILD_TYPE=Release` for release builds (enables `-O2`/`-O3`
  optimization; CMake does not optimize by default in non-release builds).
- `libsock` must be built and installed first (it provides the C bridge +
  `sock_*` PL/I wrappers that `net.pli` calls). Set `-DLIBSOCK_ROOT=<prefix>`
  when it's not in `/usr/local` or `/opt/libsock`.
- `pli-llvm` is **optional**: when it is found (via `PATH`,
   `-DPLI_EXECUTABLE=...`, or `-DPLI_LLVM=...`), `source/net.pli` is
   compiled and archived into the static lib; when it is absent (typical on
   Windows) the `net` target still builds (just without the PL/I module; it
   links the prebuilt `libsock.a` which contains the C bridge). Extra
    `pli-llvm` flags: `-DLIBNET_PLIFLAGS="..."`. **On Windows**, pli-llvm's linker
   needs the Windows SDK libraries; `cmake --build` (via MSBuild) sets this up
   automatically, but direct `pli-llvm` invocations require a *Developer Command
   Prompt for VS*.
- `cmake --build build --target libnet-examples` trial-compiles the
  examples (needs `pli-llvm`); `libnet_add_pli_program(name src)` (see
  `CMakeLists.txt`) compiles and links a single PL/I program against the
  library.
- `cmake --build build --target libnet_uninstall` removes all installed
  files (mirrors the uninstall step).

The library is a real linked module: `source/net.pli` is compiled once to
`net.o` and archived into the static lib (`libnet.a` on Unix, `net.lib` on
Windows), which links `libsock.a` (the C bridge + `sock_*` wrappers). A
program pulls in only the **interface** (`%include net;`), then links the
static lib + `libpli.a` (via pli-llvm's `-lnet` on Unix, or the `.lib` path on
Windows) in a single `pli-llvm` invocation. The `simple_usage` CMake target
mirrors this:

```bash
cmake --build build --target simple_usage
```

`ctest --test-dir build --output-on-failure` runs the test suite (no CTest
tests are registered after the C bridge was extracted into libsock).
`cmake --build build` builds the `net` target (compiling `source/net.pli`
when pli-llvm is available and linking the prebuilt `libsock.a`).

`cmake --build build --target libnet-examples` trial-compiles every example
(compile-only).

## API sketch

```pli
%include net;
dcl conn pointer;

conn = net_open(AF_INET, SOCK_STREAM, 0);        /* library allocates (BASED) */
call net_connect(conn, '127.0.0.1', 8090);
call net_setopt(conn, NETOPT_KEEPALIVE, 1);      /* socket options */
bytes = net_write(conn, 'hello');
bytes = net_read_until(conn, buf, '0A'x);        /* read a line; 0 on EOF */
call net_close(conn);                            /* frees the exact allocation */

/* All failures arrive as conditions; oncode() recovers the detail. */
on condition(net_error) begin; ... end;
on condition(net_timeout) begin; ... end;
on condition(net_overflow) begin; ... end;
```

## Production readiness

The `net` PL/I layer and its `libsock` dependency cover the core of what a
production socket client or single-client server needs:

- **socket options** — `net_setopt` (`NETOPT_REUSEADDR` / `KEEPALIVE` / `NODELAY`) and `net_set_linger`; platform values live in `libsock`'s `include/type_defs.inc` (macOS arm64; Linux values noted);
- **addresses** — `net_peer` (remote) and `net_local` (bound local addr/port);
- **DNS** — `net_resolve` (host → dotted quad), plus connect-time resolution;
- **nonblocking connect** — `net_connect_nb` + `net_connect_finish `(`EINPROGRESS` → poll writable → check `SO_ERROR`);
- **UDP** — `net_sendto` / `net_recvfrom` datagram send/receive;
- **multi-client servers** — `net_poll_multi` across the server pool, plus `net_server_add_client` / `net_server_client` / `net_server_remove_client` and the `multi_echo_server` example;
- **errors** — conditions-only model (`net_error` / `net_timeout` / `net_eof` / `net_overflow`), `net_strerror` (full errno text) and `net_errtext`; named errno constants (`libsock`'s `errno.inc`).

Still to add for a fully production-grade library (out of current scope):

- **event-loop hardening** — per-client partial reads and backpressure; the current `multi_echo_server` example assumes a complete line is available after poll;
- **connect-timeout orchestration** — wiring `net_connect_nb` + a poll-with-deadline into a single blocking `net_connect_to(h, host, port, ms)`;
- **TLS** (out of scope; pair libnet with OpenSSL at a higher layer);
- **thread-safety** — handles are not thread-safe; cross-thread sharing of a connection is not guarded.

## Error model

`libnet` reports **all** failures through conditions (the idiomatic PL/I mechanism), never through return codes. A returned value is **data only**: a byte count, a delimiter position, a readiness mask, or a pointer handle.

A procedure that can fail raises one of these conditions, intercepted with `ON`; `oncode()` recovers the detail.

- `condition net_error` — hard error; `oncode()` = POSIX `errno`.
- `condition net_timeout` — read/write timeout or `EAGAIN`/`EINTR`;  
`oncode()` = `NET_ERR_TIMEOUT`.
- `condition net_eof` — reserved explicit end-of-stream signal; normal read EOF returns 0; `oncode()` = `NET_ERR_EOF`.
- `condition net_overflow` — caller buffer filled mid-stream;  
`oncode()` = `NET_ERR_OVERFLOW`.

Use `net_errtext()` for human-readable error messages.

Note `net_poll` does **not** raise `net_timeout` on a timeout — a poll timeout is the normal "nothing ready" result (returns 0).

## Status

The library is a compiled module rather than `%include`d source.  
`ctest --test-dir build --output-on-failure` runs the test suite (no tests are
registered after the C bridge moved to libsock);

`cmake --build build` builds the `net` target, linking the prebuilt `libsock.a`
and compiling `source/net.pli` when pli-llvm is available;

`cmake --build build --target libnet-examples` trial-compiles the examples.
