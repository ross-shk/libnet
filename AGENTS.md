# libnet — Agent Guide

Small PL/I networking lib for Iron Spring `1.4.1` (`linux/386`).

## What it is
`libnet.a` + `net.inc` wraps `socket`/`bind`/`listen`/`accept`/`connect`/`read`/`write`/`send`/`recv`/`close`/`shutdown`/`getaddrinfo` via `source/c_bridge.c` → `include/c_bridge.inc` (`options(linkage(system))`). `conncb` `include/type_defs.inc:30` (`fd`/`ip_addr`/`host_name`/`port`/`is_connected`/`read_timeout`/`write_timeout`) is `aligned based` — use `dcl conn like conncb;`.

## Layout
- `include/type_defs.inc` — `%replace` types (`sock_fd_t` etc) + `AF`/`SOCK_TYPE`/`INADDR` + `conncb`
- `include/c_bridge.inc` — `c_socket`/`c_bind`/`c_connect`/`c_read`/`c_write`/`c_close`/`c_set_timeout`…
- `include/net_base.inc` — client `net_open`/`net_connect`/`net_dial`/`net_resolve`/`net_read`/`net_write`…
- `include/net_server.inc` — `net_listen`/`net_accept`
- `include/net_errors.inc` — `neterror` condition (`oncode` = `errno` via `c_get_errno`)
- `include/net.inc` — re-exports `net_errors` + `net_base` + `net_server` (single `%include net;`)
- `source/net.pli` — client package `net: package exports(...)` (11 procs + `apply_timeout` helper)
- `source/net_server.pli` — server package (2 procs)
- `source/c_bridge.c` — `bind_to_port`/`connect_to_host`/`resolve_hostname`/`c_set_timeout`/`default_accept`
- `build.sh` — `build|run <src.pli>` auto-Docker on macOS, fallback to `../libnet.a` if `pkg-config` missing
- `tests/` — `server.pli` fixture (echo/http on `18080`, `/delay` sleeps `2000` via `delay`) + `http_get`/`echo`/`resolve_dial`/`close_shutdown`/`timeout` (5 e2e)
- `examples/` — `readme_usage`/`resolve`/`use_socket` + `client_server/`

## Build / Test
```sh
make                # auto Docker on Darwin if no plic (USE_DOCKER=1)
make test           # builds tests/server, runs 5 e2e (server & client share localhost via single docker bash)
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
