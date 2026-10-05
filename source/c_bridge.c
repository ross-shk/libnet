/* c_bridge.c — thin C bridge for libnet.
 *
 * The only C in the whole library. Each function is a bare syscall wrapper
 * reached from PL/I through pli-llvm's by-value FFI
 * (OPTIONS(LINKAGE(SYSTEM)) / OPTIONS(BYVALUE)). No buffering, no policy,
 * no connection state: everything above this file lives in PL/I.
 *
 * Conventions:
 *   - fd and scalar statuses are int (fixed bin(31)) by value.
 *   - character buffers are char* with an explicit length (PL/I CHAR is
 *     blank-padded and not NUL-terminated, so we never rely on NUL).
 *   - most functions return 0 on success, -1 on error; errno is retained in
 *     a thread-local cache read by netc_errno().
 *   - IPv4 only (AF_INET): address strings are 16-byte dotted quads.
 *     AF_INET6 is reserved for a future revision with wider buffers.
 *
 * Portability:
 *   - POSIX (Linux/macOS): raw fds, poll(2), fcntl O_NONBLOCK, struct
 *     timeval socket timeouts.
 *   - Windows (_WIN32, MSVC or MinGW): Winsock2. fds are small 1-based
 *     handles into an internal SOCKET table (Winsock SOCKETs do not fit
 *     in an int, so they are never exposed directly). WSA error codes are
 *     translated to the POSIX numbers in include/errno.inc so the PL/I
 *     layer's comparisons keep working. Link with ws2_32 (handled by
 *     CMakeLists.txt). Requires Vista+ (WSAPoll, inet_ntop).
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

/* POSIX errno numbers (must match include/errno.inc) used for the
 * thread-local cache on Windows, independent of the CRT's own values. */
#define LNX_EPERM 1
#define LNX_ENOENT 2
#define LNX_EINTR 4
#define LNX_EBADF 9
#define LNX_EAGAIN 11
#define LNX_ENOMEM 12
#define LNX_EACCES 13
#define LNX_EFAULT 14
#define LNX_EINVAL 22
#define LNX_EMFILE 24
#define LNX_EPIPE 32
#define LNX_ENOTSOCK 88
#define LNX_EMSGSIZE 90
#define LNX_EADDRINUSE 98
#define LNX_EADDRNOTAVAIL 99
#define LNX_ENETUNREACH 101
#define LNX_ECONNRESET 104
#define LNX_ENOBUFS 105
#define LNX_ENOTCONN 107
#define LNX_ETIMEDOUT 110
#define LNX_ECONNREFUSED 111
#define LNX_EHOSTDOWN 112
#define LNX_EHOSTUNREACH 113
#define LNX_EINPROGRESS 115

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

/* Translate a Winsock (or getaddrinfo) error into a POSIX errno number. */
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
    case WSAEPROTOTYPE:
    case WSAENOPROTOOPT:
    case WSAEPROTONOSUPPORT:
    case WSAESOCKTNOSUPPORT:
    case WSAEOPNOTSUPP:
    case WSAEPFNOSUPPORT:
    case WSAEAFNOSUPPORT: return LNX_EINVAL;
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

/* Copy a PL/I string (src, srclen bytes) into a NUL-terminated C buffer.
 * Trailing blanks/tabs are padding, not content, so trim them; this serves
 * both VARYING (exact length) and fixed CHAR(N) (blank-padded) callers. */
static void pli_to_cstr(const char *src, int srclen, char *dst, int dstsize) {
  int n = srclen;
  if (n > dstsize - 1)
    n = dstsize - 1;
  while (n > 0 && (src[n - 1] == ' ' || src[n - 1] == '\t'))
    n--;
  int i;
  for (i = 0; i < n; ++i)
    dst[i] = src[i];
  dst[n] = '\0';
}

int netc_socket(int family, int type, int proto) {
#ifdef _WIN32
  SOCKET s;
  int fd;
  netc_ensure_wsa();
  s = socket(family, type, proto);
  if (s == INVALID_SOCKET) {
    save_errno();
    return -1;
  }
  fd = netc_alloc(s);
  if (fd < 0)
    cached_errno = LNX_EMFILE;
  return fd;
#else
  int fd = socket(family, type, proto);
  if (fd < 0)
    save_errno();
  return fd;
#endif
}

int netc_bind(int fd, unsigned int port) {
#ifdef _WIN32
  SOCKET s;
  struct sockaddr_in a;
  netc_ensure_wsa();
  s = netc_lookup(fd);
  if (s == INVALID_SOCKET) {
    cached_errno = LNX_EBADF;
    return -1;
  }
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons((unsigned short)port);
  if (bind(s, (struct sockaddr *)&a, (int)sizeof a) != 0) {
    save_errno();
    return -1;
  }
  return 0;
#else
  struct sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons((unsigned short)port);
  if (bind(fd, (struct sockaddr *)&a, sizeof a) != 0) {
    save_errno();
    return -1;
  }
  return 0;
#endif
}

int netc_listen(int fd, int backlog) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  if (s == INVALID_SOCKET) {
    cached_errno = LNX_EBADF;
    return -1;
  }
  if (listen(s, backlog) != 0) {
    save_errno();
    return -1;
  }
  return 0;
#else
  if (listen(fd, backlog) != 0) {
    save_errno();
    return -1;
  }
  return 0;
#endif
}

int netc_accept(int fd) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  SOCKET c;
  int nfd;
  if (s == INVALID_SOCKET) {
    cached_errno = LNX_EBADF;
    return -1;
  }
  c = accept(s, NULL, NULL);
  if (c == INVALID_SOCKET) {
    save_errno();
    return -1;
  }
  nfd = netc_alloc(c);
  if (nfd < 0)
    cached_errno = LNX_EMFILE;
  return nfd;
#else
  int cfd = accept(fd, NULL, NULL);
  if (cfd < 0)
    save_errno();
  return cfd;
#endif
}

/* Resolve host to the loopback-adjacent address and connect. hostlen is the
 * PL/I string length; trailing padding is trimmed by pli_to_cstr. */
int netc_connect(int fd, const char *host, int hostlen, int port) {
  char hbuf[256];
  pli_to_cstr(host, hostlen, hbuf, sizeof hbuf);

#ifdef _WIN32
  {
    SOCKET s = netc_lookup(fd);
    struct addrinfo hints, *res = NULL;
    struct sockaddr_in a;
    int gai;
    if (s == INVALID_SOCKET) {
      cached_errno = LNX_EBADF;
      return -1;
    }
    netc_ensure_wsa();
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    gai = getaddrinfo(hbuf, NULL, &hints, &res);
    if (gai != 0) {
      cached_errno = wsa_to_errno(gai);
      return -1;
    }
    memcpy(&a, res->ai_addr, res->ai_addrlen);
    a.sin_port = htons((unsigned short)port);
    freeaddrinfo(res);
    if (connect(s, (struct sockaddr *)&a, (int)sizeof a) != 0) {
      save_errno();
      return -1;
    }
    return 0;
  }
#else
  struct addrinfo hints, *res = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(hbuf, NULL, &hints, &res) != 0) {
    save_errno();
    return -1;
  }

  struct sockaddr_in a;
  memcpy(&a, res->ai_addr, res->ai_addrlen);
  a.sin_port = htons((unsigned short)port);

  freeaddrinfo(res);

  if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) {
    save_errno();
    return -1;
  }
  return 0;
#endif
}

int netc_send(int fd, const char *buf, int len, int flags) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  int n;
  if (s == INVALID_SOCKET) {
    cached_errno = LNX_EBADF;
    return -1;
  }
  n = send(s, buf, len, flags);
  if (n < 0)
    save_errno();
  return n;
#else
  int n = (int)send(fd, buf, (unsigned)len, flags);
  if (n < 0)
    save_errno();
  return n;
#endif
}

int netc_recv(int fd, char *buf, int len, int flags) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  int n;
  if (s == INVALID_SOCKET) {
    cached_errno = LNX_EBADF;
    return -1;
  }
  n = recv(s, buf, len, flags);
  if (n < 0)
    save_errno();
  return n;
#else
  int n = (int)recv(fd, buf, (unsigned)len, flags);
  if (n < 0)
    save_errno();
  return n;
#endif
}

int netc_close(int fd) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  if (s == INVALID_SOCKET) {
    cached_errno = LNX_EBADF;
    return -1;
  }
  netc_drop(fd);
  if (closesocket(s) != 0) {
    save_errno();
    return -1;
  }
  return 0;
#else
  if (close(fd) != 0) {
    save_errno();
    return -1;
  }
  return 0;
#endif
}

int netc_shutdown(int fd, int how) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  if (s == INVALID_SOCKET) {
    cached_errno = LNX_EBADF;
    return -1;
  }
  if (shutdown(s, how) != 0) {
    save_errno();
    return -1;
  }
  return 0;
#else
  if (shutdown(fd, how) != 0) {
    save_errno();
    return -1;
  }
  return 0;
#endif
}

/* SO_RCVTIMEO / SO_SNDTIMEO in milliseconds; -1 to clear. */
int netc_settimeout(int fd, int rto_ms, int wto_ms) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  if (s == INVALID_SOCKET) {
    cached_errno = LNX_EBADF;
    return -1;
  }
  if (rto_ms >= 0) {
    DWORD tv = (DWORD)rto_ms;
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv,
                   (int)sizeof tv) != 0) {
      save_errno();
      return -1;
    }
  }
  if (wto_ms >= 0) {
    DWORD tv = (DWORD)wto_ms;
    if (setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv,
                   (int)sizeof tv) != 0) {
      save_errno();
      return -1;
    }
  }
  return 0;
#else
  struct timeval tv;
  if (rto_ms >= 0) {
    tv.tv_sec = rto_ms / 1000;
    tv.tv_usec = (rto_ms % 1000) * 1000;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) != 0) {
      save_errno();
      return -1;
    }
  }
  if (wto_ms >= 0) {
    tv.tv_sec = wto_ms / 1000;
    tv.tv_usec = (wto_ms % 1000) * 1000;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv) != 0) {
      save_errno();
      return -1;
    }
  }
  return 0;
#endif
}

int netc_setnonblock(int fd, int on) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  u_long mode;
  if (s == INVALID_SOCKET) {
    cached_errno = LNX_EBADF;
    return -1;
  }
  mode = on ? 1UL : 0UL;
  if (ioctlsocket(s, FIONBIO, &mode) != 0) {
    save_errno();
    return -1;
  }
  return 0;
#else
  int fl = fcntl(fd, F_GETFL, 0);
  if (fl < 0) {
    save_errno();
    return -1;
  }
  if (on)
    fl |= O_NONBLOCK;
  else
    fl &= ~O_NONBLOCK;
  if (fcntl(fd, F_SETFL, fl) != 0) {
    save_errno();
    return -1;
  }
  return 0;
#endif
}

/* poll(fd, events, ms). events uses the libnet bitmask (see type_defs.inc):
 * 1=readable, 2=writable; mapped here onto the platform POLLIN/POLLOUT.
 * Returns the number of ready fds, 0 on timeout, -1 on error. */
int netc_poll(int fd, int events, int ms) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  WSAPOLLFD p;
  int r;
  if (s == INVALID_SOCKET) {
    cached_errno = LNX_EBADF;
    return -1;
  }
  p.fd = s;
  p.events = 0;
  if (events & 1)
    p.events |= POLLIN;
  if (events & 2)
    p.events |= POLLOUT;
  p.revents = 0;
  r = WSAPoll(&p, 1, ms);
  if (r == SOCKET_ERROR) {
    save_errno();
    return -1;
  }
  return r;
#else
  struct pollfd p;
  p.fd = fd;
  p.events = 0;
  if (events & 1)
    p.events |= POLLIN;
  if (events & 2)
    p.events |= POLLOUT;
  p.revents = 0;
  int r = poll(&p, 1, ms);
  if (r < 0)
    save_errno();
  return r;
#endif
}

/* Multi-fd poll: poll across many fds at once.
 * fds    - array of file descriptors (input)
 * events - array of event masks (input), one per fd
 * revents- array of returned event masks (output), one per fd
 * nfds   - number of entries in each array
 * ms     - timeout in milliseconds (-1 = infinite)
 * Returns the number of ready fds, 0 on timeout, -1 on error.
 * On success, revents[i] contains the ready events for fds[i]. */
int netc_poll_multi(const int *fds, const int *events, int *revents, int nfds, int ms) {
  if (nfds <= 0) return 0;
#ifdef _WIN32
  {
    WSAPOLLFD *pfds;
    int *idx;
    int i, n = 0, bad = 0, r, ready = 0;
    netc_ensure_wsa();
    pfds = (WSAPOLLFD *)malloc((size_t)nfds * sizeof(WSAPOLLFD));
    idx = (int *)malloc((size_t)nfds * sizeof(int));
    if (!pfds || !idx) {
      free(pfds);
      free(idx);
      cached_errno = LNX_ENOMEM;
      return -1;
    }
    for (i = 0; i < nfds; ++i) {
      SOCKET s = netc_lookup(fds[i]);
      if (s == INVALID_SOCKET) {
        revents[i] = 4; /* error/hup, mirrors POLLNVAL counting as ready */
        bad++;
        continue;
      }
      pfds[n].fd = s;
      pfds[n].events = 0;
      if (events[i] & 1)
        pfds[n].events |= POLLIN;
      if (events[i] & 2)
        pfds[n].events |= POLLOUT;
      pfds[n].revents = 0;
      idx[n] = i;
      n++;
    }
    if (n > 0) {
      r = WSAPoll(pfds, (ULONG)n, ms);
      if (r == SOCKET_ERROR) {
        save_errno();
        free(pfds);
        free(idx);
        return -1;
      }
      for (i = 0; i < n; ++i) {
        int re = 0;
        if (pfds[i].revents & POLLIN)
          re |= 1;
        if (pfds[i].revents & POLLOUT)
          re |= 2;
#if defined(POLLERR) && defined(POLLHUP) && defined(POLLNVAL)
        if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
          re |= 4; /* error/hup */
#else
        if (pfds[i].revents & POLLERR)
          re |= 4;
#endif
        revents[idx[i]] = re;
        if (re)
          ready++;
      }
    }
    free(pfds);
    free(idx);
    return ready + bad;
  }
#else
  struct pollfd *pfds = (struct pollfd *)malloc(nfds * sizeof(struct pollfd));
  if (!pfds) {
    errno = ENOMEM;
    save_errno();
    return -1;
  }
  for (int i = 0; i < nfds; ++i) {
    pfds[i].fd = fds[i];
    pfds[i].events = 0;
    if (events[i] & 1)
      pfds[i].events |= POLLIN;
    if (events[i] & 2)
      pfds[i].events |= POLLOUT;
    pfds[i].revents = 0;
  }
  int r = poll(pfds, nfds, ms);
  if (r >= 0) {
    for (int i = 0; i < nfds; ++i) {
      int re = 0;
      if (pfds[i].revents & POLLIN)
        re |= 1;
      if (pfds[i].revents & POLLOUT)
        re |= 2;
      if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
        re |= 4;  /* error/hup */
      revents[i] = re;
    }
  } else {
    save_errno();
  }
  free(pfds);
  return r;
#endif
}

/* Fill a dotted-quad ip (blank-padded to iplen) and port from a sockaddr. */
static void fill_sockaddr(const struct sockaddr_in *a, char *ip, int iplen,
                          int *port) {
  char tmp[16];
  inet_ntop(AF_INET, &a->sin_addr, tmp, sizeof tmp);
  int i;
  for (i = 0; i < iplen && tmp[i]; ++i)
    ip[i] = tmp[i];
  for (; i < iplen; ++i)
    ip[i] = ' ';
  if (port)
    *port = (int)ntohs(a->sin_port);
}

/* Peer address into ip (16-byte dotted quad) and port. Returns 0 or -1. */
int netc_getpeername(int fd, char *ip, int iplen, int *port) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  struct sockaddr_in a;
  netc_socklen_t alen = (netc_socklen_t)sizeof a;
  if (s == INVALID_SOCKET) {
    cached_errno = LNX_EBADF;
    return -1;
  }
  if (getpeername(s, (struct sockaddr *)&a, &alen) != 0) {
    save_errno();
    return -1;
  }
  fill_sockaddr(&a, ip, iplen, port);
  return 0;
#else
  struct sockaddr_in a;
  socklen_t alen = sizeof a;
  if (getpeername(fd, (struct sockaddr *)&a, &alen) != 0) {
    save_errno();
    return -1;
  }
  fill_sockaddr(&a, ip, iplen, port);
  return 0;
#endif
}

/* Resolve host to a dotted-quad string in ip (blank-padded to iplen).
 * Returns 0 on success, -1 on failure. */
int netc_resolve(const char *host, int hostlen, char *ip, int iplen) {
  char hbuf[256];
  pli_to_cstr(host, hostlen, hbuf, sizeof hbuf);

#ifdef _WIN32
  {
    struct addrinfo hints, *res = NULL;
    struct sockaddr_in *sa;
    int gai;
    netc_ensure_wsa();
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    gai = getaddrinfo(hbuf, NULL, &hints, &res);
    if (gai != 0) {
      cached_errno = wsa_to_errno(gai);
      return -1;
    }
    sa = (struct sockaddr_in *)res->ai_addr;
    fill_sockaddr(sa, ip, iplen, NULL);
    freeaddrinfo(res);
    return 0;
  }
#else
  struct addrinfo hints, *res = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;

  if (getaddrinfo(hbuf, NULL, &hints, &res) != 0) {
    save_errno();
    return -1;
  }

  struct sockaddr_in *sa = (struct sockaddr_in *)res->ai_addr;
  fill_sockaddr(sa, ip, iplen, NULL);

  freeaddrinfo(res);
  return 0;
#endif
}

/* strerror(e) into buf (blank-padded to buflen). */
void netc_strerror(int e, char *buf, int buflen) {
#ifdef _WIN32
  /* e carries POSIX numbers (see wsa_to_errno); describe them directly so
   * socket errors — unknown to the CRT — still produce useful text. */
  const char *s = NULL;
  switch (e) {
    case 0: s = "success"; break;
    case LNX_EPERM: s = "operation not permitted"; break;
    case LNX_ENOENT: s = "no such file or directory"; break;
    case LNX_EINTR: s = "interrupted system call"; break;
    case LNX_EBADF: s = "bad file descriptor"; break;
    case LNX_EAGAIN: s = "resource temporarily unavailable"; break;
    case LNX_ENOMEM: s = "out of memory"; break;
    case LNX_EACCES: s = "permission denied"; break;
    case LNX_EFAULT: s = "bad address"; break;
    case LNX_EINVAL: s = "invalid argument"; break;
    case LNX_EMFILE: s = "too many open files"; break;
    case LNX_EPIPE: s = "broken pipe"; break;
    case LNX_ENOTSOCK: s = "socket operation on non-socket"; break;
    case LNX_EMSGSIZE: s = "message too long"; break;
    case LNX_EADDRINUSE: s = "address already in use"; break;
    case LNX_EADDRNOTAVAIL: s = "cannot assign requested address"; break;
    case LNX_ENETUNREACH: s = "network is unreachable"; break;
    case LNX_ECONNRESET: s = "connection reset by peer"; break;
    case LNX_ENOBUFS: s = "no buffer space available"; break;
    case LNX_ENOTCONN: s = "transport endpoint is not connected"; break;
    case LNX_ETIMEDOUT: s = "connection timed out"; break;
    case LNX_ECONNREFUSED: s = "connection refused"; break;
    case LNX_EHOSTDOWN: s = "host is down"; break;
    case LNX_EHOSTUNREACH: s = "no route to host"; break;
    case LNX_EINPROGRESS: s = "operation now in progress"; break;
    default: s = strerror(e); break;
  }
  if (s == NULL)
    s = "unknown error";
  int i;
  for (i = 0; i < buflen && s[i]; ++i)
    buf[i] = s[i];
  for (; i < buflen; ++i)
    buf[i] = ' ';
#else
  const char *s = strerror(e);
  int i;
  for (i = 0; i < buflen && s[i]; ++i)
    buf[i] = s[i];
  for (; i < buflen; ++i)
    buf[i] = ' ';
#endif
}

/* setsockopt wrapper taking a libnet option code so platform-specific
 * level/optname values stay in C. opt codes (see type_defs.inc):
 *   0 = SO_REUSEADDR, 1 = SO_KEEPALIVE, 2 = TCP_NODELAY
 * value is the int argument (0/1 on/off). Returns 0 or -1. */
int netc_sockopt(int fd, int opt, int value) {
  int level = 0, name = 0;
  switch (opt) {
    case 0: level = SOL_SOCKET; name = SO_REUSEADDR; break;
    case 1: level = SOL_SOCKET; name = SO_KEEPALIVE; break;
    case 2: level = IPPROTO_TCP; name = TCP_NODELAY; break;
    default:
#ifdef _WIN32
      cached_errno = LNX_EINVAL;
#else
      errno = EINVAL;
      save_errno();
#endif
      return -1;
  }
#ifdef _WIN32
  {
    SOCKET s = netc_lookup(fd);
    if (s == INVALID_SOCKET) {
      cached_errno = LNX_EBADF;
      return -1;
    }
    if (setsockopt(s, level, name, (const char *)&value,
                   (int)sizeof value) != 0) {
      save_errno();
      return -1;
    }
    return 0;
  }
#else
  if (setsockopt(fd, level, name, &value, sizeof value) != 0) {
    save_errno();
    return -1;
  }
  return 0;
#endif
}

/* SO_LINGER on/off with a linger timeout in seconds. Returns 0 or -1. */
int netc_setlinger(int fd, int on, int seconds) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  struct linger lg;
  if (s == INVALID_SOCKET) {
    cached_errno = LNX_EBADF;
    return -1;
  }
  lg.l_onoff = on ? 1 : 0;
  lg.l_linger = on ? (u_short)seconds : 0;
  if (setsockopt(s, SOL_SOCKET, SO_LINGER, (const char *)&lg,
                 (int)sizeof lg) != 0) {
    save_errno();
    return -1;
  }
  return 0;
#else
  struct linger lg;
  lg.l_onoff = on ? 1 : 0;
  lg.l_linger = on ? seconds : 0;
  if (setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof lg) != 0) {
    save_errno();
    return -1;
  }
  return 0;
#endif
}

/* Local address (getsockname) into ip/port. Returns 0 or -1. */
int netc_getsockname(int fd, char *ip, int iplen, int *port) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  struct sockaddr_in a;
  netc_socklen_t alen = (netc_socklen_t)sizeof a;
  if (s == INVALID_SOCKET) {
    cached_errno = LNX_EBADF;
    return -1;
  }
  if (getsockname(s, (struct sockaddr *)&a, &alen) != 0) {
    save_errno();
    return -1;
  }
  fill_sockaddr(&a, ip, iplen, port);
  return 0;
#else
  struct sockaddr_in a;
  socklen_t alen = sizeof a;
  if (getsockname(fd, (struct sockaddr *)&a, &alen) != 0) {
    save_errno();
    return -1;
  }
  fill_sockaddr(&a, ip, iplen, port);
  return 0;
#endif
}

/* Nonblocking connect. host is a PL/I string, len bytes; port by value.
 * Returns 0 if the connect completed, -1 with errno EINPROGRESS if it is in
 * progress (caller should poll for writability then check netc_getsockerr),
 * -2 on other errors (errno cached). */
int netc_connect_nb(int fd, const char *host, int hostlen, int port) {
  char hbuf[256];
  pli_to_cstr(host, hostlen, hbuf, sizeof hbuf);

#ifdef _WIN32
  {
    SOCKET s;
    struct addrinfo hints, *res = NULL;
    struct sockaddr_in a;
    int gai, r;
    s = netc_lookup(fd);
    if (s == INVALID_SOCKET) {
      cached_errno = LNX_EBADF;
      return -2;
    }
    netc_ensure_wsa();
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    gai = getaddrinfo(hbuf, NULL, &hints, &res);
    if (gai != 0) {
      cached_errno = wsa_to_errno(gai);
      return -2;
    }
    memcpy(&a, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    a.sin_port = htons((unsigned short)port);
    r = connect(s, (struct sockaddr *)&a, (int)sizeof a);
    if (r != 0) {
      int w = WSAGetLastError();
      /* Windows reports an in-progress nonblocking connect as
       * WSAEWOULDBLOCK rather than WSAEINPROGRESS. */
      if (w == WSAEWOULDBLOCK || w == WSAEINPROGRESS || w == WSAEALREADY) {
        cached_errno = wsa_to_errno(w);
        return -1;
      }
      cached_errno = wsa_to_errno(w);
      return -2;
    }
    return 0;
  }
#else
  struct addrinfo hints, *res = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(hbuf, NULL, &hints, &res) != 0) {
    save_errno();
    return -2;
  }

  struct sockaddr_in a;
  memcpy(&a, res->ai_addr, res->ai_addrlen);
  freeaddrinfo(res);
  a.sin_port = htons((unsigned short)port);

  int r = connect(fd, (struct sockaddr *)&a, sizeof a);
  if (r != 0 && errno == EINPROGRESS) {
    save_errno();
    return -1;
  }
  if (r != 0) {
    save_errno();
    return -2;
  }
  return 0;
#endif
}

/* SO_ERROR for a completed nonblocking connect: 0 if it succeeded, else the
 * error code (also returned via *err). */
int netc_getsockerr(int fd, int *err) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  int soerr = 0;
  netc_socklen_t len = (netc_socklen_t)sizeof soerr;
  if (s == INVALID_SOCKET) {
    cached_errno = LNX_EBADF;
    *err = LNX_EBADF;
    return -1;
  }
  if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&soerr, &len) != 0) {
    save_errno();
    *err = cached_errno;
    return -1;
  }
  /* SO_ERROR surfaces a Winsock code; translate it for the PL/I layer. */
  *err = wsa_to_errno(soerr);
  return 0;
#else
  int soerr = 0;
  socklen_t len = sizeof soerr;
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len) != 0) {
    save_errno();
    *err = errno;
    return -1;
  }
  *err = soerr;
  return 0;
#endif
}

/* UDP send to ip:port. Returns bytes sent or -1. */
int netc_sendto(int fd, const char *buf, int len, int flags, const char *ip,
              int iplen, int port) {
  char hbuf[256];
  pli_to_cstr(ip, iplen, hbuf, sizeof hbuf);

#ifdef _WIN32
  {
    SOCKET s = netc_lookup(fd);
    struct addrinfo hints, *res = NULL;
    struct sockaddr_in a;
    int gai, r;
    if (s == INVALID_SOCKET) {
      cached_errno = LNX_EBADF;
      return -1;
    }
    netc_ensure_wsa();
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    gai = getaddrinfo(hbuf, NULL, &hints, &res);
    if (gai != 0) {
      cached_errno = wsa_to_errno(gai);
      return -1;
    }
    memcpy(&a, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    a.sin_port = htons((unsigned short)port);
    r = sendto(s, buf, len, flags, (struct sockaddr *)&a, (int)sizeof a);
    if (r < 0)
      save_errno();
    return r;
  }
#else
  struct addrinfo hints, *res = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;

  if (getaddrinfo(hbuf, NULL, &hints, &res) != 0) {
    save_errno();
    return -1;
  }

  struct sockaddr_in a;
  memcpy(&a, res->ai_addr, res->ai_addrlen);
  freeaddrinfo(res);
  a.sin_port = htons((unsigned short)port);

  int r = (int)sendto(fd, buf, (unsigned)len, flags,
                      (struct sockaddr *)&a, sizeof a);
  if (r < 0)
    save_errno();
  return r;
#endif
}

/* UDP recv; the sender's address is returned in ip/port. */
int netc_recvfrom(int fd, char *buf, int len, int flags, char *ip, int iplen,
               int *port) {
#ifdef _WIN32
  SOCKET s = netc_lookup(fd);
  struct sockaddr_in a;
  netc_socklen_t alen = (netc_socklen_t)sizeof a;
  int r;
  if (s == INVALID_SOCKET) {
    cached_errno = LNX_EBADF;
    return -1;
  }
  r = recvfrom(s, buf, len, flags, (struct sockaddr *)&a, &alen);
  if (r < 0) {
    save_errno();
    return r;
  }
  fill_sockaddr(&a, ip, iplen, port);
  return r;
#else
  struct sockaddr_in a;
  socklen_t alen = sizeof a;
  int r = (int)recvfrom(fd, buf, (unsigned)len, flags,
                        (struct sockaddr *)&a, &alen);
  if (r < 0) {
    save_errno();
    return r;
  }
  fill_sockaddr(&a, ip, iplen, port);
  return r;
#endif
}

int netc_errno(void) {
  return cached_errno;
}

void netc_clearerr(void) {
  cached_errno = 0;
}
