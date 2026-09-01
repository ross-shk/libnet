/* Copyright 2026 Ross Shkurat - Apache-2.0, see LICENSE */

 %process options(extended_replace);

 %replace sock_fd_t by "fixed bin(31)";  /* signed, -1 error */
 %replace size_t by "fixed bin(31)";  
 %replace port_t by "fixed bin(31)";  
 %replace backlog_t by "fixed bin(15)";  /* small, was 31 */
 %replace flags_t by "fixed bin(31)";
 %replace shut_how_t by "fixed bin(31)";
 %replace buffer_t by "char(2045)";  /* unused, consider 4096 */

 %replace CR_LF by "'0D0A'x";
 %replace MAX_HOST_LEN by "256";  /* rfc 1035 */
 %replace MAX_IP_LEN by "46";  /* ipv6 max 45 + nul */

 dcl 1 AF static,
       2 UNSPEC fixed bin(31) init(0),
       2 UNIX   fixed bin(31) init(1),
       2 INET   fixed bin(31) init(2),
       2 INET6  fixed bin(31) init(10);
                 /* not wired, see c_bridge.c */

 dcl 1 SOCK_TYPE static,
       2 STREAM fixed bin(31) init(1),
       2 DGRAM  fixed bin(31) init(2),
       2 RAW    fixed bin(31) init(3);

 dcl 1 SOCK_FLAGS static,
       2 CLOEXEC  fixed bin(31) init(524288), /* 0x80000 */
       2 NONBLOCK fixed bin(31) init(2048);  /* 0x800 */

 dcl 1 INADDR static,
       2 ANY       fixed bin(31) init(0),
       2 BROADCAST fixed bin(31) init(-1);

 dcl 1 PF static,
       2 UNSPEC   fixed bin(31) init(0),
       2 UNIX     fixed bin(31) init(1),
       2 INET     fixed bin(31) init(2),
       2 INET6    fixed bin(31) init(10);

 dcl 1 IPPROTO static,
       2 IP       fixed bin(31) init(0),
       2 TCP      fixed bin(31) init(6),
       2 UDP      fixed bin(31) init(17),
       2 IPV6     fixed bin(31) init(41),
       2 ICMPV6   fixed bin(31) init(58);

 dcl 1 SOL static,
       2 SOCKET   fixed bin(31) init(1),
       2 IP       fixed bin(31) init(0),
       2 TCP      fixed bin(31) init(6);

 dcl 1 SO static,
       2 REUSEADDR fixed bin(31) init(2),
       2 REUSEPORT fixed bin(31) init(15),
       2 KEEPALIVE fixed bin(31) init(9),
       2 SNDBUF    fixed bin(31) init(7),
       2 RCVBUF    fixed bin(31) init(8),
       2 SNDTIMEO  fixed bin(31) init(21),
       2 RCVTIMEO  fixed bin(31) init(20),
       2 ERROR     fixed bin(31) init(4),
       2 TYPE      fixed bin(31) init(3),
       2 LINGER    fixed bin(31) init(13);

 dcl 1 TCP static,
       2 NODELAY   fixed bin(31) init(1),
       2 MAXSEG    fixed bin(31) init(2),
       2 CORK      fixed bin(31) init(3),
       2 KEEPIDLE  fixed bin(31) init(4),
       2 KEEPINTVL fixed bin(31) init(5),
       2 KEEPCNT   fixed bin(31) init(6);

 dcl 1 MSG_FLAG static,
       2 PEEK     fixed bin(31) init(2),
       2 WAITALL  fixed bin(31) init(256),
       2 DONTWAIT fixed bin(31) init(64),
       2 NOSIGNAL fixed bin(31) init(16384);

 dcl 1 SHUT static,
       2 RD     fixed bin(31) init(0),
       2 WR     fixed bin(31) init(1),
       2 RDWR   fixed bin(31) init(2);

 dcl SOMAXCONN fixed bin(31) static init(4096);

 dcl 1 AI static,
       2 PASSIVE    fixed bin(31) init(1),
       2 CANONNAME  fixed bin(31) init(2),
       2 NUMERICHOST fixed bin(31) init(4),
       2 ADDRCONFIG fixed bin(31) init(32),
       2 V4MAPPED   fixed bin(31) init(8);

 dcl 1 NI static,
       2 NUMERICHOST fixed bin(31) init(1),
       2 MAXHOST     fixed bin(31) init(1025);

 /* conncb is aligned based - use 'dcl conn like conncb' */ 
 /* like loses initial, based requires implicit pointer */
 dcl 1 conncb aligned based,
       2 fd        sock_fd_t,
       2 ip_addr        char(46),  /* ipv6 max 45 + nul */
       2 host_name      char(256) init(''),  /* 255 + nul, rfc 1035 */
       2 port      port_t init(0),
       2 is_connected bit(1) init('0'b),
       2 addr_family    fixed bin(31),
       2 sock_type      fixed bin(31),
       2 flags      fixed bin(31),
       2 ipv4_addr   fixed bin(31),
       2 read_timeout  fixed bin(31) init(0),  /* ms, 0 infinite */
       2 write_timeout fixed bin(31) init(0);  /* ms, 0 infinite */
 