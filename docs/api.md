# libnet API reference

Structured reference for every public function in the PL/I library, written so
an agent (or human) can call the library correctly from the signatures alone.

## Error model

libnet reports **all** failures through conditions (the idiomatic PL/I
mechanism), never through return codes. A returned value is **data only**: a
byte count, a delimiter position, a readiness mask, or a pointer handle.
A procedure that can fail raises one of the conditions below, which the caller
intercepts with `ON`. `oncode()` recovers the detail.

| Condition | Meaning | `oncode()` |
|---|---|---|
| `neterror` | hard error | POSIX errno (always positive) |
| `nettimeout` | read/write timeout or `EAGAIN`/`EINTR` | `NET_TIMEOUT` (-3) |
| `netEOF` | peer closed / end of stream | `NET_EOF` (-2) |
| `netOverflow` | caller buffer filled mid-stream | `NET_OVERFLOW` (-4) |

Pointer-returning functions return the **null handle** on failure (after
raising the appropriate condition).

### oncode sentinels

| Constant | Value | Condition |
|---|---|---|
| `NET_EOF` | -2 | `netEOF` |
| `NET_TIMEOUT` | -3 | `nettimeout` |
| `NET_OVERFLOW` | -4 | `netOverflow` |

These are oncode values, **not** return codes. POSIX errno (always positive) is
carried by the `neterror` condition. Note `net_poll` does **not** raise
`nettimeout` on a timeout — a poll timeout is the normal "nothing ready"
result (returns 0).

## Client (net_base)

All client procedures take a handle from `net_open` unless noted.

| Function | Signature | Returns | Raises |
|---|---|---|---|
| `net_open` | `(family, type, proto)` | pointer handle, or null | `neterror` |
| `net_connect` | `(h, host, port)` | — (call) | `neterror` |
| `net_dial` | `(h, hostport, family)` | — (call) | `neterror` |
| `net_connect_nb` | `(h, host, port)` | — (call; in-progress is normal, not an error) | `neterror` |
| `net_connect_finish` | `(h)` | — (call) | `neterror` |
| `net_close` | `(h)` | — | — |
| `net_shutdown` | `(h, how)` | — (call) | `neterror` |
| `net_read` | `(h, buffer, buflen)` | byte count | `netEOF`, `nettimeout`, `neterror` |
| `net_read_all` | `(h, buffer)` | total count (EOF = normal end, no condition) | `nettimeout`, `neterror`, `netOverflow` |
| `net_read_until` | `(h, buffer, delim)` | delimiter position | `netEOF`, `nettimeout`, `neterror`, `netOverflow` |
| `net_write` | `(h, buffer)` | byte count | `nettimeout`, `neterror` |
| `net_send` | `(h, buffer, flags)` | byte count | `nettimeout`, `neterror` |
| `net_send_all` | `(h, buffer)` | total byte count | `neterror` |
| `net_send_once` | `(h, buffer, flags)` | byte count | `nettimeout`, `neterror` |
| `net_sendto` | `(h, buffer, ip, port)` | byte count | `nettimeout`, `neterror` (UDP) |
| `net_recvfrom` | `(h, buffer, ip, port)` | byte count | `nettimeout`, `neterror` (UDP) |
| `net_poll` | `(h, events, ms)` | ready mask, 0 on timeout | `neterror` |
| `net_set_timeout` | `(h, rto, wto)` | — (call) | `neterror` |
| `net_set_nonblocking` | `(h, on)` | — (call) | `neterror` |
| `net_setopt` | `(h, opt, value)` | — (call) | `neterror` |
| `net_set_linger` | `(h, on, seconds)` | — (call) | `neterror` |
| `net_peer` | `(h, ip, port)` | — (call; fills ip/port) | `neterror` |
| `net_local` | `(h, ip, port)` | — (call; fills ip/port) | `neterror` |
| `net_resolve` | `(host, ip)` | — (call; fills ip) | `neterror` |
| `net_strerror` | `(e, buf)` | — (call; fills buf) | — |

## Server (net_server)

| Function | Signature | Returns | Raises |
|---|---|---|---|
| `net_listen` | `(port, backlog)` | server pointer handle, or null | `neterror` |
| `net_accept` | `(server, client)` | — (call; sets `client` handle) | `neterror` |

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
