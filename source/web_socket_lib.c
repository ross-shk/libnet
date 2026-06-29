/* Copyright 2026 Ross Shkurat
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>

int default_accept(int server_fd) {
  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);
  int client_fd;

  do {
    client_fd = accept(server_fd, (struct sockaddr *)&client_addr, 
      &client_len);
  } while (client_fd < 0 && errno == EINTR);

  if (client_fd < 0) return -1;
  return client_fd;
}

int bind_to_port(int socket_fd, int port, int af, int inaddr) {
  struct sockaddr_in addr;

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = af;
  addr.sin_addr.s_addr = inaddr;   
  addr.sin_port = htons(port);         

  return bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr));
}

int get_errno_value(void) {
  return errno;
}

int connect_to_host(char *host, int port, int socket_fd, int af) {
  struct sockaddr_in addr = {0};
  addr.sin_family = af;
  addr.sin_port = htons(port);
  inet_pton(af, host, &addr.sin_addr);

  return connect(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) ;
}
