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
       2 UNSPEC fixed bin(31) init(0),   /* AF_UNSPEC */
       2 UNIX   fixed bin(31) init(1),   /* AF_UNIX */
       2 INET   fixed bin(31) init(2),   /* AF_INET */
       2 INET6  fixed bin(31) init(10);  /* AF_INET6 */

 dcl 1 SOCK_TYPE static,
       2 STREAM fixed bin(31) init(1),   /* SOCK_STREAM */
       2 DGRAM  fixed bin(31) init(2),   /* SOCK_DGRAM */
       2 RAW    fixed bin(31) init(3);   /* SOCK_RAW */

 dcl 1 SOCK_FLAGS static,
       2 CLOEXEC  fixed bin(31) init(524288), /* SOCK_CLOEXEC 0x80000 */
       2 NONBLOCK fixed bin(31) init(2048);  /* SOCK_NONBLOCK 0x800 */

 dcl 1 INADDR static,
       2 ANY       fixed bin(31) init(0),   /* INADDR_ANY */
       2 BROADCAST fixed bin(31) init(-1);  /* INADDR_BROADCAST */

 dcl 1 MSG_FLAG static,
       2 PEEK     fixed bin(31) init(2),     /* MSG_PEEK */
       2 WAITALL  fixed bin(31) init(256),   /* MSG_WAITALL */
       2 DONTWAIT fixed bin(31) init(64),    /* MSG_DONTWAIT */
       2 NOSIGNAL fixed bin(31) init(16384); /* MSG_NOSIGNAL */

 dcl 1 SHUT static,
       2 RD     fixed bin(31) init(0),   /* SHUT_RD */
       2 WR     fixed bin(31) init(1),   /* SHUT_WR */
       2 RDWR   fixed bin(31) init(2);   /* SHUT_RDWR */

 dcl SOMAXCONN fixed bin(31) static init(4096); /* SOMAXCONN */

 dcl 1 ERR static,
       2 AGAIN fixed bin(31) init(11),      /* EAGAIN */
       2 WOULDBLOCK fixed bin(31) init(35), /* EWOULDBLOCK macOS/BSD */
       2 TIMEDOUT fixed bin(31) init(110),  /* ETIMEDOUT */
       2 BADURL fixed bin(31) init(61);     /* malformed url in
       net_dial */

 dcl 1 POLL static,
       2 IN   fixed bin(31) init(1),   /* POLLIN */
       2 OUT  fixed bin(31) init(4),   /* POLLOUT */
       2 ERR  fixed bin(31) init(8),   /* POLLERR */
       2 HUP  fixed bin(31) init(16),  /* POLLHUP */
       2 NVAL fixed bin(31) init(32);  /* POLLNVAL */

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
