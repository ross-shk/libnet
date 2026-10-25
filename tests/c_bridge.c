/* tests/c_bridge.c — C regression test for the thin C bridge.
 *
 * Exercises the syscall wrappers that make up source/c_bridge.c, compiled
 * directly (no pli-llvm / PL/I needed, so this runs today). Prints PASS on
 * success and FAIL on the first problem; exits non-zero on failure.
 *
 * Build + run:
 *   cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define NET_EAGAIN 35
#define NET_EINPROGRESS 36
#define NET_ECONNREFUSED 61
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#define NET_EAGAIN EAGAIN
#define NET_EINPROGRESS EINPROGRESS
#define NET_ECONNREFUSED ECONNREFUSED
#endif

/* Libnet poll mask (see type_defs.inc): 1=readable, 2=writable. */
#define NET_POLLIN 1
#define NET_POLLOUT 2

/* New minimal C bridge surface (prototypes mirror include/c_bridge.inc). */
int  netc_socket(int, int, int);
int  netc_close(int);
int  netc_shutdown(int, int);
int  netc_bind(int, const char *, int);
int  netc_listen(int, int);
int  netc_accept(int);
int  netc_connect(int, const char *, int);
int  netc_send(int, const char *, int, int);
int  netc_recv(int, char *, int, int);
int  netc_sendto(int, const char *, int, int, const char *, int);
int  netc_recvfrom(int, char *, int, int, char *, int *);
int  netc_setsockopt(int, int, int, const void *, int);
int  netc_getsockopt(int, int, int, void *, int *);
int  netc_getpeername(int, char *, int *);
int  netc_getsockname(int, char *, int *);
int  netc_setnonblock(int, int);
int  netc_poll(int, int, int);
int  netc_poll_multi(const int *, const int *, int *, int, int);
int  netc_getaddrinfo(const char *, int, int, char *);
int  netc_errno(void);
void netc_clearerr(void);

/* --- sockaddr_in helpers (mirrors what PL/I does in net.pli) --- */
#define SOCKADDR_IN_SIZE ((int)sizeof(struct sockaddr_in))

static void build_sockaddr_in(char *buf, unsigned ip_host,
                              unsigned short port_host) {
  struct sockaddr_in sa;

  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port_host);
  sa.sin_addr.s_addr = htonl(ip_host);
#ifdef __APPLE__
  sa.sin_len = (unsigned char)sizeof(sa);
#endif
  memcpy(buf, &sa, sizeof(sa));
}

static unsigned parse_sin_addr(const char *buf) {
  return ((unsigned)(unsigned char)buf[4] << 24) |
         ((unsigned)(unsigned char)buf[5] << 16) |
         ((unsigned)(unsigned char)buf[6] << 8)  |
         (unsigned)(unsigned char)buf[7];
}

static unsigned short parse_sin_port(const char *buf) {
  return ((unsigned short)(unsigned char)buf[2] << 8) |
         (unsigned short)(unsigned char)buf[3];
}

static int fails = 0;
static void check(int cond, const char *what) {
  if (cond) {
    printf("  PASS %s\n", what);
  } else {
    printf("  FAIL %s\n", what);
    fails++;
  }
}

int main(void) {
  char addr_buf[SOCKADDR_IN_SIZE];
  char buf[64] = {0};
  int  r, n, fd, fd2, a, b;
  int  alen;

  printf("resolve\n");
  netc_clearerr();
  r = netc_getaddrinfo("127.0.0.1", 9, 9, addr_buf);
  check(r == 0, "getaddrinfo 127.0.0.1");
  if (r == 0) {
    unsigned ip = parse_sin_addr(addr_buf);
    check(ip == 0x7F000001u, "resolved IP is 127.0.0.1");
    check(parse_sin_port(addr_buf) == 0x0009, "resolved port is 9");
  }

  printf("socket options\n");
  fd = netc_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  check(fd >= 0, "socket");
  netc_clearerr();
  {
    int val = 1;
    r = netc_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    check(r == 0, "setsockopt reuseaddr");
  }
  netc_clearerr();
  {
    struct linger lg;
    lg.l_onoff = 1;
    lg.l_linger = 5;
    r = netc_setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
    check(r == 0, "setsockopt linger");
  }

  printf("local address\n");
  netc_clearerr();
  {
    build_sockaddr_in(addr_buf, 0, 0); /* INADDR_ANY, ephemeral */
    r = netc_bind(fd, addr_buf, SOCKADDR_IN_SIZE);
    check(r == 0, "bind ephemeral");
  }
  if (r == 0) {
    netc_clearerr();
    alen = SOCKADDR_IN_SIZE;
    r = netc_getsockname(fd, addr_buf, &alen);
    check(r == 0 && alen == SOCKADDR_IN_SIZE, "getsockname");
    if (r == 0) {
      unsigned short port = parse_sin_port(addr_buf);
      check(port > 0, "getsockname assigned port > 0");
    }
  }

  printf("UDP datagram loopback\n");
  netc_clearerr();
  r = netc_getaddrinfo("127.0.0.1", 9, 9, addr_buf);
  check(r == 0, "getaddrinfo for udp sendto");
  a = netc_socket(AF_INET, SOCK_DGRAM, 17);
  b = netc_socket(AF_INET, SOCK_DGRAM, 17);
  check(a >= 0 && b >= 0, "udp sockets");
  if (a >= 0 && b >= 0 && r == 0) {
    netc_clearerr();
    build_sockaddr_in(addr_buf, 0, 0); /* INADDR_ANY, ephemeral */
    r = netc_bind(a, addr_buf, SOCKADDR_IN_SIZE);
    check(r == 0, "udp bind ephemeral");
    if (r == 0) {
      netc_clearerr();
      alen = SOCKADDR_IN_SIZE;
      netc_getsockname(a, addr_buf, &alen);
      unsigned short sport = parse_sin_port(addr_buf);
      /* Build sender addr 127.0.0.1:port_be for sendto */
      build_sockaddr_in(addr_buf, 0x7F000001u, sport);
      netc_clearerr();
      n = netc_sendto(b, "hi", 2, 0, addr_buf, SOCKADDR_IN_SIZE);
      check(n == 2, "udp sendto");
      netc_clearerr();
      alen = SOCKADDR_IN_SIZE;
      n = netc_recvfrom(a, buf, 64, 0, addr_buf, &alen);
      check(n == 2 && memcmp(buf, "hi", 2) == 0, "udp recvfrom echo");
    }
    netc_close(a);
    netc_close(b);
  }

  printf("blocking connect\n");
  fd2 = netc_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd2 >= 0) {
    netc_clearerr();
    r = netc_getaddrinfo("127.0.0.1", 9, 9, addr_buf);
    if (r == 0) {
      build_sockaddr_in(addr_buf, 0x7F000001u, 0x0009u);
      netc_setnonblock(fd2, 1);
      netc_clearerr();
      r = netc_connect(fd2, addr_buf, SOCKADDR_IN_SIZE);
      /* On localhost a closed port may return EINPROGRESS for a
         non-blocking socket, or ECONNREFUSED immediately. */
      int err = netc_errno();
      check(r == 0 || err == NET_EINPROGRESS || err == NET_EAGAIN ||
            err == NET_ECONNREFUSED,
            "connect (done, in-progress, or refused)");
      netc_setnonblock(fd2, 0);
    }
    netc_close(fd2);
  }

  printf("poll mask mapping\n");
  {
    int u = netc_socket(AF_INET, SOCK_DGRAM, 17);
    check(u >= 0, "poll socket");
    if (u >= 0) {
      netc_clearerr();
      r = netc_poll(u, NET_POLLOUT, 0);
      check(r == 1, "poll writable via libnet POLLOUT");
      netc_clearerr();
      r = netc_poll(u, NET_POLLIN, 0);
      check(r == 0, "poll no-data timeout 0");
      netc_close(u);
    }
  }

  netc_close(fd);

  printf("\n%s (%d failure%s)\n", fails == 0 ? "PASS" : "FAIL",
         fails, fails == 1 ? "" : "s");
  return fails == 0 ? 0 : 1;
}
