# libnet API reference

Structured reference for every public function in the PL/I library, written so
an agent (or human) can call the library correctly from the signatures alone.

## Return conventions

- A **non-negative** return is a count / success.
- A **negative** return is always a status constant, never a count:

| Constant | Value | Meaning |
|---|---|---|
| `NET_OK` | 0 | success |
| `NET_ERR` | -1 | hard error — `condition(neterror)` raised |
| `NET_EOF` | -2 | peer closed / end of stream |
| `NET_TIMEOUT` | -3 | timeout or EAGAIN/EINTR — `condition(nettimeout)` raised |
| `NET_OVERFLOW` | -4 | caller buffer exhausted mid-stream |

Pointer-returning functions return the **null handle** on failure.

## Conditions

| Condition | Meaning |
|---|---|
| `neterror` | hard error; `oncode()` = errno |
| `nettimeout` | timeout / EAGAIN / EINTR |

## Client (net_base)

All client procedures take a handle from `net_open` unless noted.

| Function | Signature | Returns | Raises |
|---|---|---|---|
| `net_open` | `(family, type, proto)` | pointer handle, or null | `neterror` |
| `net_connect` | `(h, host, port)` | `NET_OK` / `NET_ERR` | `neterror` |
| `net_dial` | `(h, hostport, family)` | `NET_OK` / `NET_ERR` | `neterror` |
| `net_connect_nb` | `(h, host, port)` | `NET_OK` / `NET_TIMEOUT`(in-progress) / `NET_ERR` | `neterror` |
| `net_connect_finish` | `(h)` | `NET_OK` / `NET_ERR` | `neterror` |
| `net_close` | `(h)` | — | — |
| `net_shutdown` | `(h, how)` | `NET_OK` / `NET_ERR` | `neterror` |
| `net_read` | `(h, buffer, buflen)` | count, or `NET_EOF`/`NET_TIMEOUT`/`NET_ERR` | `neterror`, `nettimeout` |
| `net_read_all` | `(h, buffer)` | total count, or `NET_TIMEOUT`/`NET_ERR`/`NET_OVERFLOW` | `neterror`, `nettimeout` |
| `net_read_until` | `(h, buffer, delim)` | delimiter position, or `NET_EOF`/`NET_TIMEOUT`/`NET_ERR`/`NET_OVERFLOW` | `neterror`, `nettimeout` |
| `net_write` | `(h, buffer)` | count, or `NET_TIMEOUT`/`NET_ERR` | `neterror`, `nettimeout` |
| `net_send` | `(h, buffer, flags)` | count, or `NET_TIMEOUT`/`NET_ERR` | `neterror`, `nettimeout` |
| `net_send_all` | `(h, buffer)` | total count, or `NET_TIMEOUT`/`NET_ERR` | `neterror`, `nettimeout` |
| `net_send_once` | `(h, buffer, flags)` | count, or `NET_TIMEOUT`/`NET_ERR` | `neterror`, `nettimeout` |
| `net_sendto` | `(h, buffer, ip, port)` | count, or `NET_TIMEOUT`/`NET_ERR` | `neterror`, `nettimeout` (UDP) |
| `net_recvfrom` | `(h, buffer, ip, port)` | count, or `NET_TIMEOUT`/`NET_ERR` | `neterror`, `nettimeout` (UDP) |
| `net_poll` | `(h, events, ms)` | ready mask, 0 on timeout, `NET_ERR` | `neterror` |
| `net_set_timeout` | `(h, rto, wto)` | `NET_OK` / `NET_ERR` | `neterror` |
| `net_set_nonblocking` | `(h, on)` | `NET_OK` / `NET_ERR` | `neterror` |
| `net_setopt` | `(h, opt, value)` | `NET_OK` / `NET_ERR` | `neterror` |
| `net_set_linger` | `(h, on, seconds)` | `NET_OK` / `NET_ERR` | `neterror` |
| `net_peer` | `(h, ip, port)` | `NET_OK` / `NET_ERR` | `neterror` |
| `net_local` | `(h, ip, port)` | `NET_OK` / `NET_ERR` | `neterror` |
| `net_resolve` | `(host, ip)` | `NET_OK` / `NET_ERR` | `neterror` |
| `net_strerror` | `(e, buf)` | — | — |

## Server (net_server)

| Function | Signature | Returns | Raises |
|---|---|---|---|
| `net_listen` | `(port, backlog)` | server pointer handle, or null | `neterror` |
| `net_accept` | `(server, client)` | `NET_OK` / `NET_ERR`; `client` set to a handle | `neterror` |

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
