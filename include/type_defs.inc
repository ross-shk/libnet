/* Copyright 2026 Ross Shkurat - Apache-2.0, see LICENSE */

 %process options(extended_replace);

 %replace sock_fd_t   by "fixed bin(31)";  
 %replace size_t      by "fixed bin(31)";
 %replace port_t      by "fixed bin(31)";
 %replace backlog_t   by "fixed bin(31)";
 %replace flags_t     by "fixed bin(31)";
 %replace shut_how_t  by "fixed bin(31)";
 %replace buffer_t    by "char(2045)";

 %replace CR_LF by "'0D0A'x";

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

 dcl 1 conncb aligned based,
       2 _fd        sock_fd_t,
       2 _ip        char(16),
       2 _host      char(256) init(''),
       2 _port      port_t init(0),
       2 _connected bit(1) init('0'b),
       2 _family    fixed bin(31),
       2 _type      fixed bin(31),
       2 _flag      fixed bin(31),
       2 _in_addr   fixed bin(31);
 