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
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

 %process options(extended_replace);

 %replace socket_fd_t by "fixed bin(31)";  /* socket descriptor */
 %replace size_t      by "fixed bin(31)";
 %replace port_t      by "fixed bin(31)";
 %replace backlog_t   by "fixed bin(31)";
 %replace flags_t     by "fixed bin(31)";
 %replace shut_how_t  by "fixed bin(31)";
 %replace buffer_t    by "char(2045)";

 %replace LINE_END by "'0D0A'x";

 dcl 1 socket_t based,
      2 _fd        socket_fd_t,
      2 _host      char(256) init(''),
      2 _port      port_t init(0),
      2 _connected bit(1) init('0'b),
      2 _family    fixed bin(31),
      2 _type      fixed bin(31),
      2 _address,
       3 s_address fixed bin(31);
 