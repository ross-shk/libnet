/* Copyright 2026 Ross Shkurat - Apache-2.0, see LICENSE */

 %process options(extended_replace);

 %replace sock_fd_t by "fixed bin(31)";  /* signed, -1 error */
 %replace size_t by "fixed bin(31)";  
 %replace port_t by "fixed bin(15)";  
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

 dcl 1 INADDR static,
       2 ANY       fixed bin(31) init(0),
       2 BROADCAST fixed bin(31) init(-1);

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
 