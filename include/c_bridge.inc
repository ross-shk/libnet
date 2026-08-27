/* Copyright 2026 Ross Shkurat - Apache-2.0, see LICENSE */

 %include type_defs;

 dcl c_socket entry (
   fixed bin(31),  /* family */
   fixed bin(31),  /* type */
   fixed bin(31)   /* flag */
 ) returns(fixed bin(31))  /* socket descriptor */
   options(linkage(system)) external('socket');

 dcl c_resolve_hostname entry (
   pointer,        /* hostname (null-terminated) */
   fixed bin(31),  /* address family */
   pointer,        /* output IP string buffer */
   fixed bin(31)   /* output buffer length */
 ) returns(fixed bin(31))  /* 0 on success, -1 on error */
   options(linkage(system)) external('resolve_hostname');

 dcl c_get_errno entry () returns(fixed bin(31))
    options(linkage(system)) external('get_errno');

 dcl c_shutdown entry (
   sock_fd_t,   /* socket descriptor */
   shut_how_t     /* 0=read, 1=write, 2=both */
 ) returns(fixed bin(31))  /* status */
   options(linkage(system)) external('shutdown');

 dcl c_accept entry (
   sock_fd_t  /* server socket descriptor */
 ) returns(sock_fd_t)  /* client socket descriptor */
   options(linkage(system)) external('default_accept');

 dcl c_listen entry (
    sock_fd_t,  /* socket descriptor */
    backlog_t     /* backlog */
 ) returns(fixed bin(31))  /* status */
   options(linkage(system)) external('listen');

 dcl c_bind entry (
   sock_fd_t,    /* socket descriptor */
   port_t,         /* port */
   fixed bin(31),  /* family */
   fixed bin(31)   /* in-address */
 ) returns (fixed bin(31))  /* status */
   options(linkage(system)) external('bind_to_port');

 dcl c_connect entry (
   pointer,      /* host */
   port_t,       /* port */
   sock_fd_t,       /* socket file descriptor */
   fixed bin(31) /* address family */
 ) returns(fixed bin(31))  /* socket descriptor */
   options(linkage(system)) external('connect_to_host');

 dcl c_send entry (
   sock_fd_t,  /* socket descriptor */
   pointer,      /* request */
   size_t,       /* length */
   flags_t       /* flags */
 ) returns(size_t)  /* num of bytes sent */
   options(linkage(system)) external('send');

 dcl c_receive entry (
   sock_fd_t,  /* socket descriptor */
   pointer,      /* buffer */
   size_t,       /* length */
   flags_t       /* flags */
 ) returns(size_t)  /* num of bytes received  */
   options(linkage(system)) external('recv');

 dcl c_read entry (
   sock_fd_t,  /* socket descriptor */
   pointer,      /* buffer */
   size_t        /* length */
 ) returns(size_t)  /* num of bytes read */
   options(linkage(system)) external('read');

 dcl c_write entry (
   sock_fd_t,  /* socket descriptor */
   pointer,      /* buffer */
   size_t        /* length */
 ) returns(size_t)  /* num of bytes written */
   options(linkage(system)) external('write');

 dcl c_close entry (
   sock_fd_t  /* socket descriptor */
 ) returns(fixed bin(31))  /* error code */
   options(linkage(system)) external('close');