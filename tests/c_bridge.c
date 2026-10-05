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

/* Declare the bridge surface under test (prototypes mirror c_bridge.inc). */
int  netc_socket(int, int, int);
int  netc_sockopt(int, int, int);
int  netc_setlinger(int, int, int);
int  netc_bind(int, unsigned);
int  netc_close(int);
int  netc_setnonblock(int, int);
int  netc_getsockname(int, char *, int, int *);
int  netc_resolve(const char *, int, char *, int);
void netc_strerror(int, char *, int);
int  netc_errno(void);
void netc_clearerr(void);
int  netc_sendto(int, const char *, int, int, const char *, int, int);
int  netc_recvfrom(int, char *, int, int, char *, int, int *);
int  netc_connect_nb(int, const char *, int, int);
int  netc_poll(int, int, int);

#ifndef AF_INET
#define AF_INET 2
#endif
/* Libnet poll mask (see type_defs.inc): 1=readable, 2=writable. */
#define NET_POLLIN 1
#define NET_POLLOUT 2

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
  char ip[16] = {0};
  char buf[64] = {0};
  int  port = 0, r, n, fd, fd2;

  printf("resolve\n");
  r = netc_resolve("127.0.0.1", 9, ip, 16);
  check(r == 0 && strncmp(ip, "127.0.0.1", 9) == 0, "resolve numeric");

  printf("socket options\n");
  fd = netc_socket(AF_INET, 1, 6);
  check(fd >= 0, "socket");
  netc_clearerr();
  r = netc_sockopt(fd, 0, 1);            /* SO_REUSEADDR */
  check(r == 0, "sockopt reuseaddr");
  r = netc_setlinger(fd, 1, 5);
  check(r == 0, "setlinger");

  printf("local address\n");
  netc_clearerr();
  r = netc_bind(fd, 0);                  /* ephemeral port */
  check(r == 0, "bind ephemeral");
  if (r == 0) {
    netc_clearerr();
    r = netc_getsockname(fd, ip, 16, &port);
    check(r == 0 && port > 0, "getsockname port");
  }

  printf("UDP datagram loopback\n");
  fd2 = netc_socket(AF_INET, 2, 17);
  {
    int a = netc_socket(AF_INET, 2, 17);
    int b = netc_socket(AF_INET, 2, 17);
    netc_clearerr();
    netc_bind(a, 0);
    netc_clearerr();
    netc_getsockname(a, ip, 16, &port);
    netc_clearerr();
    n = netc_sendto(b, "hi", 2, 0, "127.0.0.1", 9, port);
    check(n == 2, "udp sendto");
    netc_clearerr();
    n = netc_recvfrom(a, buf, 64, 0, ip, 16, &port);
    check(n == 2 && strncmp(buf, "hi", 2) == 0, "udp recvfrom echo");
    netc_close(a);
    netc_close(b);
  }

  printf("nonblocking connect\n");
  netc_clearerr();
  netc_setnonblock(fd, 1);
  netc_clearerr();
  r = netc_connect_nb(fd, "127.0.0.1", 9, 9);
  /* 0 (connected) or -1 (EINPROGRESS) both exercise the path; -2 is error. */
  check(r >= -1, "connect_nb (done or in-progress)");
  netc_strerror(netc_errno(), buf, 64);
  check(buf[0] != '\0', "strerror nonempty");

  printf("poll mask mapping\n");
  {
    /* A fresh UDP socket is always writable; nothing is readable yet.
     * With the old passthrough this polled POLLPRI instead of POLLOUT. */
    int u = netc_socket(AF_INET, 2, 17);
    check(u >= 0, "poll socket");
    netc_clearerr();
    r = netc_poll(u, NET_POLLOUT, 0);
    check(r == 1, "poll writable via libnet POLLOUT");
    netc_clearerr();
    r = netc_poll(u, NET_POLLIN, 0);
    check(r == 0, "poll no-data timeout 0");
    netc_close(u);
  }

  netc_close(fd);
  netc_close(fd2);

  printf("\n%s (%d failure%s)\n", fails == 0 ? "PASS" : "FAIL",
         fails, fails == 1 ? "" : "s");
  return fails == 0 ? 0 : 1;
}
