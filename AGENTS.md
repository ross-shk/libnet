# libnet — Agent Guide

Small PL/I networking lib for Iron Spring `1.4.1` (`linux/386`).

## What it is
`libnet.a` + `net.inc` wraps `socket`/`bind`/`listen`/`accept`/`connect`/`read`/`write`/`send`/`recv`/`close`/`shutdown`/`getaddrinfo` via `source/c_bridge.c` → `include/c_bridge.inc` (`options(linkage(system))`). `conncb` `include/type_defs.inc:52` (`fd`/`ip_addr`/`host_name`/`port`/`is_connected`/`read_timeout`/`write_timeout`) is `aligned based` — use `dcl conn like conncb;`.

## Layout
- `include/type_defs.inc` — `%replace` types (`sock_fd_t` etc) + `AF`/`SOCK_TYPE`/`SOCK_FLAGS`/`INADDR`/`MSG_FLAG`/`SHUT`/`SOMAXCONN` + `conncb` (64 lines, ~564 tokens)
- `include/c_bridge.inc` — `c_socket`/`c_bind`/`c_connect`/`c_read`/`c_write`/`c_close`/`c_set_timeout`/`c_getsockname`/`c_getpeername`…
- `include/net_helpers.inc` — **internal** shared `apply_timeout` helper 
- `include/net_base.inc` — client `net_open`/`net_connect`/`net_dial`/`net_resolve`/`net_read`/`net_write`/`net_send`/`net_recv`…
- `include/net_server.inc` — `net_listen`/`net_accept`
- `include/net_errors.inc` — `neterror` condition (`oncode` = `errno` via `c_get_errno`)
- `include/net.inc` — re-exports `net_errors` + `net_base` + `net_server` (single `%include net;`)
- `source/net.pli` — client package `net: package exports(...)` (11 procs + `apply_timeout` helper) — 338 lines, ~2190 tokens
- `source/net_server.pli` — server package (2 procs + `apply_timeout` helper) — 95 lines
- `source/c_bridge.c` — `bind_to_port`/`connect_to_host`/`resolve_hostname`/`c_set_timeout`/`default_accept`/`c_getsockname`/`c_getpeername` — 122 lines
- `build.sh` — `build|run <src.pli>` auto-Docker on macOS, fallback to `../libnet.a` if `pkg-config` missing
- `tests/` — `server.pli` fixture (echo/http on `18080`, `/delay` sleeps `2000` via `delay`) + `http_get`/`echo`/`send_recv`/`resolve_dial`/`close_shutdown`/`timeout`/`ephemeral` (7 e2e)
- `examples/` — `readme_usage`/`resolve`/`use_socket`/`ephemeral` + `client_server/`

## Build / Test
```sh
make                # auto Docker on Darwin if no plic (USE_DOCKER=1)
make test           # builds tests/server, runs 7 e2e (server & client share localhost via single docker bash)
./build.sh run examples/readme_usage.pli
./build.sh run tests/echo.pli   # any single .pli
```

## Using
```sh
# installed
plic -C -dELF -i$(pkg-config --cflags net) foo.pli -o foo.o
gcc -m32 -no-pie -z muldefs -o foo foo.o $(pkg-config --libs net)
# local (from examples/)
plic -C -dELF -i../include foo.pli -o foo.o
gcc -m32 -no-pie -z muldefs -o foo foo.o ../libnet.a -lprf /usr/lib/pli/alt/fhs.o /usr/lib/pli/alt/ghs.o
```

## PL/I Style
- ` -m(2,72)` col2 ` main:` (` hello.pli:1`), body `   dcl` at 3, inner `     display` at 5, ` end main;` at 1
- `lowercase` (`dcl`, `proc`, `package`), one-line `/* intent */` before each block
- `%include net;` via `-i include` (no quotes), `%replace` for types (no `DEFINE ALIAS` on 1.4.1)
- Diagnostics: `*.lst` `grep -E '\(ERR|WRN\)'`, `rc 0` ok `4` warn `8` error — `Makefile` tolerates `4`

## For AI — Token-Optimized Context
Minimal read set for most tasks (~1500 tokens vs ~11300 full): `AGENTS.md` + `include/type_defs.inc` + `include/net_base.inc` + `include/c_bridge.inc` + `docs/api.md:14` (`conncb`/constants). Add `source/net.pli:27` or `source/net_server.pli:25` only for the side you change. **Skip:** `dist/net.inc` (254 lines, generated), `*.lst`/`*.o`/`libnet.a`, `tests/server.pli` unless testing server logic. `net.inc` is single re-export — don't read `net_errors`+`net_base`+`net_server` separately. Example `readme_usage.pli:1` + one test (`echo.pli` or `send_recv.pli`) is enough as few-shot.
