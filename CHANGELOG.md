# Changelog — libnet major/breaking

Follows Keep a Changelog. `neterror` `oncode()` = `errno` via `c_get_errno` unless noted.

## [Unreleased] — 2026-09-02

### Breaking
- `net_read_all` now `net_read_all(conn, p pointer) returns(size_t)` `include/net_base.inc:51` `source/net.pli:248` — `p` is `pointer` to `char(MAX_VARYING) varying based(p)` heap (`MAX_VARYING 32767` `include/type_defs.inc:16`, `maxlength()` guard). Hides `c_read` `4096` loop until `0` EOF, `allocate heap set(p)` inside, caller `free`. Old `net_read_all(conn, buffer char(*))` bounded `length(buffer)` removed. `net_read` `source/net.pli:222` kept for chunking (`poll`/`NONBLOCK`). Migration: `dcl resp char(4096); n=net_read_all(conn,resp);` → `dcl p pointer init(null); dcl resp char(MAX_VARYING) varying based(p); n=net_read_all(conn,p); free resp;`. Signals `ERR.TOOLARGE 75` if `>maxlength`.
- `net_set_nonblocking(conn, enable)` → pair `net_set_nonblocking(conn)` / `net_set_default(conn)` `source/net.pli:353` `include/net_base.inc:76` (`c_set_nonblocking` `source/c_bridge.c:125` `fcntl O_NONBLOCK`). Blocking is default (`net_open` `SOCK_FLAGS` `0`). Old `enable 1/0` flag removed.
- `ERR` struct `include/type_defs.inc:49` `dcl 1 ERR static, 2 AGAIN 11 / WOULDBLOCK 35 / TIMEDOUT 110 / BADURL 61 / TOOLARGE 75` replaces `%replace EAGAIN` etc. Code now `oncode(ERR.AGAIN)` `source/net.pli:121` etc, `oncode(ERR.BADURL)` `61` `net_dial`.
- `net_read_all` signature `pointer` breaks `tests/http_get.pli:28` `examples/readme_usage.pli:33` — updated to heap `based`.

### Added
- `net_poll(conn, timeout, events)` `source/net.pli:323` `c_poll` `source/c_bridge.c:114` `poll(2)` `POLL.IN/OUT/ERR/HUP/NVAL` `include/type_defs.inc:50`.
- `c_set_nonblocking` + `net_set_nonblocking`/`net_set_default` for `O_NONBLOCK` explicit toggling vs per-call `MSG_FLAG.DONTWAIT` `tests/nonblocking.pli:28`.
- `c_getsockname`/`c_getpeername` `source/c_bridge.c:104` — `net_listen(port 0)` ephemeral via `c_getsockname` writes `conn.port`, `net_accept` fills `client.ip_addr/port/host_name` via `c_getpeername` `source/net_server.pli:34`.
- `MAX_VARYING 32767` + `maxlength()` guard `source/net.pli:287` to avoid `char(32767)` magic `examples/read_unknown.pli:21`.
- `opencode.json` `watcher.ignore` `dist/*, *.o, libnet.a, tests/*.o` (keeps `*.lst` for diagnostics).

### Changed
- `net_read`/`net_write`/`net_recv`/`net_send` now return `0` on `EAGAIN/WOULDBLOCK` when `MSG_FLAG.DONTWAIT` or `SOCK_FLAGS.NONBLOCK` instead of signalling `neterror` — enables `net_poll`+`net_read` non-blocking loop `source/net.pli:121`.
- `net_read_all` now wrapper around `net_read` with nested helpers `init_heap`/`append_chunk`/`read_chunk`/`cleanup_and_signal` `source/net.pli:256` for clarity vs direct `c_read`.
- `include/type_defs.inc:17` `AF/SOCK_TYPE/SOCK_FLAGS/INADDR/MSG_FLAG/SHUT/POLL` now `/* AF_INET */` etc C-mappable comments; `SOMAXCONN` kept; unwired `PF/IPPROTO/SOL/SO/TCP/AI/NI` removed.

### Fixed
- `examples/readme_usage.pli:19` `char(32)` truncation ` -m(2,72)` → `char(MAX_VARYING)` + `conn.read_timeout/write_timeout 5000` after `net_dial` to avoid hang on `example.com:80`.
- `tests/close_shutdown.pli:30` `net_shutdown(conn,1)` → `SHUT.WR`, `tests/*` `if bytes<=0`/`if bytes<0` → `=0` EOF (error via `neterror`), `bytes=net_close` → `call net_close` where return ignored.
- `include/net_helpers.inc` deduplicated `apply_timeout` from `source/net.pli:11` + `source/net_server.pli:8` (internal, not in `net.inc`/`dist/net.inc`).
- `¬` (`0xac` single-byte, not UTF-8 `c2 ac`) `include/net_helpers.inc:11` `mod` handling for `NONBLOCK*2`/`DONTWAIT*2` vs magic `4096`/`128`, `c_get_errno()=11|35` → `ERR.AGAIN|WOULDBLOCK`, `oncode(61)` → `ERR.BADURL` `source/net.pli:84`.

## 2026-09-01 — flags / ephemeral / timeout
- `net_send`/`net_recv` with `MSG_FLAG` `0` test `tests/send_recv.pli:21`, `SOCK_FLAGS` `CLOEXEC/NONBLOCK`, `INADDR`, `SHUT`, `POLL` added `92efae9`; `net_accept` peer fill + `net_listen 0` ephemeral `d22f866`.
- `read_timeout`/`write_timeout` `conncb` `include/type_defs.inc:65` via `c_set_timeout` `SO_RCVTIMEO` `source/c_bridge.c:91` `tests/timeout.pli:28`.

## 2026-08-31 — docs / layout
- `docs/api.md` human reference + `AGENTS.md:48` token-optimized `include/net_helpers` note; `examples/ephemeral.pli` `tests/ephemeral.pli:19`.
- `improvements.md` phases 1-5 hygiene/naming/type aliases complete — removed, superseded by this log.
