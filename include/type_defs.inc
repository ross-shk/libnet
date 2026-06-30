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

 %replace net_fd_t by "fixed bin(31)";  /* socket descriptor */
 %replace size_t      by "fixed bin(31)";
 %replace port_t      by "fixed bin(31)";
 %replace backlog_t   by "fixed bin(31)";
 %replace flags_t     by "fixed bin(31)";
 %replace shut_how_t  by "fixed bin(31)";
 %replace buffer_t    by "char(2045)";

 %replace LINE_END by "'0D0A'x";

 dcl 1 AF static,
       2 UNSPEC fixed bin(31) init(0),
       2 UNIX   fixed bin(31) init(1),
       2 INET   fixed bin(31) init(2),
       2 INET6  fixed bin(31) init(10);

 dcl 1 TYPE static,
       2 STREAM fixed bin(31) init(1),
       2 DGRAM  fixed bin(31) init(2),
       2 RAW    fixed bin(31) init(3);

 dcl 1 INADDR static,
       2 ANY       fixed bin(31) init(0),
       2 BROADCAST fixed bin(31) init(-1);

 dcl 1 net_t aligned based,
       2 _fd        net_fd_t,
       2 _host      char(256) init(''),
       2 _port      port_t init(0),
       2 _connected bit(1) init('0'b),
       2 _family    fixed bin(31),
       2 _type      fixed bin(31),
       2 _flag      fixed bin(31),
       2 _in_addr   fixed bin(31);
 