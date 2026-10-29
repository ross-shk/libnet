/* c_bridge.c - thin C bridge for libnet (Windows-only).
 *
 * The ONLY C in the whole library. Each function is a thin Winsock2 wrapper
 * reached from PL/I through pli-llvm's by-value FFI
 * (OPTIONS(LINKAGE(SYSTEM)) / OPTIONS(BYVALUE)). No buffering, no policy,
 * no DNS result parsing, no option-code mapping: all logic above this
 * file lives in PL/I (source/net.pli, source/sock.pli).
 *
 * On POSIX (Linux/macOS) no C source is compiled: the c_* procedures
 * declared in include/c_bridge.inc resolve to libc directly via
 * pli-llvm's external('socket') bindings. On Windows, this file provides
 * the POSIX-named symbols wrapping Winsock2.
 *
 * Winsock functions are resolved via GetProcAddress to avoid symbol
 * name collisions: our wrapper functions share the same names that
 * pli-llvm's external() looks up (socket, close, poll, fcntl, ...),
 * but we cannot also declare them via #include <winsock2.h>.
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
#include <stdlib.h>
#include <string.h>

#undef errno

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

#define F_GETFL 3
#define F_SETFL 4
#define O_NONBLOCK 1

#define LIBNET_POLLIN  1
#define LIBNET_POLLOUT 2
#define LIBNET_POLLERR 4

#define NETN_MAX_FDS 1024

int errno = 0;

static SOCKET ws_table[NETN_MAX_FDS];
static int ws_used[NETN_MAX_FDS];
static int ws_ready = 0;

typedef SOCKET (WSAAPI *fn_socket_t)(int, int, int);
typedef int (WSAAPI *fn_closesocket_t)(SOCKET);
typedef int (WSAAPI *fn_shutdown_t)(SOCKET, int);
typedef int (WSAAPI *fn_bind_t)(SOCKET, const struct sockaddr *, int);
typedef int (WSAAPI *fn_listen_t)(SOCKET, int);
typedef SOCKET (WSAAPI *fn_accept_t)(SOCKET, struct sockaddr *, int *);
typedef int (WSAAPI *fn_connect_t)(SOCKET, const struct sockaddr *, int);
typedef int (WSAAPI *fn_send_t)(SOCKET, const char *, int, int);
typedef int (WSAAPI *fn_recv_t)(SOCKET, char *, int, int);
typedef int (WSAAPI *fn_sendto_t)(SOCKET, const char *, int, int,
    const struct sockaddr *, int);
typedef int (WSAAPI *fn_recvfrom_t)(SOCKET, char *, int, int,
    struct sockaddr *, int *);
typedef int (WSAAPI *fn_setsockopt_t)(SOCKET, int, int, const char *, int);
typedef int (WSAAPI *fn_getsockopt_t)(SOCKET, int, int, char *, int *);
typedef int (WSAAPI *fn_getpeername_t)(SOCKET, struct sockaddr *, int *);
typedef int (WSAAPI *fn_getsockname_t)(SOCKET, struct sockaddr *, int *);
typedef int (WSAAPI *fn_ioctlsocket_t)(SOCKET, long, unsigned long *);
typedef int (WSAAPI *fn_WSAPoll_t)(void *, unsigned long, int);
typedef int (WSAAPI *fn_getaddrinfo_t)(const char *, const char *,
    const struct addrinfo *, struct addrinfo **);
typedef void (WSAAPI *fn_freeaddrinfo_t)(struct addrinfo *);

static HMODULE ws_mod = NULL;
static fn_socket_t        p_socket        = NULL;
static fn_closesocket_t   p_closesocket   = NULL;
static fn_shutdown_t      p_shutdown      = NULL;
static fn_bind_t          p_bind          = NULL;
static fn_listen_t        p_listen        = NULL;
static fn_accept_t        p_accept        = NULL;
static fn_connect_t       p_connect       = NULL;
static fn_send_t          p_send          = NULL;
static fn_recv_t          p_recv          = NULL;
static fn_sendto_t        p_sendto        = NULL;
static fn_recvfrom_t      p_recvfrom      = NULL;
static fn_setsockopt_t    p_setsockopt    = NULL;
static fn_getsockopt_t    p_getsockopt    = NULL;
static fn_getpeername_t   p_getpeername   = NULL;
static fn_getsockname_t   p_getsockname   = NULL;
static fn_ioctlsocket_t   p_ioctlsocket   = NULL;
static fn_WSAPoll_t       p_WSAPoll       = NULL;
static fn_getaddrinfo_t   p_getaddrinfo   = NULL;
static fn_freeaddrinfo_t  p_freeaddrinfo  = NULL;

static int ws_ensure(void) {
  WSADATA wsa;
  if (ws_ready) return 0;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    errno = LNX_ENOBUFS;
    return -1;
  }
  ws_mod = LoadLibraryA("ws2_32.dll");
  if (!ws_mod) { errno = LNX_ENOBUFS; return -1; }
  p_socket         = (fn_socket_t)GetProcAddress(ws_mod, "socket");
  p_closesocket    = (fn_closesocket_t)GetProcAddress(ws_mod, "closesocket");
  p_shutdown       = (fn_shutdown_t)GetProcAddress(ws_mod, "shutdown");
  p_bind           = (fn_bind_t)GetProcAddress(ws_mod, "bind");
  p_listen         = (fn_listen_t)GetProcAddress(ws_mod, "listen");
  p_accept         = (fn_accept_t)GetProcAddress(ws_mod, "accept");
  p_connect        = (fn_connect_t)GetProcAddress(ws_mod, "connect");
  p_send           = (fn_send_t)GetProcAddress(ws_mod, "send");
  p_recv           = (fn_recv_t)GetProcAddress(ws_mod, "recv");
  p_sendto         = (fn_sendto_t)GetProcAddress(ws_mod, "sendto");
  p_recvfrom       = (fn_recvfrom_t)GetProcAddress(ws_mod, "recvfrom");
  p_setsockopt     = (fn_setsockopt_t)GetProcAddress(ws_mod, "setsockopt");
  p_getsockopt     = (fn_getsockopt_t)GetProcAddress(ws_mod, "getsockopt");
  p_getpeername    = (fn_getpeername_t)GetProcAddress(ws_mod, "getpeername");
  p_getsockname    = (fn_getsockname_t)GetProcAddress(ws_mod, "getsockname");
  p_ioctlsocket    = (fn_ioctlsocket_t)GetProcAddress(ws_mod, "ioctlsocket");
  p_WSAPoll        = (fn_WSAPoll_t)GetProcAddress(ws_mod, "WSAPoll");
  p_getaddrinfo    = (fn_getaddrinfo_t)GetProcAddress(ws_mod, "getaddrinfo");
  p_freeaddrinfo   = (fn_freeaddrinfo_t)GetProcAddress(ws_mod, "freeaddrinfo");
  if (!p_socket || !p_closesocket || !p_shutdown || !p_bind ||
      !p_listen || !p_accept || !p_connect || !p_send || !p_recv ||
      !p_sendto || !p_recvfrom || !p_setsockopt || !p_getsockopt ||
      !p_getpeername || !p_getsockname || !p_ioctlsocket ||
      !p_WSAPoll || !p_getaddrinfo || !p_freeaddrinfo) {
    errno = LNX_ENOBUFS;
    return -1;
  }
  ws_ready = 1;
  return 0;
}

static SOCKET ws_lookup(int fd) {
  int i = fd - 1;
  if (i < 0 || i >= NETN_MAX_FDS || !ws_used[i])
    return INVALID_SOCKET;
  return ws_table[i];
}

static int ws_alloc(SOCKET s) {
  int i;
  for (i = 0; i < NETN_MAX_FDS; ++i) {
    if (!ws_used[i]) {
      ws_used[i] = 1;
      ws_table[i] = s;
      return i + 1;
    }
  }
  p_closesocket(s);
  return -1;
}

static void ws_drop(int fd) {
  int i = fd - 1;
  if (i >= 0 && i < NETN_MAX_FDS) {
    ws_used[i] = 0;
    ws_table[i] = INVALID_SOCKET;
  }
}

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

int socket(int family, int type, int proto) {
  SOCKET s;
  int fd;
  if (ws_ensure() < 0) return -1;
  s = p_socket(family, type, proto);
  if (s == INVALID_SOCKET) { errno = wsa_to_errno(WSAGetLastError()); return -1; }
  fd = ws_alloc(s);
  if (fd < 0) { errno = LNX_EMFILE; return -1; }
  return fd;
}

int close(int fd) {
  SOCKET s = ws_lookup(fd);
  if (s == INVALID_SOCKET) { errno = LNX_EBADF; return -1; }
  ws_drop(fd);
  if (p_closesocket(s) != 0) { errno = wsa_to_errno(WSAGetLastError()); return -1; }
  return 0;
}

int shutdown(int fd, int how) {
  SOCKET s = ws_lookup(fd);
  if (s == INVALID_SOCKET) { errno = LNX_EBADF; return -1; }
  if (p_shutdown(s, how) != 0) { errno = wsa_to_errno(WSAGetLastError()); return -1; }
  return 0;
}

int bind(int fd, const char *addr_buf, int addrlen) {
  SOCKET s = ws_lookup(fd);
  if (s == INVALID_SOCKET) { errno = LNX_EBADF; return -1; }
  if (p_bind(s, (const struct sockaddr *)addr_buf, addrlen) != 0) {
    errno = wsa_to_errno(WSAGetLastError()); return -1;
  }
  return 0;
}

int listen(int fd, int backlog) {
  SOCKET s = ws_lookup(fd);
  if (s == INVALID_SOCKET) { errno = LNX_EBADF; return -1; }
  if (p_listen(s, backlog) != 0) { errno = wsa_to_errno(WSAGetLastError()); return -1; }
  return 0;
}

int accept(int fd, void *addr, int *addrlen) {
  SOCKET s = ws_lookup(fd);
  SOCKET c;
  int nfd;
  if (s == INVALID_SOCKET) { errno = LNX_EBADF; return -1; }
  c = p_accept(s, addr ? (struct sockaddr *)addr : NULL,
               addrlen ? (int *)addrlen : NULL);
  if (c == INVALID_SOCKET) { errno = wsa_to_errno(WSAGetLastError()); return -1; }
  nfd = ws_alloc(c);
  if (nfd < 0) { errno = LNX_EMFILE; return -1; }
  return nfd;
}

int connect(int fd, const char *addr_buf, int addrlen) {
  SOCKET s = ws_lookup(fd);
  if (s == INVALID_SOCKET) { errno = LNX_EBADF; return -1; }
  if (p_connect(s, (const struct sockaddr *)addr_buf, addrlen) != 0) {
    errno = wsa_to_errno(WSAGetLastError()); return -1;
  }
  return 0;
}

int send(int fd, const char *buf, int len, int flags) {
  SOCKET s = ws_lookup(fd);
  int n;
  if (s == INVALID_SOCKET) { errno = LNX_EBADF; return -1; }
  n = p_send(s, buf, len, flags);
  if (n < 0) { errno = wsa_to_errno(WSAGetLastError()); }
  return n;
}

int recv(int fd, char *buf, int len, int flags) {
  SOCKET s = ws_lookup(fd);
  int n;
  if (s == INVALID_SOCKET) { errno = LNX_EBADF; return -1; }
  n = p_recv(s, buf, len, flags);
  if (n < 0) { errno = wsa_to_errno(WSAGetLastError()); }
  return n;
}

int sendto(int fd, const char *buf, int len, int flags,
           const char *addr_buf, int addrlen) {
  SOCKET s = ws_lookup(fd);
  int r;
  if (s == INVALID_SOCKET) { errno = LNX_EBADF; return -1; }
  r = p_sendto(s, buf, len, flags,
               (const struct sockaddr *)addr_buf, addrlen);
  if (r < 0) { errno = wsa_to_errno(WSAGetLastError()); }
  return r;
}

int recvfrom(int fd, char *buf, int len, int flags,
             char *addr_buf, int *addrlen) {
  SOCKET s = ws_lookup(fd);
  int alen;
  int r;
  if (s == INVALID_SOCKET) { errno = LNX_EBADF; return -1; }
  alen = addrlen ? *addrlen : 0;
  r = p_recvfrom(s, buf, len, flags,
                 (struct sockaddr *)addr_buf, &alen);
  if (addrlen) *addrlen = alen;
  if (r < 0) { errno = wsa_to_errno(WSAGetLastError()); }
  return r;
}

int setsockopt(int fd, int level, int optname,
               const void *value, int optlen) {
  SOCKET s = ws_lookup(fd);
  if (s == INVALID_SOCKET) { errno = LNX_EBADF; return -1; }
  if (p_setsockopt(s, level, optname, (const char *)value, optlen) != 0) {
    errno = wsa_to_errno(WSAGetLastError()); return -1;
  }
  return 0;
}

int getsockopt(int fd, int level, int optname,
               void *value, int *optlen) {
  SOCKET s = ws_lookup(fd);
  if (s == INVALID_SOCKET) { errno = LNX_EBADF; return -1; }
  if (p_getsockopt(s, level, optname, (char *)value, (int *)optlen) != 0) {
    errno = wsa_to_errno(WSAGetLastError()); return -1;
  }
  return 0;
}

int getpeername(int fd, char *addr_buf, int *addrlen) {
  SOCKET s = ws_lookup(fd);
  int alen;
  if (s == INVALID_SOCKET) { errno = LNX_EBADF; return -1; }
  alen = addrlen ? *addrlen : 0;
  if (p_getpeername(s, (struct sockaddr *)addr_buf, &alen) != 0) {
    errno = wsa_to_errno(WSAGetLastError()); return -1;
  }
  if (addrlen) *addrlen = alen;
  return 0;
}

int getsockname(int fd, char *addr_buf, int *addrlen) {
  SOCKET s = ws_lookup(fd);
  int alen;
  if (s == INVALID_SOCKET) { errno = LNX_EBADF; return -1; }
  alen = addrlen ? *addrlen : 0;
  if (p_getsockname(s, (struct sockaddr *)addr_buf, &alen) != 0) {
    errno = wsa_to_errno(WSAGetLastError()); return -1;
  }
  if (addrlen) *addrlen = alen;
  return 0;
}

int fcntl(int fd, int cmd, int arg) {
  SOCKET s = ws_lookup(fd);
  unsigned long mode;
  if (s == INVALID_SOCKET) { errno = LNX_EBADF; return -1; }
  switch (cmd) {
    case F_GETFL:
      mode = 0;
      if (p_ioctlsocket(s, FIONBIO, &mode) != 0) {
        errno = wsa_to_errno(WSAGetLastError());
        return -1;
      }
      return mode ? O_NONBLOCK : 0;
    case F_SETFL:
      mode = (arg & O_NONBLOCK) ? 1UL : 0UL;
      if (p_ioctlsocket(s, FIONBIO, &mode) != 0) {
        errno = wsa_to_errno(WSAGetLastError());
        return -1;
      }
      return 0;
    default:
      errno = LNX_EINVAL;
      return -1;
  }
}

struct pli_pollfd {
  int fd;
  short events;
  short revents;
};

int poll(void *fds, int nfds, int ms) {
  struct pli_pollfd *pfds = (struct pli_pollfd *)fds;
  WSAPOLLFD *wpfds;
  int *idx;
  int i, nvalid = 0, bad = 0, r, ready = 0;
  if (nfds <= 0) return 0;
  if (ws_ensure() < 0) return -1;
  wpfds = (WSAPOLLFD *)malloc((size_t)nfds * sizeof(WSAPOLLFD));
  idx = (int *)malloc((size_t)nfds * sizeof(int));
  if (!wpfds || !idx) { free(wpfds); free(idx); errno = LNX_ENOMEM; return -1; }
  for (i = 0; i < nfds; ++i) {
    SOCKET s = ws_lookup(pfds[i].fd);
    if (s == INVALID_SOCKET) { pfds[i].revents = (short)(LIBNET_POLLERR); bad++; continue; }
    idx[nvalid] = i;
    wpfds[nvalid].fd = s;
    wpfds[nvalid].events = 0;
    if (pfds[i].events & LIBNET_POLLIN)  wpfds[nvalid].events |= WS_POLLIN;
    if (pfds[i].events & LIBNET_POLLOUT) wpfds[nvalid].events |= WS_POLLOUT;
    wpfds[nvalid].revents = 0;
    nvalid++;
  }
  if (nvalid > 0) {
    r = p_WSAPoll(wpfds, (unsigned long)nvalid, ms);
    if (r == SOCKET_ERROR) {
      errno = wsa_to_errno(WSAGetLastError());
      free(wpfds); free(idx);
      return -1;
    }
    for (i = 0; i < nvalid; ++i) {
      short ev = 0;
      if (wpfds[i].revents & WS_POLLIN)  ev |= (short)LIBNET_POLLIN;
      if (wpfds[i].revents & WS_POLLOUT) ev |= (short)LIBNET_POLLOUT;
      if (wpfds[i].revents & (WS_POLLERR | WS_POLLHUP | WS_POLLNVAL))
        ev |= (short)LIBNET_POLLERR;
      pfds[idx[i]].revents = ev;
      if (ev) ready++;
    }
  }
  free(wpfds); free(idx);
  return ready + bad;
}

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res) {
  if (ws_ensure() < 0) return EAI_FAIL;
  return p_getaddrinfo(node, service, hints, res);
}

void freeaddrinfo(struct addrinfo *res) {
  if (!res) return;
  if (ws_ensure() < 0) return;
  p_freeaddrinfo(res);
}

#else /* _WIN32 */
/* POSIX: c_bridge.c is not compiled. All c_* symbols resolve to libc. */
#endif
