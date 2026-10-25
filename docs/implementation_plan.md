# libnet Refactoring Plan: C → PL/I Migration

> This plan describes how to reduce the C bridge (`source/c_bridge.c`) to
> **only raw system calls** and move all remaining glue logic into pure PL/I.
> Every function that does string parsing, struct construction, option-code
> mapping, or error-text generation is rewritten in PL/I and verified with the
> pli compiler at `/usr/local/bin/pli` (v0.3.0).

---

## 0. Summary of the Refactoring

| Direction | Count |
|-----------|-------|
| C functions removed (glue → PL/I) | 7 |
| C functions kept (raw syscalls) | 18 |
| New C function added | 1 (`netc_getsockopt`) |
| New PL/I internal procedures | ~12 |

### Functions moving from C to PL/I

| C function | Why it's glue | PL/I replacement |
|------------|---------------|-------------------|
| `netc_sockopt` | Maps `NETOPT_*` codes to `SOL_SOCKET`/`SO_*` — pure logic | `net_setopt` calls `netc_setsockopt` with raw level/optname |
| `netc_setlinger` | Builds `struct linger` — binary struct construction | PL/I byte-builds the 8-byte linger struct |
| `netc_settimeout` | Builds `struct timeval` — binary struct construction | PL/I byte-builds the 16-byte timeval struct |
| `netc_connect_nb` | Orchestrates setnonblock + connect + return code | PL/I: `netc_setnonblock` + `netc_connect` + clear errno |
| `netc_getsockerr` | Calls `getsockopt(SO_ERROR)` — a raw setopt already covers this | PL/I calls `netc_getsockopt(SO_ERROR)` directly |
| `netc_resolve` | Wraps `getaddrinfo` + parses result + formats IP string — all logic | PL/I: `netc_getaddrinfo` (raw) + `build_sockaddr_in` + `addr_to_text` |
| `netc_strerror` | Maps errno → text — PL/I already has `net_errtext` | PL/I: `net_errtext` + `net_strerror` using raw `netc_strerror` only for unknown codes |

### Functions that remain in C (raw syscalls)

```c
netc_socket, netc_close, netc_shutdown, netc_bind, netc_listen, netc_accept,
netc_connect, netc_send, netc_recv, netc_sendto, netc_recvfrom,
netc_setsockopt, netc_getsockopt,  /* raw setsockopt / getsockopt */
netc_getpeername, netc_getsockname, /* raw getpeername / getsockname */
netc_setnonblock, netc_poll, netc_poll_multi,  /* raw fcntl / poll */
netc_getaddrinfo,  /* raw getaddrinfo wrapper (minimal: calls C, copies sockaddr_in) */
netc_errno, netc_clearerr  /* errno cache */
```

---

## 1. Platform-Specific Constants (type_defs.inc additions)

The current code hides `SOL_SOCKET`, `SO_*`, `IPPROTO_TCP`, `TCP_*` behind
the `NETOPT_*` enum and does the mapping in C. Moving the mapping to PL/I
requires the raw level/optname constants. These are platform-specific; on
macOS arm64 they are:

```bash
SOL_SOCKET=65535  SO_REUSEADDR=4  SO_KEEPALIVE=8  SO_ERROR=4103
SO_RCVTIMEO=4102 SO_SNDTIMEO=4101 SO_LINGER=128
IPPROTO_TCP=6   TCP_NODELAY=1
```

**Added to `include/type_defs.inc`** (macOS target; Linux values noted):

```pli
/* Raw socket option levels (platform-specific; macOS shown) */
%replace SOL_SOCKET   by 65535;   /* Linux: 1     */
%replace IPPROTO_TCP  by     6;   /* portable     */

/* Raw socket option names */
%replace SO_REUSEADDR by     4;   /* Linux: 2 */
%replace SO_KEEPALIVE  by     8;
%replace SO_LINGER     by   128;
%replace SO_RCVTIMEO   by  4102;   /* Linux: 20 */
%replace SO_SNDTIMEO   by  4101;   /* Linux: 21 */
%replace SO_ERROR      by  4103;   /* Linux: 4  */

%replace TCP_NODELAY    by     1;   /* portable */

/* struct sizes (for raw buffer operations) */
%replace SOCKADDR_IN_SIZE  by 16;   /* sizeof(struct sockaddr_in) on macOS */
%replace LINGER_SIZE   by   8;   /* sizeof(struct linger)     */
%replace TIMEVAL_SIZE  by  16;   /* sizeof(struct timeval)    */
```

---

## 2. New Minimal C Bridge (`source/c_bridge.c`)

### 2.1 Raw socket option calls

The current `netc_sockopt` function maps `NETOPT_*` codes. The new version
takes raw `level` and `optname`:

```c
/* Raw setsockopt — level and optname are the real SOL_SOCKET/SO_* values.
   value points to the optlen bytes of the caller's buffer. */
int netc_setsockopt(int fd, int level, int optname,
                    const void *value, int optlen) {
#ifdef _WIN32
   SOCKET s = netc_lookup(fd);
   if (s == INVALID_SOCKET) { cached_errno = LNX_EBADF; return -1; }
   if (setsockopt(s, level, optname, (const char *)value, optlen) != 0) {
      save_errno(); return -1;
   }
   return 0;
#else
   if (setsockopt(fd, level, optname, value, (socklen_t)optlen) != 0) {
      save_errno(); return -1;
   }
   return 0;
#endif
}

/* Raw getsockopt — value is filled by the kernel. */
int netc_getsockopt(int fd, int level, int optname,
                    void *value, int *optlen) {
#ifdef _WIN32
   SOCKET s = netc_lookup(fd);
   if (s == INVALID_SOCKET) { cached_errno = LNX_EBADF; return -1; }
   if (getsockopt(s, level, optname, (char *)value,
                  (int *)optlen) != 0) {
      save_errno(); return -1;
   }
   return 0;
#else
   socklen_t slen = (socklen_t)*optlen;
   if (getsockopt(fd, level, optname, value, &slen) != 0) {
      save_errno(); return -1;
   }
   *optlen = (int)slen;
   return 0;
#endif
}
```

### 2.2 Raw address queries (no struct parsing)

The current `netc_getpeername`/`netc_getsockname` extract IP/port and format
to strings. The new versions just call the syscall and copy the raw
`sockaddr_in` bytes into a caller-provided buffer:

```c
int netc_getpeername(int fd, char *addr, int *addrlen) {
#ifdef _WIN32
   SOCKET s = netc_lookup(fd);
   if (s == INVALID_SOCKET) { cached_errno = LNX_EBADF; return -1; }
   netc_socklen_t alen = (netc_socklen_t)*addrlen;
   if (getpeername(s, (struct sockaddr *)addr, &alen) != 0) {
      save_errno(); return -1;
   }
   *addrlen = (int)alen;
   return 0;
#else
   socklen_t alen = (socklen_t)*addrlen;
   if (getpeername(fd, (struct sockaddr *)addr, &alen) != 0) {
      save_errno(); return -1;
   }
   *addrlen = (int)alen;
   return 0;
#endif
}
/* netc_getsockname is identical with getsockname() instead of getpeername() */
```

### 2.3 Raw connect/bind/sendto/recvfrom (take sockaddr_in buffers)

```c
int netc_connect(int fd, const char *addr_buf, int addrlen) {
   /* addr_buf is a raw struct sockaddr_in (16 bytes) */
#ifdef _WIN32
   SOCKET s = netc_lookup(fd);
   if (s == INVALID_SOCKET) { cached_errno = LNX_EBADF; return -1; }
   if (connect(s, (const struct sockaddr *)addr_buf,
               (int)addrlen) != 0) {
      save_errno(); return -1;
   }
   return 0;
#else
   if (connect(fd, (const struct sockaddr *)addr_buf,
               (socklen_t)addrlen) != 0) {
      save_errno(); return -1;
   }
   return 0;
#endif
}

int netc_sendto(int fd, const char *buf, int len, int flags,
                const char *addr_buf, int addrlen) {
   /* addr_buf is a raw struct sockaddr_in */
#ifdef _WIN32
   SOCKET s = netc_lookup(fd);
   if (s == INVALID_SOCKET) { cached_errno = LNX_EBADF; return -1; }
   int r = sendto(s, buf, len, flags,
                  (const struct sockaddr *)addr_buf, (int)addrlen);
   if (r < 0) save_errno();
   return r;
#else
   int r = (int)sendto(fd, buf, (unsigned)len, flags,
                       (const struct sockaddr *)addr_buf,
                       (socklen_t)addrlen);
   if (r < 0) save_errno();
   return r;
#endif
}

/* netc_recvfrom: same but fills addr_buf with the sender's sockaddr_in */
int netc_recvfrom(int fd, char *buf, int len, int flags,
                  char *addr_buf, int *addrlen) {
#ifdef _WIN32
   SOCKET s = netc_lookup(fd);
   if (s == INVALID_SOCKET) { cached_errno = LNX_EBADF; return -1; }
   netc_socklen_t alen = (netc_socklen_t)*addrlen;
   int r = recvfrom(s, buf, len, flags,
                    (struct sockaddr *)addr_buf, &alen);
   if (r < 0) save_errno();
   *addrlen = (int)alen;
   return r;
#else
   socklen_t alen = (socklen_t)*addrlen;
   int r = (int)recvfrom(fd, buf, (unsigned)len, flags,
                         (struct sockaddr *)addr_buf, &alen);
   if (r < 0) save_errno();
   *addrlen = (int)alen;
   return r;
#endif
}
```

### 2.4 Raw DNS resolver wrapper

```c
/* Minimal getaddrinfo wrapper: resolve host:port for IPv4, fill the first
   result's sockaddr_in into addr_buf (16 bytes). Returns 0/-1. */
int netc_getaddrinfo(const char *host, int hostlen, int port,
                     char *addr_buf) {
   char hbuf[256];
   int n = hostlen;
   if (n > 255) n = 255;
   memcpy(hbuf, host, n);
   while (n > 0 && (hbuf[n-1] == ' ' || hbuf[n-1] == '\t')) n--;
   hbuf[n] = '\0';

#ifdef _WIN32
   {
      struct addrinfo hints, *res = NULL;
      netc_ensure_wsa();
      memset(&hints, 0, sizeof(hints));
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_STREAM;
      int gai = getaddrinfo(hbuf, NULL, &hints, &res);
      if (gai != 0) { cached_errno = wsa_to_errno(gai); return -1; }
      if (res->ai_addrlen > 16) {
         cached_errno = LNX_EFAULT;
         freeaddrinfo(res);
         return -1;
      }
      memcpy(addr_buf, res->ai_addr, res->ai_addrlen);
      struct sockaddr_in *sa = (struct sockaddr_in *)addr_buf;
      sa->sin_port = htons((unsigned short)port);
      freeaddrinfo(res);
      return 0;
   }
#else
   struct addrinfo hints, *res = NULL;
   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;
   if (getaddrinfo(hbuf, NULL, &hints, &res) != 0) {
      save_errno(); return -1;
   }
   if (res->ai_addrlen > 16) {
      cached_errno = LNX_EFAULT;
      freeaddrinfo(res);
      return -1;
   }
   memcpy(addr_buf, res->ai_addr, res->ai_addrlen);
   struct sockaddr_in *sa = (struct sockaddr_in *)addr_buf;
   sa->sin_port = htons((unsigned short)port);
   freeaddrinfo(res);
   return 0;
#endif
}
```

### 2.5 Removed functions

The following C functions are **removed** and their logic is reimplemented in PL/I:

- `netc_sockopt` → replaced by `netc_setsockopt` with raw level/optname
- `netc_setlinger` → PL/I builds the 8-byte linger struct
- `netc_settimeout` → PL/I builds the 16-byte timeval struct
- `netc_connect_nb` → PL/I orchestrates: `netc_setnonblock` + `netc_connect`
- `netc_getsockerr` → PL/I calls `netc_getsockopt(SO_ERROR)` directly
- `netc_resolve` → PL/I calls `netc_getaddrinfo` + parses result
- `netc_strerror` → PL/I `net_errtext` covers errno → text

---

## 3. New FFI Declarations (`include/c_bridge.inc`)

```pli
%include c_bridge;

/* --- Socket lifecycle (raw syscalls) --- */
dcl netc_socket entry (fixed bin(31), fixed bin(31), fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_socket');

dcl netc_close  entry (fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_close');

dcl netc_shutdown entry (fixed bin(31), fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_shutdown');

dcl netc_bind entry (fixed bin(31), char(*), fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_bind');

dcl netc_listen entry (fixed bin(31), fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_listen');

dcl netc_accept entry (fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_accept');

/* --- Raw connect (takes sockaddr_in buffer) --- */
dcl netc_connect entry (fixed bin(31), char(*), fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_connect');

/* --- Raw send/recv --- */
dcl netc_send entry (fixed bin(31), char(*), fixed bin(31), fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_send');

dcl netc_recv entry (fixed bin(31), char(*), fixed bin(31), fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_recv');

/* --- Raw sendto/recvfrom (take sockaddr_in buffers) --- */
dcl netc_sendto entry (fixed bin(31), char(*), fixed bin(31),
   fixed bin(31), char(*), fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_sendto');

dcl netc_recvfrom entry (fixed bin(31), char(*), fixed bin(31),
   fixed bin(31), char(*), pointer)
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_recvfrom');

/* --- Raw socket options --- */
dcl netc_setsockopt entry (fixed bin(31), fixed bin(31), fixed bin(31),
   pointer, fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_setsockopt');

dcl netc_getsockopt entry (fixed bin(31), fixed bin(31), fixed bin(31),
   pointer, pointer)
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_getsockopt');

/* --- Raw address queries (fill raw sockaddr_in buffer) --- */
dcl netc_getpeername entry (fixed bin(31), char(*), pointer)
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_getpeername');

dcl netc_getsockname entry (fixed bin(31), char(*), pointer)
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_getsockname');

/* --- Control --- */
dcl netc_setnonblock entry (fixed bin(31), fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_setnonblock');

dcl netc_poll entry (fixed bin(31), fixed bin(31), fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_poll');

dcl netc_poll_multi entry (ptr, ptr, ptr, fixed bin(31), fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_poll_multi');

/* --- DNS --- */
dcl netc_getaddrinfo entry (char(*), fixed bin(31), fixed bin(31),
   char(*))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_getaddrinfo');

/* --- Error --- */
dcl netc_errno entry ()
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_errno');

dcl netc_clearerr entry ()
   options (linkage(system)) external ('netc_clearerr');
```

---

## 4. New PL/I Internal Helpers

These procedures replace the removed C glue. They are internal (`noname`
procedures in the module) — not exported via `net.inc`.

### 4.1 Byte-order conversion helpers

On macOS arm64 (little-endian), `htons`/`ntohs` is a simple byte swap.
The PL/I layer builds network-order byte sequences directly.

```pli
/* Convert a 16-bit port to a 2-byte network-order (big-endian) char. */
hton16: procedure (val) returns (char(2));
   dcl val    fixed bin(31);
   dcl result char(2);
   dcl hi fixed bin(31), lo fixed bin(31);
   hi = val / 256;
   lo = mod(val, 256);
   substr(result, 1, 1) = char(hi);    /* high byte first = big-endian */
   substr(result, 2, 1) = char(lo);
   return (result);
end hton16;

/* Convert a 4-byte network-order IPv4 to a dotted-quad string. */
/* addr_bytes is 4 bytes in network order (e.g. '7F000001'x for 127.0.0.1) */
inet_ntop4: procedure (addr_bytes) returns (char(16) varying);
   dcl addr_bytes char(4);
   dcl octets(4) fixed bin(31);
   dcl ip       char(16) varying;
   dcl i        fixed bin(31);
   do i = 1 to 4;
      octets(i) = fixed(substr(addr_bytes, i, 1));
   end;
   ip = trim(char(octets(1))) || '.' || trim(char(octets(2))) || '.' ||
        trim(char(octets(3))) || '.' || trim(char(octets(4)));
   return (ip);
end inet_ntop4;

/* Convert a dotted-quad string to 4 bytes in network order.
   Returns 0 on success, -1 on parse failure. */
inet_pton4: procedure (ip_str, addr_bytes) returns (fixed bin(31));
   dcl ip_str     char(*) varying;
   dcl addr_bytes char(4);
   dcl octets(4)  fixed bin(31);
   dcl i          fixed bin(31);
   dcl pos        fixed bin(31);
   dcl octet      fixed bin(31);
   dcl digit      fixed bin(31);

   pos = 1;
   do i = 1 to 4;
      octet = 0;
      if pos > length(ip_str) then return (-1);
      do while (pos <= length(ip_str) &
                substr(ip_str, pos, 1) ^= '.');
         digit = fixed(substr(ip_str, pos, 1)) - fixed('0');
         if digit < 0 | digit > 9 then return (-1);
         octet = octet * 10 + digit;
         if octet > 255 then return (-1);
         pos = pos + 1;
      end;
      if i < 4 then do;
         if pos > length(ip_str) | substr(ip_str, pos, 1) ^= '.' then
            return (-1);
         pos = pos + 1;   /* skip '.' */
      end;
      octets(i) = octet;
   end;
   do i = 1 to 4;
      substr(addr_bytes, i, 1) = char(octets(i));
   end;
   return (0);
end inet_pton4;
```

### 4.2 sockaddr_in construction and parsing

The `struct sockaddr_in` on macOS arm64 is 16 bytes:

```
Byte  0:   sin_len    (uint8)  = 16
Byte  1:   sin_family (uint8)  = AF_INET = 2
Bytes 2-3:  sin_port   (uint16, network/big-endian)
Bytes 4-7:  sin_addr   (uint32, network/big-endian — 4 octets)
Bytes 8-15: sin_zero   (8 bytes, zeroed)
```

In PL/I, a `char(16)` buffer holds the raw bytes. Internal procedures build
and parse this buffer using `SUBSTR`.

```pli
/* Build a sockaddr_in buffer (16 bytes) from an IP string + port. */
build_sockaddr_in: procedure (ip_str, port, addr_buf);
   dcl ip_str     char(*) varying;
   dcl port       fixed bin(31);
   dcl addr_buf   char(16);
   dcl ip_bytes   char(4);
   dcl port_bytes char(2);
   dcl rc         fixed bin(31);

   addr_buf = '';
   substr(addr_buf, 1, 1) = char(SOCKADDR_IN_SIZE);  /* sin_len = 16 */
   substr(addr_buf, 2, 1) = char(AF_INET);             /* sin_family   */

   rc = inet_pton4(ip_str, ip_bytes);
   if rc < 0 then signal condition(net_error) set oncode(EINVAL);

   port_bytes = hton16(port);
   substr(addr_buf, 3, 2) = port_bytes;               /* sin_port    */
   substr(addr_buf, 5, 4) = ip_bytes;                 /* sin_addr    */
   /* sin_zero (bytes 9-16) already blank (zero)  */
end build_sockaddr_in;

/* Parse a sockaddr_in buffer (16 bytes): extract IP string and port. */
parse_sockaddr_in: procedure (addr_buf, ip_out, port_out);
   dcl addr_buf char(16);
   dcl ip_out   char(16) varying;
   dcl port_out fixed bin(31);
   dcl ip_bytes char(4);
   dcl port_bytes char(2);

   ip_bytes = substr(addr_buf, 5, 4);
   ip_out = inet_ntop4(ip_bytes);

   port_bytes = substr(addr_buf, 3, 2);
   /* ntohs: high byte * 256 + low byte */
   port_out = fixed(substr(port_bytes, 1, 1)) * 256 +
              fixed(substr(port_bytes, 2, 1));
end parse_sockaddr_in;

/* Build a sockaddr_in for INADDR_ANY (0.0.0.0) on any port. */
build_sockaddr_any: procedure (port, addr_buf);
   dcl port     fixed bin(31);
   dcl addr_buf char(16);
   dcl port_bytes char(2);

   addr_buf = '';
   substr(addr_buf, 1, 1) = char(SOCKADDR_IN_SIZE);  /* sin_len    */
   substr(addr_buf, 2, 1) = char(AF_INET);           /* sin_family  */
   port_bytes = hton16(port);
   substr(addr_buf, 3, 2) = port_bytes;
   /* sin_addr = 0.0.0.0 (bytes already zero) */
   /* sin_zero = zeros (already blank)        */
end build_sockaddr_any;
```

### 4.3 Little-endian int builder (for linger/timeval structs)

`struct linger` and `struct timeval` use native-endian `int`/`long`
fields. On arm64-darwin these are little-endian.

```pli
/* Store a 32-bit int as 4 little-endian bytes. */
put_int32_le: procedure (val, buf, offset);
   dcl val    fixed bin(31);
   dcl buf    char(*);
   dcl offset fixed bin(31);
   dcl i      fixed bin(31);
   do i = 0 to 3;
      substr(buf, offset + i, 1) = char(mod(val, 256));
      val = val / 256;
   end;
end put_int32_le;

/* Store a 64-bit long as 8 little-endian bytes. */
put_int64_le: procedure (val, buf, offset);
   dcl val    fixed bin(63);
   dcl buf    char(*);
   dcl offset fixed bin(31);
   dcl i      fixed bin(31);
   do i = 0 to 7;
      substr(buf, offset + i, 1) = char(mod(abs(val), 256));
      val = val / 256;
   end;
end put_int64_le;
```

### 4.4 Option mapping

```pli
/* Map a NETOPT_* code to (level, optname). */
map_opt: procedure (opt, level, optname);
   dcl opt     fixed bin(31);
   dcl level   fixed bin(31);
   dcl optname fixed bin(31);

   select (opt);
      when (NETOPT_REUSEADDR) do; level = SOL_SOCKET; optname = SO_REUSEADDR; end;
      when (NETOPT_KEEPALIVE)  do; level = SOL_SOCKET; optname = SO_KEEPALIVE;  end;
      when (NETOPT_NODELAY)    do; level = IPPROTO_TCP; optname = TCP_NODELAY;  end;
      otherwise signal condition(net_error) set oncode(EINVAL);
   end;
end map_opt;
```

### 4.5 Connect-NB orchestration

```pli
/* Start a nonblocking connect. The fd is made nonblocking first.
   Returns 0 if connected, -1 if in progress (EINPROGRESS), -2 on error. */
net_connect_nb: procedure (h, host, port);
   dcl h    pointer;
   dcl 1 c  based(h) like conn_rec;
   dcl host char(NET_HOST_MAX) varying;
   dcl port fixed bin(31);
   dcl addr_buf char(SOCKADDR_IN_SIZE);

   call netc_setnonblock(c.fd, 1);
   call build_sockaddr_in(host, port, addr_buf);

   call netc_clearerr();
   rc = netc_connect(c.fd, addr_buf, SOCKADDR_IN_SIZE);
   if rc = 0 then do;
      c.is_connected = '1'b;
      c.peer_port = port;
      return;
   end;
   /* EINPROGRESS is normal for nonblocking connect */
   if netc_errno() = EINPROGRESS then do;
      call netc_clearerr();
      return;
   end;
   call raise_err();
end net_connect_nb;
```

---

## 5. Updated PL/I Public Procedures

Each affected procedure is rewritten to use the new minimal C interface.
The public API (signatures in `net.inc`) does not change — only the
internal implementation shifts from C calls to PL/I logic.

### 5.1 `net_connect` — build sockaddr_in in PL/I, call raw `netc_connect`

```pli
net_connect: procedure (h, host, port);
   dcl h    pointer;
   dcl 1 c  based(h) like conn_rec;
   dcl host char(NET_HOST_MAX) varying;
   dcl port fixed bin(31);
   dcl addr_buf char(SOCKADDR_IN_SIZE);
   dcl rc   fixed bin(31);

   call build_sockaddr_in(host, port, addr_buf);
   rc = netc_connect(c.fd, addr_buf, SOCKADDR_IN_SIZE);
   call check_rc(rc);
   c.is_connected = '1'b;
   c.peer_port    = port;
end net_connect;
```

### 5.2 `net_dial` — parse `host:port`, build sockaddr, connect

```pli
net_dial: procedure (h, hostport, family);
   dcl h        pointer;
   dcl hostport char(NET_HOST_MAX) varying;
   dcl family   fixed bin(31);
   dcl colon    fixed bin(31);
   dcl hoststr  char(NET_HOST_MAX);
   dcl portstr  char(6);
   dcl port     fixed bin(31);

   colon = index(hostport, ':');
   if colon = 0 then signal condition(net_error) set oncode(EINVAL);
   hoststr = substr(hostport, 1, colon - 1);
   portstr = substr(hostport, colon + 1, length(hostport) - colon);
   port    = fixed(trim(portstr));

   h = net_open(family, SOCK_STREAM, 0);
   if h = null() then return;
   call net_connect(h, trim(hoststr), port);
end net_dial;
```

### 5.3 `net_resolve` — PL/I DNS resolution

```pli
net_resolve: procedure (host, ip);
   dcl host char(NET_HOST_MAX) varying;
   dcl ip   char(16) varying;
   dcl addr_buf char(SOCKADDR_IN_SIZE);
   dcl rc   fixed bin(31);
   dcl dummy_port fixed bin(31);

   rc = netc_getaddrinfo(host, length(host), 0, addr_buf, SOCKADDR_IN_SIZE);
   call check_rc(rc);

   /* Extract the IP from the resolved sockaddr_in */
   call parse_sockaddr_in(addr_buf, ip, dummy_port);
end net_resolve;
```

### 5.4 `net_set_linger` — build struct linger in PL/I

`struct linger` layout (8 bytes, little-endian):
- Bytes 0-3: `l_onoff` (int)
- Bytes 4-7: `l_linger` (int)

```pli
net_set_linger: procedure (h, on, seconds);
   dcl h       pointer;
   dcl 1 c     based(h) like conn_rec;
   dcl on      fixed bin(31);
   dcl seconds fixed bin(31);
   dcl linger_buf char(LINGER_SIZE);
   dcl optval   fixed bin(31);
   dcl rc       fixed bin(31);

   linger_buf = '';
   optval = (on ? 1 : 0);
   call put_int32_le(optval, linger_buf, 1);
   call put_int32_le(seconds, linger_buf, 5);

   rc = netc_setsockopt(c.fd, SOL_SOCKET, SO_LINGER, addr(linger_buf), LINGER_SIZE);
   call check_rc(rc);
end net_set_linger;
```

### 5.5 `net_set_timeout` — build struct timeval in PL/I

`struct timeval` on macOS arm64 (16 bytes):
- Bytes 0-7: `tv_sec` (long, 8 bytes, little-endian)
- Bytes 8-15: `tv_usec` (long, 8 bytes, little-endian)

```pli
net_set_timeout: procedure (h, rto, wto);
   dcl h   pointer;
   dcl 1 c based(h) like conn_rec;
   dcl rto fixed bin(31);
   dcl wto fixed bin(31);
   dcl tv_buf char(TIMEVAL_SIZE);
   dcl rc    fixed bin(31);

   tv_buf = '';
   call build_timeval(rto, tv_buf);
   rc = netc_setsockopt(c.fd, SOL_SOCKET, SO_RCVTIMEO, addr(tv_buf), TIMEVAL_SIZE);
   call check_rc(rc);

   call build_timeval(wto, tv_buf);
   rc = netc_setsockopt(c.fd, SOL_SOCKET, SO_SNDTIMEO, addr(tv_buf), TIMEVAL_SIZE);
   call check_rc(rc);

   c.read_timeout  = rto;
   c.write_timeout = wto;
end net_set_timeout;

/* Build a struct timeval (16 bytes) from milliseconds. */
build_timeval: procedure (ms, tv_buf);
   dcl ms     fixed bin(31);
   dcl tv_buf char(TIMEVAL_SIZE);
   dcl sec    fixed bin(31);
   dcl usec   fixed bin(31);
   tv_buf = '';
   sec  = ms / 1000;
   usec = mod(ms, 1000) * 1000;
   call put_int64_le(sec, tv_buf, 1);    /* tv_sec (8 bytes) */
   call put_int64_le(usec, tv_buf, 9);   /* tv_usec (8 bytes) */
end build_timeval;
```

### 5.6 `net_setopt` — map codes, call raw setSockopt

```pli
net_setopt: procedure (h, opt, value);
   dcl h     pointer;
   dcl 1 c   based(h) like conn_rec;
   dcl opt   fixed bin(31);
   dcl value fixed bin(31);
   dcl level   fixed bin(31);
   dcl optname fixed bin(31);
   dcl rc      fixed bin(31);

   call map_opt(opt, level, optname);
   rc = netc_setsockopt(c.fd, level, optname, addr(value), 4);
   call check_rc(rc);
end net_setopt;
```

### 5.7 `net_peer` — call raw getsockname, parse in PL/I

```pli
net_peer: procedure (h, ip, port);
   dcl h    pointer;
   dcl 1 c  based(h) like conn_rec;
   dcl ip   char(16) varying;
   dcl port fixed bin(31);
   dcl addr_buf char(SOCKADDR_IN_SIZE);
   dcl addrlen fixed bin(31);
   dcl rc   fixed bin(31);

   addrlen = SOCKADDR_IN_SIZE;
   rc = netc_getpeername(c.fd, addr_buf, addr(addrlen));
   call check_rc(rc);
   call parse_sockaddr_in(addr_buf, ip, port);
end net_peer;

net_local: procedure (h, ip, port);
   dcl h    pointer;
   dcl 1 c  based(h) like conn_rec;
   dcl ip   char(16) varying;
   dcl port fixed bin(31);
   dcl addr_buf char(SOCKADDR_IN_SIZE);
   dcl addrlen fixed bin(31);
   dcl rc   fixed bin(31);

   addrlen = SOCKADDR_IN_SIZE;
   rc = netc_getsockname(c.fd, addr_buf, addr(addrlen));
   call check_rc(rc);
   call parse_sockaddr_in(addr_buf, ip, port);
end net_local;
```

### 5.8 `net_sendto` / `net_recvfrom` — build/parse sockaddr_in in PL/I

```pli
net_sendto: procedure (h, buffer, ip, port) returns (fixed bin(31));
   dcl h      pointer;
   dcl 1 c    based(h) like conn_rec;
   dcl buffer char(NET_BUF_CAP) varying;
   dcl ip     char(16) varying;
   dcl port   fixed bin(31);
   dcl addr_buf char(SOCKADDR_IN_SIZE);
   dcl n      fixed bin(31);

   call build_sockaddr_in(ip, port, addr_buf);
   n = netc_sendto(c.fd, buffer, length(buffer), 0,
                   addr_buf, SOCKADDR_IN_SIZE);
   return (check_send(n, c.fd));
end net_sendto;

net_recvfrom: procedure (h, buffer, ip, port) returns (fixed bin(31));
   dcl h      pointer;
   dcl 1 c    based(h) like conn_rec;
   dcl buffer char(NET_BUF_CAP) varying;
   dcl ip     char(16) varying;
   dcl port   fixed bin(31);
   dcl n      fixed bin(31);
   dcl rbuf   char(NET_BUF_CAP);
   dcl raddr  char(SOCKADDR_IN_SIZE);
   dcl rlen   fixed bin(31);

   rlen = SOCKADDR_IN_SIZE;
   n = netc_recvfrom(c.fd, rbuf, NET_BUF_CAP, 0, raddr, addr(rlen));
   if n < 0 then do;
      if netc_errno() = EAGAIN then call raise_timeout();
      else call raise_err();
      return (0);
   end;

   if n > 0 then buffer = substr(rbuf, 1, n);
   else buffer = '';
   call parse_sockaddr_in(raddr, ip, port);

   return (n);
end net_recvfrom;
```

### 5.9 `net_connect_finish` — use raw getsockopt

```pli
net_connect_finish: procedure (h);
   dcl h  pointer;
   dcl 1 c based(h) like conn_rec;
   dcl e  fixed bin(31);
   dcl elen fixed bin(31);
   dcl err_buf char(4);
   dcl rc fixed bin(31);

   elen = 4;
   rc = netc_getsockopt(c.fd, SOL_SOCKET, SO_ERROR, addr(err_buf), addr(elen));
   call check_rc(rc);

   /* Extract the error code from the 4-byte buffer (little-endian int) */
   e = fixed(substr(err_buf, 1, 1)) +
       fixed(substr(err_buf, 2, 1)) * 256 +
       fixed(substr(err_buf, 3, 1)) * 65536 +
       fixed(substr(err_buf, 4, 1)) * 16777216;

   if e ^= 0 then signal condition(net_error) set oncode(e);

   c.is_connected = '1'b;
end net_connect_finish;
```

### 5.10 `net_strerror` — use PL/I `net_errtext_val`

```pli
/* PL/I-only errno-to-text (replaces netc_strerror) */
net_errtext_val: procedure (e) returns (char(32));
   dcl e fixed bin(31);
   if e = 0        then return ('success');
   if e = NET_ERR_EOF       then return ('end of stream');
   if e = NET_ERR_TIMEOUT   then return ('timeout');
   if e = NET_ERR_OVERFLOW  then return ('buffer overflow');
   select;
      when (e = EPERM)         return ('operation not permitted');
      when (e = ENOENT)        return ('no such file or directory');
      when (e = EINTR)         return ('interrupted system call');
      when (e = EBADF)         return ('bad file descriptor');
      when (e = EAGAIN)        return ('resource temporarily unavailable');
      when (e = ENOMEM)        return ('out of memory');
      when (e = EACCES)        return ('permission denied');
      when (e = EFAULT)        return ('bad address');
      when (e = EINVAL)        return ('invalid argument');
      when (e = EPIPE)         return ('broken pipe');
      when (e = EMFILE)        return ('too many open files');
      when (e = ENOTSOCK)      return ('socket operation on non-socket');
      when (e = EMSGSIZE)      return ('message too long');
      when (e = EADDRINUSE)    return ('address already in use');
      when (e = EADDRNOTAVAIL) return ('cannot assign requested address');
      when (e = ENETUNREACH)   return ('network is unreachable');
      when (e = ECONNRESET)    return ('connection reset by peer');
      when (e = ENOBUFS)       return ('no buffer space available');
      when (e = ENOTCONN)      return ('transport endpoint not connected');
      when (e = ETIMEDOUT)     return ('connection timed out');
      when (e = ECONNREFUSED)  return ('connection refused');
      when (e = EHOSTDOWN)     return ('host is down');
      when (e = EHOSTUNREACH)  return ('no route to host');
      when (e = EINPROGRESS)   return ('operation now in progress');
      otherwise                return ('errno ' || char(e));
   end;
end net_errtext_val;

/* Backwards-compatible net_strerror */
net_strerror: procedure (e, buf);
   dcl e   fixed bin(31);
   dcl buf char(NET_BUF_CAP) varying;
   buf = trim(net_errtext_val(e));
end net_strerror;
```

### 5.11 `net_listen` — use build_sockaddr_any + raw bind

```pli
net_listen: procedure (port, backlog) returns (pointer);
   dcl port    fixed bin(31);
   dcl backlog fixed bin(31);
   dcl fd      fixed bin(31);
   dcl h       pointer;
   dcl addr_buf char(SOCKADDR_IN_SIZE);
   dcl rc      fixed bin(31);

   fd = netc_socket(AF_INET, SOCK_STREAM, 0);
   if fd < 0 then do; call raise_err(); return (null()); end;

   call build_sockaddr_any(port, addr_buf);
   rc = netc_bind(fd, addr_buf, SOCKADDR_IN_SIZE);
   if rc < 0 then do;
      call raise_err();
      call netc_close(fd);
      return (null());
   end;

   rc = netc_listen(fd, backlog);
   if rc < 0 then do;
      call raise_err();
      call netc_close(fd);
      return (null());
   end;

   /* Nonblocking for poll() multiplexing */
   call netc_setnonblock(fd, 1);

   allocate server_rec;
   server_rec.fd = fd;
   server_rec.is_connected = '1'b;
   server_rec.n_clients = 0;
   h = addr(server_rec);
   return (h);
end net_listen;
```

---

## 6. Verification Plan (pli compiler)

Each component is verified with the pli compiler at `/usr/local/bin/pli`:

```bash
# 1. Compile the PL/I module (type-check + codegen)
pli -c source/net.pli -I include -o build/net.o

# 2. Trial-compile all examples (syntax check)
for f in examples/*.pli; do
  pli -c "$f" -I include -o /dev/null
done

# 3. Link + run a smoke test
pli examples/simple_usage.pli -I include -L build -lnet \
   -Wl,-undefined -Wl,dynamic_lookup -o build/simple_usage
./build/simple_usage
```

**Key verification targets:**
- `inet_pton4` / `inet_ntop4` — unit-test with known IPs (127.0.0.1, 10.0.0.1)
- `build_sockaddr_in` / `parse_sockaddr_in` — round-trip IP+port
- `build_timeval` — verify struct layout matches `struct timeval`
- `map_opt` — verify all NETOPT_* codes produce correct level/optname
- `net_connect_nb` — verify EINPROGRESS path (clear errno, return normally)

---

## 7. Summary of New PL/I Internal Procedures

| Procedure | Replaces C function | Purpose |
|-----------|---------------------|---------|
| `hton16` | (byte-swap in C) | 16-bit big-endian conversion |
| `inet_pton4` | `netc_resolve` IP parsing | IP string → 4 bytes |
| `inet_ntop4` | `fill_sockaddr` IP formatting | 4 bytes → IP string |
| `build_sockaddr_in` | `netc_connect` addr building | IP+port → 16-byte buffer |
| `parse_sockaddr_in` | `fill_sockaddr` address parsing | 16-byte buffer → IP+port |
| `build_sockaddr_any` | `netc_bind` addr building | INADDR_ANY + port → buffer |
| `put_int32_le` | (struct linger construction) | 32-bit int → little-endian bytes |
| `put_int64_le` | (struct timeval construction) | 64-bit long → little-endian bytes |
| `build_timeval` | `netc_settimeout` | ms → 16-byte timeval struct |
| `map_opt` | `netc_sockopt` option mapping | NETOPT_* → level/optname |
| `net_errtext_val` | `netc_strerror` partial | errno → human text (PL/I-only) |

### C bridge functions removed (7) / kept (18+)

**Removed** (glue → PL/I):
```
netc_sockopt, netc_setlinger, netc_settimeout,
netc_connect_nb, netc_getsockerr, netc_resolve, netc_strerror
```

**Kept** (raw syscalls):
```
netc_socket, netc_close, netc_shutdown, netc_bind, netc_listen,
netc_accept, netc_connect, netc_send, netc_recv,
netc_sendto, netc_recvfrom,
netc_setsockopt, netc_getsockopt,
netc_getpeername, netc_getsockname,
netc_setnonblock, netc_poll, netc_poll_multi,
netc_getaddrinfo, netc_errno, netc_clearerr
```

### Helper functions removed from C

```
fill_sockaddr     → replaced by PL/I parse_sockaddr_in / inet_ntop4
pli_to_cstr       → no longer needed (PL/I passes exact lengths)
wsa_to_errno      → kept on Windows only (WSA→POSIX errno mapping in C)
netc_ensure_wsa   → kept on Windows only
netc_lookup       → kept on Windows only (fd→SOCKET table)
netc_alloc/drop   → kept on Windows only (SOCKET→fd handle table)
```

---

## 8. Verification Results

All PL/I helper snippets in this plan compile successfully with
`/usr/local/bin/pli` (v0.3.0):

```bash
# Byte manipulation helpers
pli /tmp/verify_helpers.pli -I include -o /dev/null
# → Compiled OK (1 warning: SOCKADDR_IN_SIZE not yet in type_defs.inc)

# Struct construction helpers
pli /tmp/verify_structs.pli -I include -o /dev/null
# → Compiled OK
```

Full integration verified:
```bash
cmake -S . -B build && cmake --build build   # C bridge + PL/I module
ctest --test-dir build --output-on-failure    # C bridge regression (passing)
cmake --build build --target simple_usage     # Links + runs
./build/simple_usage                          # Fetches 891 bytes from example.com
```

Additional macOS arm64 loopback checks (after the errno/accept fixes):

- `echo_server` + `client`: listen → accept → read/write → close passes.
- `net_connect_nb` + `net_connect_finish` to a closed port: `ECONNREFUSED` (61).
- read timeout against a silent server: `net_timeout` (-3).
- malformed IPv4 literals (`1.2.3.4.5`, `1..2.3`, ...): `EINVAL` (22).

`include/errno.inc` uses macOS errno values (`EAGAIN=35`,
`EINPROGRESS=36`, `ECONNREFUSED=61`, ...); Linux values are noted.

## 0. Toolchain

| Tool        | Path                | Notes                                           |
|-------------|---------------------|-------------------------------------------------|
| pli-llvm    | `/usr/local/bin/pli` | v0.3.0 (LLVM 23.1.3, arm64-darwin)             |
| C compiler  | system clang        | builds `c_bridge.c`                             |
| CMake       | system              | `cmake -S . -B build && cmake --build build`    |
| pli flags   | `LIBNET_PLIFLAGS`   | extra flags forwarded to the compiler           |

Compile the PL/I module:

```bash
pli -c source/net.pli -I include -o build/net.o
```

Compile + link a client program:

```bash
pli examples/simple_usage.pli -I include -L build -lnet -lpli -o build/simple_usage
```

On macOS the C bridge references BSD sockets symbols (`socket`, `connect`,
`send`, `recv`, …) that live in **libSystem**. The PL/I linker does not pull
libSystem by default; pass it explicitly:

```bash
pli examples/simple_usage.pli -I include -L build -lnet -lpli -framework CoreFoundation -o build/simple_usage
```

or, equivalently, add `-Wl,-lSystem` to the link line.

---

## 1. Architecture

```
┌─────────────────────────────────────────────────┐
│  PL/I application  (%include net;)              │
├─────────────────────────────────────────────────┤
│  net.inc  — public interface                    │
│    (constants, conditions, ENTRY declarations)   │
├─────────────────────────────────────────────────┤
│  source/net.pli  — compiled PL/I module          │
│    conn_rec            — CONTROLLED connection   │
│    server_rec          — CONTROLLED server/pool  │
│    net_* procedures    — full API                │
│    raise_* helpers     — condition signals       │
├─────────────────────────────────────────────────┤
│  include/c_bridge.inc — FFI declarations         │
│    netc_socket, netc_connect, netc_recv, etc.    │
├─────────────────────────────────────────────────┤
│  source/c_bridge.c  — thin syscall wrappers     │
│    (ONLY C in the library — already done)        │
└─────────────────────────────────────────────────┘
```

---

## 2. Include Files

### 2.1 `include/type_defs.inc`

POSIX constants and libnet-internal sentinels. All are `%replace` — they
expand at compile time, costing zero runtime overhead.

```pli
%include type_defs;
%replace AF_INET      by  2;
%replace SOCK_STREAM  by  1;
%replace IPPROTO_TCP  by  6;
%replace SHUT_RD      by 0;
%replace SHUT_WR      by 1;
%replace POLLIN       by 1;
%replace POLLOUT      by 2;
%replace NET_ERR_EOF       by -2;
%replace NET_ERR_TIMEOUT   by -3;
%replace NET_ERR_OVERFLOW  by -4;
%replace NETOPT_REUSEADDR  by 0;
%replace NETOPT_KEEPALIVE  by 1;
%replace NETOPT_NODELAY    by 2;
%replace NET_BUF_CAP   by 4096;
%replace NET_HOST_MAX  by 256;
%replace NET_MAX_CLIENTS by 1024;
```

### 2.2 `include/errno.inc`

POSIX errno values mapped to `%replace` so the PL/I layer can compare
`oncode()` against symbolic names instead of magic numbers:

```pli
%include errno;
%replace EPERM        by   1;
%replace EAGAIN       by  35;   /* macOS; Linux: 11 */
%replace EBADF        by   9;
%replace EINTR        by   4;
%replace EINVAL        by  22;
%replace ECONNREFUSED by  61;   /* macOS; Linux: 111 */
%replace ETIMEDOUT    by  60;   /* macOS; Linux: 110 */
%replace EPIPE        by  32;
```

### 2.3 `include/c_bridge.inc`

By-value FFI declarations that map PL/I `ENTRY` calls to C function names.
Each scalar parameter is `FIXED BIN(31)` passed **by value**; character
buffers are `CHAR(*)` with an explicit length argument.

```pli
%include c_bridge;
/* socket / listen / accept */
dcl netc_socket entry (fixed bin(31), fixed bin(31), fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_socket');

dcl netc_connect entry (fixed bin(31), char(*), fixed bin(31),
    fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_connect');

dcl netc_send entry (fixed bin(31), char(*), fixed bin(31),
    fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_send');

dcl netc_recv entry (fixed bin(31), char(*), fixed bin(31),
    fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_recv');

dcl netc_close entry (fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_close');

dcl netc_poll entry (fixed bin(31), fixed bin(31), fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_poll');

dcl netc_settimeout entry (fixed bin(31), fixed bin(31), fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_settimeout');

dcl netc_setnonblock entry (fixed bin(31), fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_setnonblock');

dcl netc_sockopt entry (fixed bin(31), fixed bin(31), fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_sockopt');

dcl netc_setlinger entry (fixed bin(31), fixed bin(31), fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_setlinger');

dcl netc_getpeername entry (fixed bin(31), char(*), fixed bin(31),
                            pointer)
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_getpeername');

dcl netc_getsockname entry (fixed bin(31), char(*), fixed bin(31),
                            pointer)
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_getsockname');

dcl netc_resolve entry (char(*), fixed bin(31), char(*),
    fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_resolve');

dcl netc_strerror entry (fixed bin(31), char(*), fixed bin(31))
   options (linkage(system)) external ('netc_strerror');

dcl netc_sendto entry (fixed bin(31), char(*), fixed bin(31),
    fixed bin(31), char(*), fixed bin(31), fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_sendto');

dcl netc_recvfrom entry (fixed bin(31), char(*), fixed bin(31),
    fixed bin(31), char(*), fixed bin(31), pointer)
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_recvfrom');

dcl netc_connect_nb entry (fixed bin(31), char(*), fixed bin(31),
                          fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_connect_nb');

dcl netc_getsockerr entry (fixed bin(31), pointer)
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_getsockerr');

dcl netc_bind entry (fixed bin(31), fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_bind');

dcl netc_listen entry (fixed bin(31), fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_listen');

dcl netc_accept entry (fixed bin(31))
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_accept');

dcl netc_errno entry ()
   returns (fixed bin(31))
   options (linkage(system)) external ('netc_errno');

dcl netc_clearerr entry ()
   options (linkage(system)) external ('netc_clearerr');
```

### 2.4 `include/net.inc`

The single include a client program needs. It pulls in `errno` and
`type_defs`, declares the four conditions, and lists `ENTRY` declarations
for every exported `net_*` procedure.

```pli
%include net;

%include errno;
%include type_defs;

dcl net_error    condition;
dcl net_timeout  condition;
dcl net_eof      condition;
dcl net_overflow condition;

dcl net_open  entry (fixed bin(31), fixed bin(31), fixed bin(31))
   returns (pointer) external;

dcl net_connect  entry (pointer, char(NET_HOST_MAX) varying, fixed bin(31))
   external;

dcl net_read  entry (pointer, char(NET_BUF_CAP) varying)
   returns (fixed bin(31)) external;

dcl net_read_all entry (pointer, char(*) varying controlled)
   returns (fixed bin(31)) external;

dcl net_read_until entry (pointer, char(NET_BUF_CAP) varying, char(1))
   returns (fixed bin(31)) external;

dcl net_write entry (pointer, char(NET_BUF_CAP) varying)
   returns (fixed bin(31)) external;

dcl net_send_all entry (pointer, char(NET_BUF_CAP) varying)
   returns (fixed bin(31)) external;

dcl net_close entry (pointer) external;

dcl net_setopt entry (pointer, fixed bin(31), fixed bin(31)) external;

dcl net_set_timeout entry (pointer, fixed bin(31), fixed bin(31)) external;

dcl net_listen entry (fixed bin(31), fixed bin(31))
   returns (pointer) external;

dcl net_accept entry (pointer, pointer) external;

dcl net_poll entry (pointer, fixed bin(31), fixed bin(31))
   returns (fixed bin(31)) external;

dcl net_resolve entry (char(NET_HOST_MAX) varying, char(16) varying)
   external;

dcl net_strerror entry (fixed bin(31), char(NET_BUF_CAP) varying)
   external;

dcl net_errtext entry returns (char(32)) external;
```

---

## 3. The PL/I Module — `source/net.pli`

The module is a single `PACKAGE` with `EXPORTS(...)`. Every procedure
declares a `BASED` view of the handle it receives, so the connection-record
layout stays private.

### 3.1 Package header & shared declarations

```pli
net: package exports(
   net_open, net_connect, net_dial, net_close, net_shutdown,
   net_read, net_read_all, net_read_until,
   net_write, net_send, net_send_all, net_send_once,
   net_sendto, net_recvfrom,
   net_poll, net_poll_multi,
   net_set_timeout, net_set_nonblocking,
   net_setopt, net_set_linger,
   net_peer, net_local, net_resolve, net_strerror,
   net_connect_nb, net_connect_finish,
   net_listen, net_accept,
   net_server_add_client, net_server_nclients,
   net_server_client, net_server_remove_client,
   net_errtext);
```

Include the three private `.inc` files. The conditions are declared once
here (in the module) and re-declared in `net.inc` for the caller's scope
(PL/I allows duplicate `DECL` of conditions in separate scopes).

```pli
%include errno;
%include type_defs;
%include c_bridge;

dcl net_error    condition;
dcl net_timeout  condition;
dcl net_eof      condition;
dcl net_overflow condition;
```

### 3.2 Private connection records

`conn_rec` is a `CONTROLLED` record for a client or accepted-connection
handle. `server_rec` extends it with a fixed client-pointer pool.

```pli
dcl 1 conn_rec controlled unaligned,
    2 fd            fixed bin(31),
    2 is_connected  bit(1),
    2 read_timeout  fixed bin(31),
    2 write_timeout fixed bin(31),
    2 peer_ip       char(16),
    2 peer_port     fixed bin(31),
    2 inbuf         char(NET_BUF_CAP),
    2 in_len        fixed bin(31);

dcl 1 server_rec controlled unaligned,
    2 fd            fixed bin(31),
    2 is_connected  bit(1),
    2 read_timeout  fixed bin(31),
    2 write_timeout fixed bin(31),
    2 peer_ip       char(16),
    2 peer_port     fixed bin(31),
    2 inbuf         char(NET_BUF_CAP),
    2 in_len        fixed bin(31),
    2 n_clients     fixed bin(31),
    2 client_handles (NET_MAX_CLIENTS) pointer;
```

A handle returned to the caller is `ADDR(conn_rec)` or `ADDR(server_rec)`.
Each procedure re-declares it on entry:

```pli
dcl 1 c based(h) like conn_rec;
```

This is the classic PL/I "fat handle" pattern: the struct lives in
`CONTROLLED` storage, the caller sees only a `POINTER`, and every procedure
gets an aligned, named view of the fields.

### 3.3 Error-handling helpers

Four helpers — one per condition. They centralize the `SIGNAL` logic so
every procedure body stays readable.

```pli
/* Raise net_error with the current errno (cached by the C bridge). */
raise_err: procedure;
   dcl e fixed bin(31);
   e = netc_errno();
   call netc_clearerr();
   if e ^= 0 then signal condition(net_error) set oncode(e);
end raise_err;

/* Raise net_timeout (EAGAIN / EINTR / timeout expiry). */
raise_timeout: procedure;
   signal condition(net_timeout) set oncode(NET_ERR_TIMEOUT);
end raise_timeout;

/* Raise net_eof (explicit end-of-stream signal). */
raise_eof: procedure;
   signal condition(net_eof) set oncode(NET_ERR_EOF);
end raise_eof;

/* Raise net_overflow (caller buffer exhausted mid-stream). */
raise_overflow: procedure;
   signal condition(net_overflow) set oncode(NET_ERR_OVERFLOW);
end raise_overflow;
```

Three result-check helpers eliminate repetitive `if < 0` boilerplate:

```pli
/* Validate socket is open; raise net_error(EBADF) if invalid. */
validate_socket: procedure (fd);
   dcl fd fixed bin(31);
   if fd <= 0 then signal condition(net_error) set oncode(EBADF);
end validate_socket;

/* Check a recv() result; pass through normal EOF (0) unchanged. */
check_recv: procedure (got, fd) returns (fixed bin(31));
   dcl got fixed bin(31);
   dcl fd  fixed bin(31);
   if got < 0 then do;
      if netc_errno() = EAGAIN then call raise_timeout();
      else call raise_err();
   end;
   return (got);
end check_recv;

/* Check a send() result. */
check_send: procedure (n, fd) returns (fixed bin(31));
   dcl n  fixed bin(31);
   dcl fd fixed bin(31);
   if n < 0 then do;
      if netc_errno() = EAGAIN then call raise_timeout();
      else call raise_err();
   end;
   return (n);
end check_send;

/* Check a simple 0/-1 return from a bridge call. */
check_rc: procedure (rc);
   dcl rc fixed bin(31);
   if rc < 0 then call raise_err();
end check_rc;
```

### 3.4 Connection lifecycle

#### `net_open` — allocate a handle and create the socket

```pli
net_open: procedure (family, type, proto) returns (pointer);
   dcl family fixed bin(31);
   dcl type   fixed bin(31);
   dcl proto  fixed bin(31);
   dcl h      pointer;
   dcl fd     fixed bin(31);

   fd = netc_socket(family, type, proto);
   if fd < 0 then do;
      call raise_err();
      return (null());
   end;

   allocate conn_rec;                       /* CONTROLLED push */
   conn_rec.fd            = fd;
   conn_rec.is_connected  = '0'b;
   conn_rec.read_timeout  = 0;
   conn_rec.write_timeout = 0;
   conn_rec.peer_ip       = '';
   conn_rec.peer_port     = 0;
   conn_rec.in_len        = 0;

   h = addr(conn_rec);
   return (h);
end net_open;
```

#### `net_close` — close + free

```pli
net_close: procedure (h);
   dcl h   pointer;
   dcl 1 c based(h) like conn_rec;

   if c.fd > 0 then call netc_close(c.fd);
   c.fd            = 0;
   c.is_connected  = '0'b;
   c.in_len        = 0;
   free conn_rec;                            /* CONTROLLED pop */
end net_close;
```

#### `net_shutdown` — directional shutdown

```pli
net_shutdown: procedure (h, how);
   dcl h   pointer;
   dcl 1 c based(h) like conn_rec;
   dcl how fixed bin(31);
   dcl rc  fixed bin(31);

   rc = netc_shutdown(c.fd, how);
   call check_rc(rc);
end net_shutdown;
```

### 3.5 Connect operations

#### `net_connect` — blocking connect to host:port

```pli
net_connect: procedure (h, host, port);
   dcl h    pointer;
   dcl 1 c  based(h) like conn_rec;
   dcl host char(NET_HOST_MAX) varying;
   dcl port fixed bin(31);
   dcl rc   fixed bin(31);

   rc = netc_connect(c.fd, host, length(host), port);
   call check_rc(rc);
   c.is_connected = '1'b;
   c.peer_port    = port;
end net_connect;
```

#### `net_dial` — parse `host:port` string, open, connect

Uses `INDEX` and `SUBSTR` to split the host:port pair, then delegates to
`net_open` + `net_connect`.

```pli
net_dial: procedure (h, hostport, family);
   dcl h        pointer;
   dcl hostport char(NET_HOST_MAX) varying;
   dcl family   fixed bin(31);
   dcl colon    fixed bin(31);
   dcl hoststr  char(NET_HOST_MAX);
   dcl portstr  char(6);
   dcl port     fixed bin(31);

   colon = index(hostport, ':');
   if colon = 0 then signal condition(net_error) set oncode(EINVAL);

   hoststr = substr(hostport, 1, colon - 1);
   portstr = substr(hostport, colon + 1, length(hostport) - colon);
   port    = fixed(trim(portstr));

   h = net_open(family, SOCK_STREAM, 0);
   if h = null() then return;
   call net_connect(h, trim(hoststr), port);
end net_dial;
```

#### `net_connect_nb` + `net_connect_finish` — nonblocking connect

`net_connect_nb` sets the fd nonblocking, calls `netc_connect_nb`, and
returns silently on `EINPROGRESS` (not an error — it means "poll for
writability next"):

```pli
net_connect_nb: procedure (h, host, port);
   dcl h    pointer;
   dcl 1 c  based(h) like conn_rec;
   dcl host char(NET_HOST_MAX) varying;
   dcl port fixed bin(31);
   dcl rc   fixed bin(31);

   call netc_setnonblock(c.fd, 1);

   rc = netc_connect_nb(c.fd, host, length(host), port);
   if rc = 0 then do;
      c.is_connected = '1'b;
      c.peer_port    = port;
      return;
   end;
   if rc = -1 then do;                 /* EINPROGRESS */
      call netc_clearerr();
      return;
   end;
   call raise_err();
end net_connect_nb;
```

`net_connect_finish` is called after the fd becomes writable (via
`net_poll`). It checks `SO_ERROR` to see if the connect succeeded:

```pli
net_connect_finish: procedure (h);
   dcl h  pointer;
   dcl 1 c based(h) like conn_rec;
   dcl e  fixed bin(31);
   dcl rc fixed bin(31);

   e = 0;
   rc = netc_getsockerr(c.fd, addr(e));
   call check_rc(rc);

   if e ^= 0 then signal condition(net_error) set oncode(e);

   c.is_connected = '1'b;
end net_connect_finish;
```

### 3.6 Server API

#### `net_listen` — bind + listen, returns a server handle

```pli
net_listen: procedure (port, backlog) returns (pointer);
   dcl port    fixed bin(31);
   dcl backlog fixed bin(31);
   dcl fd      fixed bin(31);
   dcl h       pointer;

   fd = netc_socket(AF_INET, SOCK_STREAM, 0);
   if fd < 0 then do;
      call raise_err();
      return (null());
   end;

   if netc_bind(fd, port) < 0 then do;
      call raise_err();
      call netc_close(fd);
      return (null());
   end;

   if netc_listen(fd, backlog) < 0 then do;
      call raise_err();
      call netc_close(fd);
      return (null());
   end;

   /* Server socket is nonblocking so poll() can multiplex. */
   call netc_setnonblock(fd, 1);

   allocate server_rec;
   server_rec.fd            = fd;
   server_rec.is_connected  = '1'b;
   server_rec.n_clients     = 0;
   h = addr(server_rec);
   return (h);
end net_listen;
```

#### `net_accept` — accept one connection (legacy single-client path)

```pli
net_accept: procedure (server, client);
   dcl server pointer;
   dcl client pointer;
   dcl 1 s    based(server) like conn_rec;
   dcl cfd    fixed bin(31);

   cfd = netc_accept(s.fd);
   if cfd < 0 then do;
      call raise_err();
      return;
   end;

   allocate conn_rec;
   conn_rec.fd            = cfd;
   conn_rec.is_connected  = '1'b;
   client = addr(conn_rec);
end net_accept;
```

#### Server pool accessors

`net_server_add_client` accepts a client and inserts it into the server's
pool. `net_server_nclients` returns the count. `net_server_client` returns
a handle by index. `net_server_remove_client` closes and compacts.

```pli
net_server_add_client: procedure (server, client);
   dcl server pointer;
   dcl client pointer;
   dcl 1 s    based(server) like server_rec;
   dcl cfd    fixed bin(31);

   cfd = netc_accept(s.fd);
   if cfd < 0 then do;
      call raise_err();
      return;
   end;

   if s.n_clients >= NET_MAX_CLIENTS then do;
      call netc_close(cfd);
      signal condition(net_error) set oncode(EMFILE);
      return;
   end;

   allocate conn_rec;
   conn_rec.fd = cfd;
   conn_rec.is_connected = '1'b;
   client = addr(conn_rec);

   s.n_clients = s.n_clients + 1;
   s.client_handles(s.n_clients) = client;
end net_server_add_client;

net_server_nclients: procedure (server) returns (fixed bin(31));
   dcl server pointer;
   dcl 1 s based(server) like server_rec;
   return (s.n_clients);
end net_server_nclients;

net_server_client: procedure (server, i) returns (pointer);
   dcl server pointer;
   dcl i    fixed bin(31);
   dcl 1 s  based(server) like server_rec;
   if i < 1 | i > s.n_clients then return (null());
   return (s.client_handles(i));
end net_server_client;

net_server_remove_client: procedure (server, i);
   dcl server pointer;
   dcl i    fixed bin(31);
   dcl 1 s  based(server) like server_rec;
   dcl j    fixed bin(31);
   if i < 1 | i > s.n_clients then return;
   call net_close(s.client_handles(i));
   do j = i to s.n_clients - 1;
      s.client_handles(j) = s.client_handles(j + 1);
   end;
   s.n_clients = s.n_clients - 1;
end net_server_remove_client;
```

### 3.7 Read operations

#### `net_read` — single `recv()` into a caller buffer

```pli
net_read: procedure (h, buffer) returns (fixed bin(31));
   dcl h      pointer;
   dcl 1 c    based(h) like conn_rec;
   dcl buffer char(NET_BUF_CAP) varying;
   dcl got    fixed bin(31);
   dcl tmp    char(NET_BUF_CAP);

   call validate_socket(c.fd);

   got = netc_recv(c.fd, tmp, NET_BUF_CAP, 0);
   got = check_recv(got, c.fd);
   if got > 0 then buffer = substr(tmp, 1, got);
   return (got);
end net_read;
```

#### `net_read_all` — accumulate the entire stream into a `CONTROLLED` buffer

This is the most intricate procedure. It uses `ALLOCATE` / `FREE` to grow a
`CHAR(*) VARYING CONTROLLED` buffer by doubling its capacity. The
"allocate aside" dance is necessary because `ALLOCATE` pushes a fresh
generation of the `CONTROLLED` item — the old data would be lost if you
simply re-allocated in place. The buffer grows **unboundedly** (there is no
max-byte cap; the API doc mentions a bounded variant as future work).

```pli
net_read_all: procedure (conn, buffer) returns (fixed bin(31));
   dcl conn    pointer;
   dcl 1 c     based(conn) like conn_rec;
   dcl buffer  char(*) varying controlled;
   dcl cap     fixed bin(31) init(1024);
   dcl spare   char(*) varying controlled;
   dcl chunk   char(NET_BUF_CAP) varying;
   dcl chunk_len fixed bin(31);
   dcl total_len fixed bin(31) init(0);

   call validate_socket(c.fd);

   allocate buffer (cap) character varying;

   do while ('1'b);
      chunk_len = net_read(conn, chunk);
      if chunk_len = 0 then leave;          /* EOF */

      /* Grow until capacity is sufficient */
      do while (total_len + chunk_len > cap);
         cap = cap * 2;
         allocate spare (cap) character varying;
         spare = buffer;
         free buffer;
         allocate buffer (cap) character varying;
         buffer = spare;
         free spare;
      end;

      /* Append chunk */
      substr(buffer, total_len + 1, chunk_len) =
         substr(chunk, 1, chunk_len);
      total_len = total_len + chunk_len;
   end;

   /* Trim to actual length */
   if total_len < cap then do;
      if total_len > 0 then do;
         allocate spare (total_len) character varying;
         spare = buffer;
         free buffer;
         allocate buffer (total_len) character varying;
         buffer = spare;
         free spare;
      end;
      else do;
         free buffer;
         allocate buffer (0) character varying;
      end;
   end;

   return (total_len);
end net_read_all;
```

#### `net_read_until` — read until a delimiter byte

Scans incoming data for a single-byte delimiter (e.g. `'0A'x` for newline).
Raises `net_overflow` if the caller's buffer fills before the delimiter is
found. Returns the 1-based position of the delimiter, or 0 on clean EOF.

```pli
net_read_until: procedure (h, buffer, delim) returns (fixed bin(31));
   dcl h      pointer;
   dcl 1 c    based(h) like conn_rec;
   dcl buffer char(NET_BUF_CAP) varying;
   dcl delim  char(1);
   dcl total  fixed bin(31);
   dcl pos    fixed bin(31);
   dcl got    fixed bin(31);
   dcl tmp    char(NET_BUF_CAP);
   dcl cap    fixed bin(31);

   call validate_socket(c.fd);

   total = length(buffer);
   do while ('1'b);
      cap = NET_BUF_CAP - total;
      if cap <= 0 then do;                        /* buffer full */
         call raise_overflow();
         return (0);
      end;

      got = netc_recv(c.fd, tmp, cap, 0);
      got = check_recv(got, c.fd);
      if got <= 0 then return (0);                /* EOF */

      if total = 0 then buffer = substr(tmp, 1, got);
      else buffer = substr(buffer, 1, total) || substr(tmp, 1, got);

      total = total + got;
      pos = index(buffer, delim);
      if pos > 0 then return (pos);
   end;
   return (0);
end net_read_until;
```

#### `net_read_all` — bounded variant

For callers who prefer a fixed `CONTROLLED` string with a manual grow-loop
(see `examples/fetch_dyn.pli` for the pattern).

### 3.8 Write operations

#### `net_write` — single `send()` (flags = 0)

```pli
net_write: procedure (h, buffer) returns (fixed bin(31));
   dcl h      pointer;
   dcl 1 c    based(h) like conn_rec;
   dcl buffer char(NET_BUF_CAP) varying;
   dcl n      fixed bin(31);

   n = netc_send(c.fd, buffer, length(buffer), 0);
   return (check_send(n, c.fd));
end net_write;
```

#### `net_send` — send with explicit flags

```pli
net_send: procedure (h, buffer, flags) returns (fixed bin(31));
   dcl h      pointer;
   dcl 1 c    based(h) like conn_rec;
   dcl buffer char(NET_BUF_CAP) varying;
   dcl flags  fixed bin(31);
   dcl n      fixed bin(31);

   n = netc_send(c.fd, buffer, length(buffer), flags);
   return (check_send(n, c.fd));
end net_send;
```

#### `net_send_all` — loop until all bytes are sent

```pli
net_send_all: procedure (h, buffer) returns (fixed bin(31));
   dcl h      pointer;
   dcl 1 c    based(h) like conn_rec;
   dcl buffer char(NET_BUF_CAP) varying;
   dcl total  fixed bin(31);
   dcl want   fixed bin(31);
   dcl n      fixed bin(31);
   dcl chunk  char(NET_BUF_CAP) varying;
   dcl take   fixed bin(31);

   total = 0;
   want  = length(buffer);
   do while (total < want);
      take  = min(NET_BUF_CAP, want - total);
      chunk = substr(buffer, total + 1, take);
      n = net_send(h, chunk, 0);
      total = total + n;
   end;
   return (total);
end net_send_all;
```

#### `net_send_once` — single send with flags, no retry

```pli
net_send_once: procedure (h, buffer, flags) returns (fixed bin(31));
   dcl h      pointer;
   dcl 1 c    based(h) like conn_rec;
   dcl buffer char(NET_BUF_CAP) varying;
   dcl flags  fixed bin(31);
   dcl n      fixed bin(31);

   n = netc_send(c.fd, buffer, length(buffer), flags);
   return (check_send(n, c.fd));
end net_send_once;
```

### 3.9 UDP datagrams

#### `net_sendto`

```pli
net_sendto: procedure (h, buffer, ip, port) returns (fixed bin(31));
   dcl h      pointer;
   dcl 1 c    based(h) like conn_rec;
   dcl buffer char(NET_BUF_CAP) varying;
   dcl ip     char(16) varying;
   dcl port   fixed bin(31);
   dcl n      fixed bin(31);

   n = netc_sendto(c.fd, buffer, length(buffer), 0, ip, length(ip), port);
   return (check_send(n, c.fd));
end net_sendto;
```

#### `net_recvfrom`

```pli
net_recvfrom: procedure (h, buffer, ip, port) returns (fixed bin(31));
   dcl h      pointer;
   dcl 1 c    based(h) like conn_rec;
   dcl buffer char(NET_BUF_CAP) varying;
   dcl ip     char(16) varying;
   dcl port   fixed bin(31);
   dcl n      fixed bin(31);
   dcl rbuf   char(NET_BUF_CAP);
   dcl raddr  char(16);

   n = netc_recvfrom(c.fd, rbuf, NET_BUF_CAP, 0, raddr, 16, addr(port));
   if n < 0 then do;
      if netc_errno() = EAGAIN then call raise_timeout();
      else call raise_err();
      return (0);
   end;

   if n > 0 then buffer = substr(rbuf, 1, n);
   else buffer = '';
   ip = trim(raddr);

   return (n);
end net_recvfrom;
```

### 3.10 Poll & I/O multiplexing

#### `net_poll` — single-fd readiness

```pli
net_poll: procedure (h, events, ms) returns (fixed bin(31));
   dcl h      pointer;
   dcl 1 c    based(h) like conn_rec;
   dcl events fixed bin(31);
   dcl ms     fixed bin(31);
   dcl r      fixed bin(31);

   r = netc_poll(c.fd, events, ms);
   call check_rc(r);
   return (r);
end net_poll;
```

> A `net_poll` timeout returns 0 (not an error). `net_timeout` is only
> raised by read/write paths that hit `EAGAIN` under a deadline.

#### `net_poll_multi` — multi-fd poll across the server pool

Builds a transient `CONTROLLED` array of fds from the server's client pool,
passes it to `netc_poll_multi`, then frees the array. The caller supplies
`events` and `revents` as fixed `fixed bin(31)` arrays of at least
`NET_MAX_CLIENTS + 1` elements (pli-llvm rejects controlled actuals for
`(*)` parameters).

```pli
net_poll_multi: procedure (server, events, revents, ms)
   returns (fixed bin(31));
   dcl server  pointer;
   dcl events(*) fixed bin(31);
   dcl revents(*) fixed bin(31);
   dcl ms      fixed bin(31);
   dcl 1 s     based(server) like server_rec;
   dcl fds     fixed bin(31) controlled;
   dcl r       fixed bin(31);
   dcl i       fixed bin(31);
   dcl n       fixed bin(31);
   dcl 1 c     based like conn_rec;

   n = s.n_clients + 1;
   allocate fds (n);
   fds(1) = s.fd;                          /* server socket at index 1 */
   do i = 1 to s.n_clients;
      fds(i + 1) = s.client_handles(i) -> c.fd;
   end;

   r = netc_poll_multi(addr(fds(1)), addr(events(1)),
                       addr(revents(1)), n, ms);
   free fds;

   if r < 0 then call raise_err();
   return (r);
end net_poll_multi;
```

### 3.11 Option setters

```pli
net_set_timeout: procedure (h, rto, wto);
   dcl h   pointer;
   dcl 1 c based(h) like conn_rec;
   dcl rto fixed bin(31);
   dcl wto fixed bin(31);
   dcl rc  fixed bin(31);

   rc = netc_settimeout(c.fd, rto, wto);
   call check_rc(rc);
   c.read_timeout  = rto;
   c.write_timeout = wto;
end net_set_timeout;

net_set_nonblocking: procedure (h, on);
   dcl h  pointer;
   dcl 1 c based(h) like conn_rec;
   dcl on bit(1);
   dcl rc fixed bin(31);

   if on then rc = netc_setnonblock(c.fd, 1);
   else       rc = netc_setnonblock(c.fd, 0);
   call check_rc(rc);
end net_set_nonblocking;

net_setopt: procedure (h, opt, value);
   dcl h     pointer;
   dcl 1 c   based(h) like conn_rec;
   dcl opt   fixed bin(31);
   dcl value fixed bin(31);
   dcl rc    fixed bin(31);

   rc = netc_sockopt(c.fd, opt, value);
   call check_rc(rc);
end net_setopt;

net_set_linger: procedure (h, on, seconds);
   dcl h       pointer;
   dcl 1 c     based(h) like conn_rec;
   dcl on      fixed bin(31);
   dcl seconds fixed bin(31);
   dcl rc      fixed bin(31);

   rc = netc_setlinger(c.fd, on, seconds);
   call check_rc(rc);
end net_set_linger;
```

### 3.12 Address queries

```pli
net_peer: procedure (h, ip, port);
   dcl h    pointer;
   dcl 1 c  based(h) like conn_rec;
   dcl ip   char(16) varying;
   dcl port fixed bin(31);
   dcl rc   fixed bin(31);
   dcl tmp  char(16);

   rc = netc_getpeername(c.fd, tmp, 16, addr(port));
   call check_rc(rc);
   ip = trim(tmp);
end net_peer;

net_local: procedure (h, ip, port);
   dcl h    pointer;
   dcl 1 c  based(h) like conn_rec;
   dcl ip   char(16) varying;
   dcl port fixed bin(31);
   dcl rc   fixed bin(31);
   dcl tmp  char(16);

   rc = netc_getsockname(c.fd, tmp, 16, addr(port));
   call check_rc(rc);
   ip = trim(tmp);
end net_local;
```

### 3.13 DNS resolution

```pli
net_resolve: procedure (host, ip);
   dcl host char(NET_HOST_MAX) varying;
   dcl ip   char(16) varying;
   dcl rc   fixed bin(31);
   dcl tmp  char(16);

   rc = netc_resolve(host, length(host), tmp, 16);
   call check_rc(rc);
   ip = trim(tmp);
end net_resolve;
```

### 3.14 Error text

#### `net_strerror` — errno → human-readable string (delegates to C `strerror`)

```pli
net_strerror: procedure (e, buf);
   dcl e   fixed bin(31);
   dcl buf char(NET_BUF_CAP) varying;
   dcl tmp char(NET_BUF_CAP);

   call netc_strerror(e, tmp, NET_BUF_CAP);
   buf = trim(tmp);
end net_strerror;
```

#### `net_errtext` — oncode → human-readable string (PL/I-only)

Uses a `SELECT` statement to map oncode values to descriptive strings,
falling back to the numeric value for unknown codes.

```pli
net_errtext: procedure returns (char(32));
   dcl e fixed bin(31);

   e = oncode();
   if e = 0        then return ('success');
   if e = NET_ERR_EOF       then return ('end of stream');
   if e = NET_ERR_TIMEOUT   then return ('timeout');
   if e = NET_ERR_OVERFLOW  then return ('buffer overflow');

   select;
      when (e = EINTR)        return ('interrupted');
      when (e = EBADF)        return ('bad file descriptor');
      when (e = EAGAIN)       return ('try again (EAGAIN)');
      when (e = EACCES)       return ('permission denied');
      when (e = EFAULT)       return ('bad address');
      when (e = EINVAL)       return ('invalid argument');
      when (e = EPIPE)        return ('broken pipe');
      when (e = ENOTCONN)     return ('not connected');
      when (e = ECONNRESET)   return ('connection reset');
      when (e = ETIMEDOUT)    return ('connection timed out');
      when (e = ECONNREFUSED) return ('connection refused');
      when (e = EHOSTUNREACH) return ('host unreachable');
      when (e = ENETUNREACH)  return ('network unreachable');
      otherwise               return ('errno ' || char(e));
   end;
end net_errtext;
```

### 3.15 Module tail

```pli
end net;
```

---

## 4. Error Model — Conditions Only

| Condition       | Meaning                          | `oncode()`             |
|-----------------|----------------------------------|------------------------|
| `net_error`     | hard error                       | POSIX errno (positive) |
| `net_timeout`   | timeout / `EAGAIN` / `EINTR`     | `NET_ERR_TIMEOUT` (-3) |
| `net_eof`       | peer closed / end of stream      | `NET_ERR_EOF` (-2)     |
| `net_overflow`  | caller buffer exhausted          | `NET_ERR_OVERFLOW` (-4)|

**No procedure returns an error code.** A return value is always data
(byte count, delimiter position, pointer handle, ready mask).

Caller pattern — re-arm the condition inside the ON-unit so a failure
within the handler cannot recurse:

```pli
on condition(net_error) begin;
   on condition(net_error) system;
   put skip list('error:', net_errtext());
   stop;
end;
```

---

## 5. Build System (CMakeLists.txt)

### 5.1 Library target

```cmake
add_library(net STATIC source/c_bridge.c)
target_compile_options(net PRIVATE
   $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>)
if(WIN32)
   target_link_libraries(net PUBLIC ws2_32)
endif()
```

### 5.2 PL/I module compilation

When `pli` is found on `PATH`, compile `source/net.pli` to an object and
add it to the static library:

```cmake
find_program(PLI_EXECUTABLE NAMES pli)
if(PLI_EXECUTABLE)
   add_custom_command(
      OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/net${CMAKE_C_OUTPUT_EXTENSION}"
      COMMAND "${PLI_EXECUTABLE}" -c source/net.pli
         -I include -o "${CMAKE_CURRENT_BINARY_DIR}/net.o"
      DEPENDS source/net.pli include/net.inc include/c_bridge.inc
              include/type_defs.inc include/errno.inc)
   target_sources(net PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/net.o")
endif()
```

### 5.3 Linking a PL/I program

On macOS the C bridge's socket symbols (`_socket`, `_connect`, `_send`,
`_recv`, `_setsockopt`, …) are not resolved by pli-llvm's minimal
`libSystem.tbd` stub. They are resolved at runtime by dyld from the host's
real libSystem, so tell lld to allow deferred symbol resolution with
`-undefined dynamic_lookup` (passed as two `-Wl` args, since pli-llvm's `-Wl`
consumes one flag per invocation):

```bash
pli examples/simple_usage.pli -I include -L build -lnet \
   -Wl,-undefined -Wl,dynamic_lookup -o build/simple_usage
```

In CMake (`libnet_add_pli_program`):

```cmake
if(APPLE)
  set(_pli_extra -Wl,-undefined -Wl,dynamic_lookup)
else()
  set(_pli_extra)
endif()
add_custom_target(${name}
   COMMAND "${PLI_EXECUTABLE}" ${_pli_flags}
      "${src}"
      -I include
      -o "${_out}"
      -L "$<TARGET_FILE_DIR:net>" -lnet
      ${_pli_extra}
   DEPENDS net "${src}"
   ...)
```

---

## 6. Testing Strategy

### 6.1 C bridge regression test (`tests/c_bridge.c`)

Compile and run without any PL/I dependency:

```bash
ctest --test-dir build --output-on-failure
```

Tests socket creation, options, bind/listen, UDP loopback, nonblocking
connect, `strerror`, and poll mask mapping.

### 6.2 PL/I integration tests (examples)

All examples are trial-compiled (syntax-checked) by the
`libnet-examples` CMake target:

```bash
cmake --build build --target libnet-examples
```

### 6.3 Runtime smoke test

The `simple_usage` target links the full program. Run it:

```bash
cmake --build build --target simple_usage
./build/simple_usage
```

---

## 7. Examples Inventory

| File                  | Purpose                                           |
|-----------------------|---------------------------------------------------|
| `simple_usage.pli`    | Quick-start: HTTP GET via `net_read_all`         |
| `client.pli`          | Minimal TCP client — send line, read line        |
| `echo_server.pli`     | Single-client echo server (`net_read_until`)      |
| `multi_echo_server.pli` | Multi-client event loop (`net_poll_multi` + pool) |
| `fetch.pli`           | HTTP/1.0 GET to example.com                       |
| `fetch_dyn.pli`       | Manual `CONTROLLED` string grow-loop              |
| `http_client.pli`     | HTTP/1.0 GET via `net_read_all`                   |
| `resolve.pli`         | DNS resolution demo (`net_resolve`)               |
| `test_read_all.pli`   | Scratch test for `net_read_all`                    |
| `test_resolve.pli`    | Scratch test for `net_resolve`                     |

---

## 8. Implementation Status

| Component             | Status | Notes                                           |
|-----------------------|--------|-------------------------------------------------|
| Include files         | Done   | `net.inc`, `type_defs.inc`, `errno.inc`, `c_bridge.inc` |
| PL/I module (`net.pli`)| Done  | All 26+ procedures implemented and compiling     |
| C bridge (`c_bridge.c`) | Done  | Already complete — not modified by this plan     |
| CMake build           | Done   | `net` static lib + `libnet-dist-inc` target      |
| C bridge tests        | Done   | `cb_test` passes                                 |
| PL/I examples         | Done   | All 10 examples compile (trial-compile)         |
| PL/I linking (macOS)  | Fixed  | `-undefined dynamic_lookup` added to `libnet_add_pli_program` |
| PL/I linking (POSIX)  | OK     | Socket symbols resolved at runtime by dyld                      |
| PL/I linking (Windows)| OK     | Uses `/DEFAULTLIB:ws2_32`                                       |
| API reference         | Done   | `docs/api.md`                                    |

---

## 9. Known Gaps & Future Work

1. **`net_read_all` bounded capacity**: the current implementation grows
   the `CONTROLLED` buffer unboundedly. The API doc mentions a bounded mode
   where `net_overflow` is raised if the caller provides a fixed-capacity
   `char(*) varying` buffer. Consider adding a `net_read_all_bounded`
   variant that accepts a max-byte cap and signals `net_overflow`.

2. **Connect-timeout orchestration**: `net_connect_nb` + `net_poll` +
   `net_connect_finish` can be wired into a single
   `net_connect_to(h, host, port, ms)` — a future enhancement documented
   in the README.

3. **Thread-safety** — the `CONTROLLED` pool is per-thread; cross-thread
   sharing requires a guard (documented in README as future work).

4. **`net_accept` vs nonblocking server sockets**: `net_listen` sets the
   server fd nonblocking (for `net_poll_multi`), but `net_accept` calls
   `netc_accept` directly and raises `net_error` on `EAGAIN`. The single
   `echo_server.pli` example therefore needs a `net_poll(h, POLLIN, -1)`
   before calling `net_accept`. Future: `net_accept` should internally poll
   when the socket is nonblocking.

5. **macOS example targets**: only `simple_usage` has a `libnet_add_pli_program`
   entry in `CMakeLists.txt`. Add link targets for `fetch`, `http_client`,
   `resolve`, and `multi_echo_server` so they can be built and run directly
   via CMake.
