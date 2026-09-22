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
 *     a thread-local cache read by s_errno().
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* Thread-local errno snapshot so PL/I can read it without clobbering errno. */
static __thread int cached_errno = 0;

static void save_errno(void) {
  cached_errno = errno;
}

int s_socket(int family, int type, int proto) {
  int fd = socket(family, type, proto);
  if (fd < 0)
    save_errno();
  return fd;
}

int s_bind(int fd, unsigned int port) {
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

int s_listen(int fd, int backlog) {
  if (listen(fd, backlog) != 0) {
    save_errno();
    return -1;
  }
  return 0;
}

int s_accept(int fd) {
  int cfd = accept(fd, NULL, NULL);
  if (cfd < 0)
    save_errno();
  return cfd;
}

/* Resolve host to the loopback-adjacent address and connect. hostlen is the
 * PL/I char length (may be padded with blanks); we NUL-terminate a copy. */
int s_connect(int fd, const char *host, int hostlen, int port) {
  char hbuf[256];
  int n = hostlen < (int)sizeof(hbuf) - 1 ? hostlen : (int)sizeof(hbuf) - 1;
  int i;
  for (i = 0; i < n; ++i)
    hbuf[i] = (host[i] == ' ' || host[i] == '\t') ? '\0' : host[i];
  hbuf[n] = '\0';

  struct sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons((unsigned short)port);

  struct in_addr addr;
  if (inet_pton(AF_INET, hbuf, &addr) == 1) {
    a.sin_addr = addr;
  } else {
    struct hostent *he = gethostbyname(hbuf);
    if (!he || !he->h_addr_list[0]) {
      save_errno();
      return -1;
    }
    memcpy(&a.sin_addr, he->h_addr_list[0], he->h_length);
  }

  if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) {
    save_errno();
    return -1;
  }
  return 0;
}

int s_send(int fd, const char *buf, int len, int flags) {
  int n = (int)send(fd, buf, (unsigned)len, flags);
  if (n < 0)
    save_errno();
  return n;
}

int s_recv(int fd, char *buf, int len, int flags) {
  int n = (int)recv(fd, buf, (unsigned)len, flags);
  if (n < 0)
    save_errno();
  return n;
}

int s_close(int fd) {
  if (close(fd) != 0) {
    save_errno();
    return -1;
  }
  return 0;
}

int s_shutdown(int fd, int how) {
  if (shutdown(fd, how) != 0) {
    save_errno();
    return -1;
  }
  return 0;
}

/* SO_RCVTIMEO / SO_SNDTIMEO in milliseconds; -1 to clear. */
int s_settimeout(int fd, int rto_ms, int wto_ms) {
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

int s_setnonblock(int fd, int on) {
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

/* poll(fd, events, ms). events bitmask: 1=POLLIN, 2=POLLOUT.
 * Returns the number of ready fds, 0 on timeout, -1 on error. */
int s_poll(int fd, int events, int ms) {
  struct pollfd p;
  p.fd = fd;
  p.events = (short)events;
  p.revents = 0;
  int r = poll(&p, 1, ms);
  if (r < 0)
    save_errno();
  return r;
}

/* Peer address into ip (16-byte dotted quad) and port. Returns 0 or -1. */
int s_getpeername(int fd, char *ip, int iplen, int *port) {
  struct sockaddr_in a;
  socklen_t alen = sizeof a;
  if (getpeername(fd, (struct sockaddr *)&a, &alen) != 0) {
    save_errno();
    return -1;
  }
  char tmp[16];
  inet_ntop(AF_INET, &a.sin_addr, tmp, sizeof tmp);
  int i;
  for (i = 0; i < iplen && tmp[i]; ++i)
    ip[i] = tmp[i];
  for (; i < iplen; ++i)
    ip[i] = ' ';
  *port = (int)ntohs(a.sin_port);
  return 0;
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

/* Resolve host (blank-padded, len bytes) to a dotted-quad string in ip
 * (blank-padded to iplen). Returns 0 on success, -1 on failure. */
int s_resolve(const char *host, int hostlen, char *ip, int iplen) {
  char hbuf[256];
  int n = hostlen < (int)sizeof(hbuf) - 1 ? hostlen : (int)sizeof(hbuf) - 1;
  int i;
  for (i = 0; i < n; ++i)
    hbuf[i] = (host[i] == ' ' || host[i] == '\t') ? '\0' : host[i];
  hbuf[n] = '\0';

  struct in_addr addr;
  if (inet_pton(AF_INET, hbuf, &addr) == 1) {
    /* already numeric */
  } else {
    struct hostent *he = gethostbyname(hbuf);
    if (!he || !he->h_addr_list[0]) {
      save_errno();
      return -1;
    }
    memcpy(&addr, he->h_addr_list[0], he->h_length);
  }
  char tmp[16];
  inet_ntop(AF_INET, &addr, tmp, sizeof tmp);
  for (i = 0; i < iplen && tmp[i]; ++i)
    ip[i] = tmp[i];
  for (; i < iplen; ++i)
    ip[i] = ' ';
  return 0;
}

/* strerror(e) into buf (blank-padded to buflen). */
void s_strerror(int e, char *buf, int buflen) {
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
int s_sockopt(int fd, int opt, int value) {
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
int s_setlinger(int fd, int on, int seconds) {
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
int s_getsockname(int fd, char *ip, int iplen, int *port) {
  struct sockaddr_in a;
  socklen_t alen = sizeof a;
  if (getsockname(fd, (struct sockaddr *)&a, &alen) != 0) {
    save_errno();
    return -1;
  }
  fill_sockaddr(&a, ip, iplen, port);
  return 0;
}

/* Nonblocking connect. host is blank-padded, len bytes; port by value.
 * Returns 0 if the connect completed, -1 with errno EINPROGRESS if it is in
 * progress (caller should poll for writability then check s_getsockerr),
 * -2 on other errors (errno cached). */
int s_connect_nb(int fd, const char *host, int hostlen, int port) {
  char hbuf[256];
  int n = hostlen < (int)sizeof(hbuf) - 1 ? hostlen : (int)sizeof(hbuf) - 1;
  int i;
  for (i = 0; i < n; ++i)
    hbuf[i] = (host[i] == ' ' || host[i] == '\t') ? '\0' : host[i];
  hbuf[n] = '\0';

  struct sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons((unsigned short)port);

  struct in_addr addr;
  if (inet_pton(AF_INET, hbuf, &addr) == 1) {
    a.sin_addr = addr;
  } else {
    struct hostent *he = gethostbyname(hbuf);
    if (!he || !he->h_addr_list[0]) {
      save_errno();
      return -2;
    }
    memcpy(&a.sin_addr, he->h_addr_list[0], he->h_length);
  }

  int r = connect(fd, (struct sockaddr *)&a, sizeof a);
  if (r != 0 && errno == EINPROGRESS) {
    cached_errno = EINPROGRESS;
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
int s_getsockerr(int fd, int *err) {
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
int s_sendto(int fd, const char *buf, int len, int flags, const char *ip,
             int iplen, int port) {
  char hbuf[256];
  int n = iplen < (int)sizeof(hbuf) - 1 ? iplen : (int)sizeof(hbuf) - 1;
  int i;
  for (i = 0; i < n; ++i)
    hbuf[i] = (ip[i] == ' ' || ip[i] == '\t') ? '\0' : ip[i];
  hbuf[n] = '\0';

  struct sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons((unsigned short)port);
  if (inet_pton(AF_INET, hbuf, &a.sin_addr) != 1) {
    struct hostent *he = gethostbyname(hbuf);
    if (!he || !he->h_addr_list[0]) {
      save_errno();
      return -1;
    }
    memcpy(&a.sin_addr, he->h_addr_list[0], he->h_length);
  }
  int r = (int)sendto(fd, buf, (unsigned)len, flags,
                      (struct sockaddr *)&a, sizeof a);
  if (r < 0)
    save_errno();
  return r;
}

/* UDP recv; the sender's address is returned in ip/port. */
int s_recvfrom(int fd, char *buf, int len, int flags, char *ip, int iplen,
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

int s_errno(void) {
  return cached_errno;
}

void s_clearerr(void) {
  cached_errno = 0;
}
