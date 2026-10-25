/* c_bridge.c — thin C bridge for libnet (minimal: raw syscalls only).
 *
 * The ONLY C in the whole library. Each function is a thin syscall/libc
 * wrapper reached from PL/I through pli-llvm's by-value FFI
 * (OPTIONS(LINKAGE(SYSTEM)) / OPTIONS(BYVALUE)). No buffering, no policy,
 * no DNS result parsing, no option-code mapping: all logic above this
 * file lives in PL/I (source/net.pli).
 *
 * The remaining marshalling is deliberately minimal:
 *   - netc_getaddrinfo NUL-terminates the host and writes sin_port into
 *     the returned sockaddr_in;
 *   - netc_poll / netc_poll_multi translate the libnet event mask to
 *     and from poll(2) bits.
 * All address construction/parsing, DNS result formatting, option-code
 * mapping, and error-text generation happen in PL/I.
 *
 * Portability:
 *   - POSIX (Linux/macOS): raw fds, poll(2), fcntl O_NONBLOCK, struct
 *     timeval socket timeouts.
 *   - Windows (_WIN32, MSVC or MinGW): Winsock2. fds are small 1-based
 *     handles into an internal SOCKET table (Winsock SOCKETs do not fit
 *     in an int, so they are never exposed directly). WSA error codes are
 *     translated to POSIX numbers so the PL/I layer keeps working.
 *
 * The struct sockaddr_in layout (used by PL/I to build raw address buffers):
 *   Byte  0:   sin_len    (uint8)  = 16
 *   Byte  1:   sin_family (uint8)  = AF_INET (2)
 *   Bytes 2-3: sin_port   (uint16, network/big-endian)
 *   Bytes 4-7: sin_addr   (uint32, network/big-endian)
 *   Bytes 8-15: sin_zero  (8 bytes, zeroed)
 * Total: 16 bytes. PL/I builds/parses this buffer directly.
 */

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef int netc_socklen_t;

#if defined(_MSC_VER)
#define NETC_THREAD_LOCAL __declspec(thread)
#else
#define NETC_THREAD_LOCAL _Thread_local
#endif

/* Errno values used for the thread-local cache on Windows. These must
 * match include/errno.inc (macOS arm64) so PL/I comparisons work. */
#define LNX_EPERM 1
#define LNX_ENOENT 2
#define LNX_EINTR 4
#define LNX_EBADF 9
#define LNX_EAGAIN 35
#define LNX_ENOMEM 12
#define LNX_EACCES 13
#define LNX_EFAULT 14
#define LNX_EINVAL 22
#define LNX_EPIPE 32
#define LNX_EMFILE 24
#define LNX_ENOTSOCK 38
#define LNX_EMSGSIZE 40
#define LNX_EADDRINUSE 48
#define LNX_EADDRNOTAVAIL 49
#define LNX_ENETUNREACH 51
#define LNX_ECONNRESET 54
#define LNX_ENOBUFS 55
#define LNX_ENOTCONN 57
#define LNX_ETIMEDOUT 60
#define LNX_ECONNREFUSED 61
#define LNX_EHOSTDOWN 64
#define LNX_EHOSTUNREACH 65
#define LNX_EINPROGRESS 36

/* Small 1-based handle table: PL/I and the C test only ever see ints. */
#define NETC_MAX_FDS 1024
static SOCKET netc_table[NETC_MAX_FDS];
static int netc_used[NETC_MAX_FDS];
static int netc_wsa_ready = 0;

static void netc_ensure_wsa(void) {
  if (!netc_wsa_ready) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0)
      netc_wsa_ready = 1;
  }
}

static SOCKET netc_lookup(int fd) {
  int i = fd - 1;
  if (i < 0 || i >= NETC_MAX_FDS || !netc_used[i])
    return INVALID_SOCKET;
  return netc_table[i];
}

static int netc_alloc(SOCKET s) {
  int i;
  for (i = 0; i < NETC_MAX_FDS; ++i) {
    if (!netc_used[i]) {
      netc_used[i] = 1;
      netc_table[i] = s;
      return i + 1;
    }
  }
  closesocket(s);
  return -1;
}

static void netc_drop(int fd) {
  int i = fd - 1;
  if (i >= 0 && i < NETC_MAX_FDS) {
    netc_used[i] = 0;
    netc_table[i] = INVALID_SOCKET;
  }
}

/* Translate a Winsock or getaddrinfo error into a POSIX errno number. */
static int wsa_to_errno(int wsa) {
  switch (wsa) {
    case 0: return 0;
    case WSAEINTR: return LNX_EINTR;
    case WSAEACCES: return LNX_EACCES;
    case WSAEFAULT: return LNX_EFAULT;
    case WSAEINVAL: return LNX_EINVAL;
    case WSAEMFILE: return LNX_EMFILE;
    case WSAEWOULDBLOCK: return LNX_EAGAIN;
    case WSAEINPROGRESS: return LNX_EINPROGRESS;
    case WSAEALREADY: return LNX_EINPROGRESS;
    case WSAENOTSOCK: return LNX_ENOTSOCK;
    case WSAEDESTADDRREQ: return LNX_EINVAL;
    case WSAEMSGSIZE: return LNX_EMSGSIZE;
    case WSAEADDRINUSE: return LNX_EADDRINUSE;
    case WSAEADDRNOTAVAIL: return LNX_EADDRNOTAVAIL;
    case WSAENETDOWN:
    case WSAENETUNREACH: return LNX_ENETUNREACH;
    case WSAENETRESET:
    case WSAECONNABORTED:
    case WSAECONNRESET:
    case WSAEDISCON: return LNX_ECONNRESET;
    case WSAENOBUFS: return LNX_ENOBUFS;
    case WSAEISCONN:
    case WSAESHUTDOWN: return LNX_EPIPE;
    case WSAENOTCONN: return LNX_ENOTCONN;
    case WSAETOOMANYREFS:
    case WSAEUSERS:
    case WSAEDQUOT: return LNX_ENOMEM;
    case WSAETIMEDOUT: return LNX_ETIMEDOUT;
    case WSAECONNREFUSED: return LNX_ECONNREFUSED;
    case WSAELOOP:
    case WSAENAMETOOLONG:
    case WSAENOTEMPTY:
    case WSAESTALE:
    case WSAEREMOTE: return LNX_EINVAL;
    case WSAEHOSTDOWN: return LNX_EHOSTDOWN;
    case WSAEHOSTUNREACH: return LNX_EHOSTUNREACH;
    case WSAEPROCLIM: return LNX_EMFILE;
    case WSASYSNOTREADY:
    case WSATRY_AGAIN: return LNX_EAGAIN;
    case WSAHOST_NOT_FOUND:
    case WSANO_DATA: return LNX_ENOENT;
    default: return LNX_EINVAL;
  }
}

#else /* POSIX */

#include <arpa/inet.h>
#include <errno.h>
/* LNX_* constants are used in non-Windows code paths too (e.g.
   netc_getaddrinfo sets cached_errno = LNX_EFAULT directly). On POSIX,
   map them to the system's errno.h values so the PL/I layer sees
   the same numbers on every platform. */
#define LNX_EFAULT EFAULT
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

typedef socklen_t netc_socklen_t;

#define NETC_THREAD_LOCAL _Thread_local

/* getaddrinfo returns EAI_* codes, not errno values. Map the common
 * failures so PL/I always gets a non-zero error on failure. */
static int gai_to_errno(int gai) {
  switch (gai) {
    case EAI_AGAIN:    return EAGAIN;
    case EAI_BADFLAGS: return EINVAL;
    case EAI_FAIL:     return EINVAL;
    case EAI_FAMILY:   return EINVAL;
    case EAI_MEMORY:   return ENOMEM;
    case EAI_NONAME:   return ENOENT;
#if defined(EAI_NODATA) && EAI_NODATA != EAI_NONAME
    case EAI_NODATA:   return ENOENT;
#endif
    case EAI_SERVICE:  return EINVAL;
    case EAI_SOCKTYPE: return EINVAL;
    case EAI_SYSTEM:   return errno ? errno : EINVAL;
    default:           return EINVAL;
  }
}

#endif /* _WIN32 / POSIX */

/* Thread-local errno snapshot so PL/I can read it without clobbering errno. */
static NETC_THREAD_LOCAL int cached_errno = 0;

static void save_errno(void) {
#ifdef _WIN32
  cached_errno = wsa_to_errno(WSAGetLastError());
#else
  cached_errno = errno;
#endif
}

/*=======================================================================*/
/* Raw socket lifecycle (syscalls)                                       */
/*======================================================================*/

int netc_socket(int family, int type, int proto) {
#ifdef _WIN32
  SOCKET s;
  int fd;
  netc_ensure_wsa();
  s = socket(family, type, proto);
  if (s == INVALID_SOCKET) { save_errno(); return -1; }
  fd = netc_alloc(s);
  if (fd < 0) cached_errno = LNX_EMFILE;
  return fd;
#else
  int fd = socket(family, type, proto);
  if (fd < 0) save_errno();
  return fd;
#endif
}

int netc_close(int fd) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  if (s == INVALID_SOCKET) { cached_errno = LNX_EBADF; return -1; }
  netc_drop(fd);
  if (closesocket(s) != 0) { save_errno(); return -1; }
  return 0;
#else
  if (close(fd) != 0) { save_errno(); return -1; }
  return 0;
#endif
}

int netc_shutdown(int fd, int how) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  if (s == INVALID_SOCKET) { cached_errno = LNX_EBADF; return -1; }
  if (shutdown(s, how) != 0) { save_errno(); return -1; }
  return 0;
#else
  if (shutdown(fd, how) != 0) { save_errno(); return -1; }
  return 0;
#endif
}

/* Bind to an address. addr_buf points to a raw struct sockaddr_in (16 bytes).
 * In practice we bind to INADDR_ANY, but the caller's PL/I layer builds the
 * full sockaddr_in, so we just pass the raw bytes. */
int netc_bind(int fd, const char *addr_buf, int addrlen) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  if (s == INVALID_SOCKET) { cached_errno = LNX_EBADF; return -1; }
  if (bind(s, (const struct sockaddr *)addr_buf, addrlen) != 0) {
    save_errno(); return -1;
  }
  return 0;
#else
  if (bind(fd, (const struct sockaddr *)addr_buf, (socklen_t)addrlen) != 0) {
    save_errno(); return -1;
  }
  return 0;
#endif
}

int netc_listen(int fd, int backlog) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  if (s == INVALID_SOCKET) { cached_errno = LNX_EBADF; return -1; }
  if (listen(s, backlog) != 0) { save_errno(); return -1; }
  return 0;
#else
  if (listen(fd, backlog) != 0) { save_errno(); return -1; }
  return 0;
#endif
}

int netc_accept(int fd) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  SOCKET c;
  int nfd;
  if (s == INVALID_SOCKET) { cached_errno = LNX_EBADF; return -1; }
  c = accept(s, NULL, NULL);
  if (c == INVALID_SOCKET) { save_errno(); return -1; }
  nfd = netc_alloc(c);
  if (nfd < 0) cached_errno = LNX_EMFILE;
  return nfd;
#else
  int cfd = accept(fd, NULL, NULL);
  if (cfd < 0) save_errno();
  return cfd;
#endif
}

/*=======================================================================*/
/* Raw connect (takes a raw struct sockaddr_in buffer)                   */
/*======================================================================*/

int netc_connect(int fd, const char *addr_buf, int addrlen) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  if (s == INVALID_SOCKET) { cached_errno = LNX_EBADF; return -1; }
  if (connect(s, (const struct sockaddr *)addr_buf, addrlen) != 0) {
    save_errno(); return -1;
  }
  return 0;
#else
  if (connect(fd, (const struct sockaddr *)addr_buf, (socklen_t)addrlen) != 0) {
    save_errno(); return -1;
  }
  return 0;
#endif
}

/*=======================================================================*/
/* Raw send / recv                                                       */
/*======================================================================*/

int netc_send(int fd, const char *buf, int len, int flags) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  int n;
  if (s == INVALID_SOCKET) { cached_errno = LNX_EBADF; return -1; }
  n = send(s, buf, len, flags);
  if (n < 0) save_errno();
  return n;
#else
  int n = (int)send(fd, buf, (unsigned)len, flags);
  if (n < 0) save_errno();
  return n;
#endif
}

int netc_recv(int fd, char *buf, int len, int flags) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  int n;
  if (s == INVALID_SOCKET) { cached_errno = LNX_EBADF; return -1; }
  n = recv(s, buf, len, flags);
  if (n < 0) save_errno();
  return n;
#else
  int n = (int)recv(fd, buf, (unsigned)len, flags);
  if (n < 0) save_errno();
  return n;
#endif
}

/*=======================================================================*/
/* Raw UDP sendto / recvfrom (take/fill raw struct sockaddr_in buffers)  */
/*======================================================================*/

int netc_sendto(int fd, const char *buf, int len, int flags,
                const char *addr_buf, int addrlen) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  int r;
  if (s == INVALID_SOCKET) { cached_errno = LNX_EBADF; return -1; }
  r = sendto(s, buf, len, flags,
             (const struct sockaddr *)addr_buf, addrlen);
  if (r < 0) save_errno();
  return r;
#else
  int r = (int)sendto(fd, buf, (unsigned)len, flags,
                      (const struct sockaddr *)addr_buf, (socklen_t)addrlen);
  if (r < 0) save_errno();
  return r;
#endif
}

int netc_recvfrom(int fd, char *buf, int len, int flags,
                  char *addr_buf, int *addrlen) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  netc_socklen_t alen;
  int r;
  if (s == INVALID_SOCKET) { cached_errno = LNX_EBADF; return -1; }
  alen = (netc_socklen_t)*addrlen;
  r = recvfrom(s, buf, len, flags,
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

/*=======================================================================*/
/* Raw socket options (level and optname are the real SOL_SOCKET/SO_*     */
/* values; PL/I maps NETOPT_* to them)                                    */
/*======================================================================*/

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

int netc_getsockopt(int fd, int level, int optname,
                    void *value, int *optlen) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  if (s == INVALID_SOCKET) { cached_errno = LNX_EBADF; return -1; }
  if (getsockopt(s, level, optname, (char *)value, (int *)optlen) != 0) {
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

/*=======================================================================*/
/* Raw address queries (fill caller-provided sockaddr_in buffer)         */
/*======================================================================*/

int netc_getpeername(int fd, char *addr_buf, int *addrlen) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  netc_socklen_t alen;
  if (s == INVALID_SOCKET) { cached_errno = LNX_EBADF; return -1; }
  alen = (netc_socklen_t)*addrlen;
  if (getpeername(s, (struct sockaddr *)addr_buf, &alen) != 0) {
    save_errno(); return -1;
  }
  *addrlen = (int)alen;
  return 0;
#else
  socklen_t alen = (socklen_t)*addrlen;
  if (getpeername(fd, (struct sockaddr *)addr_buf, &alen) != 0) {
    save_errno(); return -1;
  }
  *addrlen = (int)alen;
  return 0;
#endif
}

int netc_getsockname(int fd, char *addr_buf, int *addrlen) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  netc_socklen_t alen;
  if (s == INVALID_SOCKET) { cached_errno = LNX_EBADF; return -1; }
  alen = (netc_socklen_t)*addrlen;
  if (getsockname(s, (struct sockaddr *)addr_buf, &alen) != 0) {
    save_errno(); return -1;
  }
  *addrlen = (int)alen;
  return 0;
#else
  socklen_t alen = (socklen_t)*addrlen;
  if (getsockname(fd, (struct sockaddr *)addr_buf, &alen) != 0) {
    save_errno(); return -1;
  }
  *addrlen = (int)alen;
  return 0;
#endif
}

/*=======================================================================*/
/* Nonblocking I/O control                                               */
/*======================================================================*/

int netc_setnonblock(int fd, int on) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  u_long mode;
  if (s == INVALID_SOCKET) { cached_errno = LNX_EBADF; return -1; }
  mode = on ? 1UL : 0UL;
  if (ioctlsocket(s, FIONBIO, &mode) != 0) { save_errno(); return -1; }
  return 0;
#else
  int fl = fcntl(fd, F_GETFL, 0);
  if (fl < 0) { save_errno(); return -1; }
  if (on) fl |= O_NONBLOCK;
  else     fl &= ~O_NONBLOCK;
  if (fcntl(fd, F_SETFL, fl) != 0) { save_errno(); return -1; }
  return 0;
#endif
}

/*=======================================================================*/
/* poll(2) — single and multi-fd                                         */
/*======================================================================*/

/* Events use the libnet bitmask: 1=readable, 2=writable. */
int netc_poll(int fd, int events, int ms) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  WSAPOLLFD p;
  int r;
  if (s == INVALID_SOCKET) { cached_errno = LNX_EBADF; return -1; }
  p.fd = s;
  p.events = 0;
  if (events & 1) p.events |= POLLIN;
  if (events & 2) p.events |= POLLOUT;
  p.revents = 0;
  r = WSAPoll(&p, 1, ms);
  if (r == SOCKET_ERROR) { save_errno(); return -1; }
  return r;
#else
  struct pollfd p;
  p.fd = fd;
  p.events = 0;
  if (events & 1) p.events |= POLLIN;
  if (events & 2) p.events |= POLLOUT;
  p.revents = 0;
  int r = poll(&p, 1, ms);
  if (r < 0) save_errno();
  return r;
#endif
}

/* Multi-fd poll: poll across many fds at once.
 * fds    - array of file descriptors (input)
 * events - array of event masks (input), one per fd
 * revents- array of returned event masks (output), one per fd
 * nfds   - number of entries in each array
 * ms     - timeout in milliseconds (-1 = infinite)
 * Returns the number of ready fds, 0 on timeout, -1 on error. */
int netc_poll_multi(const int *fds, const int *events, int *revents,
                    int nfds, int ms) {
  if (nfds <= 0) return 0;
#ifdef _WIN32
  {
    WSAPOLLFD *pfds;
    int *idx;
    int i, n = 0, bad = 0, r, ready = 0;
    netc_ensure_wsa();
    pfds = (WSAPOLLFD *)malloc((size_t)nfds * sizeof(WSAPOLLFD));
    idx = (int *)malloc((size_t)nfds * sizeof(int));
    if (!pfds || !idx) { free(pfds); free(idx); cached_errno = LNX_ENOMEM; return -1; }
    for (i = 0; i < nfds; ++i) {
      SOCKET s = netc_lookup(fds[i]);
      if (s == INVALID_SOCKET) { revents[i] = 4; bad++; continue; }
      pfds[n].fd = s;
      pfds[n].events = 0;
      if (events[i] & 1) pfds[n].events |= POLLIN;
      if (events[i] & 2) pfds[n].events |= POLLOUT;
      pfds[n].revents = 0;
      idx[n] = i;
      n++;
    }
    if (n > 0) {
      r = WSAPoll(pfds, (ULONG)n, ms);
      if (r == SOCKET_ERROR) { save_errno(); free(pfds); free(idx); return -1; }
      for (i = 0; i < n; ++i) {
        int re = 0;
        if (pfds[i].revents & POLLIN)  re |= 1;
        if (pfds[i].revents & POLLOUT) re |= 2;
        if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) re |= 4;
        revents[idx[i]] = re;
        if (re) ready++;
      }
    }
    free(pfds); free(idx);
    return ready + bad;
  }
#else
  struct pollfd *pfds = (struct pollfd *)malloc((size_t)nfds * sizeof(struct pollfd));
  if (!pfds) { errno = ENOMEM; save_errno(); return -1; }
  {
    int i, r, ready = 0;
    for (i = 0; i < nfds; ++i) {
      pfds[i].fd = fds[i];
      pfds[i].events = 0;
      if (events[i] & 1) pfds[i].events |= POLLIN;
      if (events[i] & 2) pfds[i].events |= POLLOUT;
      pfds[i].revents = 0;
    }
    r = poll(pfds, nfds, ms);
    if (r >= 0) {
      for (i = 0; i < nfds; ++i) {
        int re = 0;
        if (pfds[i].revents & POLLIN)  re |= 1;
        if (pfds[i].revents & POLLOUT) re |= 2;
        if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) re |= 4;
        revents[i] = re;
        if (re) ready++;
      }
    } else {
      save_errno();
    }
    free(pfds);
    return r >= 0 ? ready : -1;
  }
#endif
}

/*=======================================================================*/
/* DNS — raw getaddrinfo wrapper                                         */
/*======================================================================*/

/* Resolve host (NUL-terminated internally; hostlen given for PL/I char).
 * port is set into the resulting sockaddr_in's sin_port field (network
 * byte order).  addr_buf receives 16 bytes of sockaddr_in.
 * Returns 0 on success, -1 on error (errno cached). */
int netc_getaddrinfo(const char *host, int hostlen, int port, char *addr_buf) {
  struct addrinfo hints, *res = NULL;
  int n = hostlen;
  int gai;
  char hbuf[256];
  int i;

  cached_errno = 0;
  memset(addr_buf, 0, 16);
  if (n < 0) n = 0;
  if (n > 255) n = 255;
  for (i = 0; i < n; ++i) hbuf[i] = host[i];
  /* Trim trailing whitespace (PL/I blank padding) */
  while (n > 0 && (hbuf[n-1] == ' ' || hbuf[n-1] == '\t')) n--;
  hbuf[n] = '\0';

#ifdef _WIN32
  netc_ensure_wsa();
#endif

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  gai = getaddrinfo(hbuf, NULL, &hints, &res);
  if (gai != 0) {
#ifdef _WIN32
    cached_errno = wsa_to_errno(gai);
#else
    cached_errno = gai_to_errno(gai);
#endif
    if (cached_errno == 0) cached_errno = EINVAL;
    return -1;
  }
  if (res == NULL || res->ai_addr == NULL ||
      res->ai_addrlen < sizeof(struct sockaddr_in) ||
      res->ai_addrlen > 16) {
    cached_errno = LNX_EFAULT;
    if (res != NULL) freeaddrinfo(res);
    return -1;
  }

  memcpy(addr_buf, res->ai_addr, sizeof(struct sockaddr_in));
  ((struct sockaddr_in *)addr_buf)->sin_port =
      htons((unsigned short)port);
  freeaddrinfo(res);
  return 0;
}

/*=======================================================================*/
/* Error                                                                 */
/*======================================================================*/

int netc_errno(void) {
  return cached_errno;
}

void netc_clearerr(void) {
  cached_errno = 0;
}
