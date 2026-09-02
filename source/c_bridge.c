/* Copyright 2026 Ross Shkurat - Apache-2.0, see LICENSE */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/time.h>
#include <poll.h>

int default_accept(int server_fd) {
  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);
  int client_fd;

  /* accept with EINTR retry */
  do {
    client_fd = accept(server_fd, (struct sockaddr *)&client_addr, 
      &client_len);
  } while (client_fd < 0 && errno == EINTR);

  /* check result */
  if (client_fd < 0) return -1;
  return client_fd;
}

int bind_to_port(int socket_fd, int port, int af, int inaddr) {
  struct sockaddr_in addr;

  /* build sockaddr */
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = af;
  addr.sin_addr.s_addr = inaddr;   
  addr.sin_port = htons(port);         

  /* bind */
  return bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr));
}

int resolve_hostname(char *hostname, int family,
                     char *out_ip, int out_len) {
  struct addrinfo hints = {0};
  struct addrinfo *result;
  int ret;

  /* setup hints */
  hints.ai_family = family;

  /* resolve */
  ret = getaddrinfo(hostname, NULL, &hints, &result);
  if (ret != 0)
      return -1;

  /* convert to string */
  if (result->ai_family == AF_INET) {
    struct sockaddr_in *a = (struct sockaddr_in *)result->ai_addr;
    inet_ntop(AF_INET, &a->sin_addr, out_ip, out_len);
  } else if (result->ai_family == AF_INET6) {
    struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)result->ai_addr;
    inet_ntop(AF_INET6, &a6->sin6_addr, out_ip, out_len);
  } else {
    freeaddrinfo(result);
    errno = EAFNOSUPPORT;
    return -1;
  }
  
  freeaddrinfo(result);
  return 0;
}

int get_errno(void) {
  /* return errno */
  return errno;
}

int connect_to_host(char *host, int port, int socket_fd, int af) {
  struct sockaddr_in addr = {0};
  /* build sockaddr and connect */
  addr.sin_family = af;
  addr.sin_port = htons(port);
  inet_pton(af, host, &addr.sin_addr);

  return connect(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) ;
}

int c_set_timeout(int fd, int is_read, int timeout_ms) {
  struct timeval tv;
  /* convert ms to timeval */
  if (timeout_ms <= 0) {
    tv.tv_sec = 0;
    tv.tv_usec = 0;
  } else {
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
  }
  /* set socket timeout */
  int opt = is_read ? SO_RCVTIMEO : SO_SNDTIMEO;
  return setsockopt(fd, SOL_SOCKET, opt, &tv, sizeof(tv));
}

int c_getsockname(int fd, char *out_ip, int out_len, int *out_port) {
  struct sockaddr_in addr;
  socklen_t len = sizeof(addr);
  /* get local addr */
  if (getsockname(fd, (struct sockaddr*)&addr, &len) < 0) return -1;
  inet_ntop(AF_INET, &addr.sin_addr, out_ip, out_len);
  *out_port = ntohs(addr.sin_port);
  return 0;
}

int c_getpeername(int fd, char *out_ip, int out_len, int *out_port) {
  struct sockaddr_in addr;
  socklen_t len = sizeof(addr);
  /* get peer addr */
  if (getpeername(fd, (struct sockaddr*)&addr, &len) < 0) return -1;
  inet_ntop(AF_INET, &addr.sin_addr, out_ip, out_len);
  *out_port = ntohs(addr.sin_port);
  return 0;
}

int c_poll(int fd, int timeout_ms, int events) {
  struct pollfd pfd;
  int r;
  /* poll single fd with EINTR retry */
  pfd.fd = fd;
  pfd.events = events;
  pfd.revents = 0;
  do {
    r = poll(&pfd, 1, timeout_ms);
  } while (r < 0 && errno == EINTR);
  if (r < 0) return -1;
  if (r == 0) return 0;
  return pfd.revents;
}
