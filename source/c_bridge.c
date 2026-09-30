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
 */

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

/* Thread-local errno snapshot so PL/I can read it without clobbering errno. */
static _Thread_local int cached_errno = 0;

static void save_errno(void) {
  cached_errno = errno;
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
  int fd = socket(family, type, proto);
  if (fd < 0)
    save_errno();
  return fd;
}

int netc_bind(int fd, unsigned int port) {
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
}

int netc_listen(int fd, int backlog) {
  if (listen(fd, backlog) != 0) {
    save_errno();
    return -1;
  }
  return 0;
}

int netc_accept(int fd) {
  int cfd = accept(fd, NULL, NULL);
  if (cfd < 0)
    save_errno();
  return cfd;
}

/* Resolve host to the loopback-adjacent address and connect. hostlen is the
 * PL/I string length; trailing padding is trimmed by pli_to_cstr. */
int netc_connect(int fd, const char *host, int hostlen, int port) {
  char hbuf[256];
  pli_to_cstr(host, hostlen, hbuf, sizeof hbuf);

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
}

int netc_send(int fd, const char *buf, int len, int flags) {
  int n = (int)send(fd, buf, (unsigned)len, flags);
  if (n < 0)
    save_errno();
  return n;
}

int netc_recv(int fd, char *buf, int len, int flags) {
  int n = (int)recv(fd, buf, (unsigned)len, flags);
  if (n < 0)
    save_errno();
  return n;
}

int netc_close(int fd) {
  if (close(fd) != 0) {
    save_errno();
    return -1;
  }
  return 0;
}

int netc_shutdown(int fd, int how) {
  if (shutdown(fd, how) != 0) {
    save_errno();
    return -1;
  }
  return 0;
}

/* SO_RCVTIMEO / SO_SNDTIMEO in milliseconds; -1 to clear. */
int netc_settimeout(int fd, int rto_ms, int wto_ms) {
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
}

int netc_setnonblock(int fd, int on) {
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
}

/* poll(fd, events, ms). events uses the libnet bitmask (see type_defs.inc):
 * 1=readable, 2=writable; mapped here onto the platform POLLIN/POLLOUT.
 * Returns the number of ready fds, 0 on timeout, -1 on error. */
int netc_poll(int fd, int events, int ms) {
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
  struct sockaddr_in a;
  socklen_t alen = sizeof a;
  if (getpeername(fd, (struct sockaddr *)&a, &alen) != 0) {
    save_errno();
    return -1;
  }
  fill_sockaddr(&a, ip, iplen, port);
  return 0;
}

/* Resolve host to a dotted-quad string in ip (blank-padded to iplen).
 * Returns 0 on success, -1 on failure. */
int netc_resolve(const char *host, int hostlen, char *ip, int iplen) {
  char hbuf[256];
  pli_to_cstr(host, hostlen, hbuf, sizeof hbuf);

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
}

/* strerror(e) into buf (blank-padded to buflen). */
void netc_strerror(int e, char *buf, int buflen) {
  const char *s = strerror(e);
  int i;
  for (i = 0; i < buflen && s[i]; ++i)
    buf[i] = s[i];
  for (; i < buflen; ++i)
    buf[i] = ' ';
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
    default: errno = EINVAL; save_errno(); return -1;
  }
  if (setsockopt(fd, level, name, &value, sizeof value) != 0) {
    save_errno();
    return -1;
  }
  return 0;
}

/* SO_LINGER on/off with a linger timeout in seconds. Returns 0 or -1. */
int netc_setlinger(int fd, int on, int seconds) {
  struct linger lg;
  lg.l_onoff = on ? 1 : 0;
  lg.l_linger = on ? seconds : 0;
  if (setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof lg) != 0) {
    save_errno();
    return -1;
  }
  return 0;
}

/* Local address (getsockname) into ip/port. Returns 0 or -1. */
int netc_getsockname(int fd, char *ip, int iplen, int *port) {
  struct sockaddr_in a;
  socklen_t alen = sizeof a;
  if (getsockname(fd, (struct sockaddr *)&a, &alen) != 0) {
    save_errno();
    return -1;
  }
  fill_sockaddr(&a, ip, iplen, port);
  return 0;
}

/* Nonblocking connect. host is a PL/I string, len bytes; port by value.
 * Returns 0 if the connect completed, -1 with errno EINPROGRESS if it is in
 * progress (caller should poll for writability then check netc_getsockerr),
 * -2 on other errors (errno cached). */
int netc_connect_nb(int fd, const char *host, int hostlen, int port) {
  char hbuf[256];
  pli_to_cstr(host, hostlen, hbuf, sizeof hbuf);

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
}

/* SO_ERROR for a completed nonblocking connect: 0 if it succeeded, else the
 * error code (also returned via *err). */
int netc_getsockerr(int fd, int *err) {
  int soerr = 0;
  socklen_t len = sizeof soerr;
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len) != 0) {
    save_errno();
    *err = errno;
    return -1;
  }
  *err = soerr;
  return 0;
}

/* UDP send to ip:port. Returns bytes sent or -1. */
int netc_sendto(int fd, const char *buf, int len, int flags, const char *ip,
              int iplen, int port) {
  char hbuf[256];
  pli_to_cstr(ip, iplen, hbuf, sizeof hbuf);

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
}

/* UDP recv; the sender's address is returned in ip/port. */
int netc_recvfrom(int fd, char *buf, int len, int flags, char *ip, int iplen,
               int *port) {
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
}

int netc_errno(void) {
  return cached_errno;
}

void netc_clearerr(void) {
  cached_errno = 0;
}
