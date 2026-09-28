# libnet API reference

Structured reference for every public function in the PL/I library, written so
an agent (or human) can call the library correctly from the signatures alone.

The library is a **compiled PL/I module** (`source/net.pli`) archived into
`libnet.a`. A program does `%include net;` (which supplies the constants, the
four conditions, and the external `net_*` entry declarations — not the
implementation), then links `-lnet + libpli.a` and calls the entries below.

## Error model

libnet reports **all** failures through conditions (the idiomatic PL/I
mechanism), never through return codes. A returned value is **data only**: a
byte count, a delimiter position, a readiness mask, or a pointer handle.
A procedure that can fail raises one of the conditions below, which the caller
intercepts with `ON`. `oncode()` recovers the detail.

| Condition | Meaning | `oncode()` |
|---|---|---|
| `net_error` | hard error | POSIX errno (always positive) |
| `net_timeout` | read/write timeout or `EAGAIN`/`EINTR` | `NET_ERR_TIMEOUT` (-3) |
| `net_eof` | peer closed / end of stream | `NET_ERR_EOF` (-2) |
| `net_overflow` | caller buffer filled mid-stream | `NET_ERR_OVERFLOW` (-4) |

Pointer-returning functions return the **null handle** on failure (after
raising the appropriate condition).

### oncode sentinels

| Constant | Value | Condition |
|---|---|---|
| `NET_ERR_EOF` | -2 | `net_eof` |
| `NET_ERR_TIMEOUT` | -3 | `net_timeout` |
| `NET_ERR_OVERFLOW` | -4 | `net_overflow` |

These are oncode values, **not** return codes. POSIX errno (always positive) is
carried by the `net_error` condition. Note `net_poll` does **not** raise
`net_timeout` on a timeout — a poll timeout is the normal "nothing ready"
result (returns 0).

## Client (net module — client procedures)

All client procedures take a handle from `net_open` unless noted.

| Function | Signature | Returns | Raises |
|---|---|---|---|
| `net_open` | `(family, type, proto)` | pointer handle, or null | `net_error` |
| `net_connect` | `(h, host, port)` | — (call) | `net_error` |
| `net_dial` | `(h, hostport, family)` | — (call) | `net_error` |
| `net_connect_nb` | `(h, host, port)` | — (call; in-progress is normal, not an error) | `net_error` |
| `net_connect_finish` | `(h)` | — (call) | `net_error` |
| `net_close` | `(h)` | — | — |
| `net_shutdown` | `(h, how)` | — (call) | `net_error` |
| `net_read` | `(h, buffer)` | byte count | `net_eof`, `net_timeout`, `net_error` |
| `net_read_all` | `(h, buffer)` | total count (EOF = normal end, no condition). Accumulates into an internal `CONTROLLED` buffer, then copies into the caller `VARYING` buffer up to its capacity. | `net_timeout`, `net_error`, `net_overflow` (bytes that fit are handed over first) |
| `net_read_until` | `(h, buffer, delim)` | delimiter position | `net_eof`, `net_timeout`, `net_error`, `net_overflow` |
| `net_write` | `(h, buffer)` | byte count | `net_timeout`, `net_error` |
| `net_send` | `(h, buffer, flags)` | byte count | `net_timeout`, `net_error` |
| `net_send_all` | `(h, buffer)` | total byte count | `net_error` |
| `net_send_once` | `(h, buffer, flags)` | byte count | `net_timeout`, `net_error` |
| `net_sendto` | `(h, buffer, ip, port)` | byte count | `net_timeout`, `net_error` (UDP) |
| `net_recvfrom` | `(h, buffer, ip, port)` | byte count | `net_timeout`, `net_error` (UDP) |
| `net_poll` | `(h, events, ms)` | ready mask, 0 on timeout | `net_error` |
| `net_set_timeout` | `(h, rto, wto)` | — (call) | `net_error` |
| `net_set_nonblocking` | `(h, on)` | — (call) | `net_error` |
| `net_setopt` | `(h, opt, value)` | — (call) | `net_error` |
| `net_set_linger` | `(h, on, seconds)` | — (call) | `net_error` |
| `net_peer` | `(h, ip, port)` | — (call; fills ip/port) | `net_error` |
| `net_local` | `(h, ip, port)` | — (call; fills ip/port) | `net_error` |
| `net_resolve` | `(host, ip)` | — (call; fills ip) | `net_error` |
| `net_strerror` | `(e, buf)` | — (call; fills buf) | — |

## Server (net module — server procedures)

| Function | Signature | Returns | Raises |
|---|---|---|---|
| `net_listen` | `(port, backlog)` | server pointer handle, or null | `net_error` |
| `net_accept` | `(server, client)` | — (call; sets `client` handle) | `net_error` |

## Option codes (net_setopt)

| Code | Constant | Effect |
|---|---|---|
| 0 | `NETOPT_REUSEADDR` | SO_REUSEADDR |
| 1 | `NETOPT_KEEPALIVE` | SO_KEEPALIVE |
| 2 | `NETOPT_NODELAY` | TCP_NODELAY |

## Constants (POSIX mirror)

`AF_INET`, `AF_INET6`, `SOCK_STREAM`, `SOCK_DGRAM`, `IPPROTO_TCP`,
`IPPROTO_UDP`, `SHUT_RD`, `SHUT_WR`, `SHUT_RDWR`, `POLLIN`, `POLLOUT`,
`MSG_DONTWAIT`, and errno names (`EAGAIN`, `EBADF`, `ECONNREFUSED`, ...).
