 
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

 %include type_defs;

 dcl socket entry (
   fixed bin(31),  /* family */
   fixed bin(31),  /* type */
   fixed bin(31)   /* flag */
 ) returns(fixed bin(31))  /* socket descriptor */
   options(linkage(system)) external('socket');

 dcl resolve_hostname entry (
   pointer,        /* hostname (null-terminated) */
   fixed bin(31),  /* address family */
   pointer,        /* output IP string buffer */
   fixed bin(31)   /* output buffer length */
 ) returns(fixed bin(31))  /* 0 on success, -1 on error */
   options(linkage(system)) external('resolve_hostname');

 dcl get_errno entry () returns(fixed bin(31))
    options(linkage(system)) external('get_errno');

 dcl shutdown entry (
   conn_t,   /* socket descriptor */
   shut_how_t     /* 0=read, 1=write, 2=both */
 ) returns(fixed bin(31))  /* status */
   options(linkage(system)) external('shutdown');

 dcl accept entry (
   conn_t  /* server socket descriptor */
 ) returns(conn_t)  /* client socket descriptor */
   options(linkage(system)) external('default_accept');

 dcl listen entry (
    conn_t,  /* socket descriptor */
    backlog_t     /* backlog */
 ) returns(fixed bin(31))  /* status */
   options(linkage(system)) external('listen');

 dcl bind entry (
   conn_t,    /* socket descriptor */
   port_t,         /* port */
   fixed bin(31),  /* family */
   fixed bin(31)   /* in-address */
 ) returns (fixed bin(31))  /* status */
   options(linkage(system)) external('bind_to_port');
 
 dcl connect_to_ip entry (
   fixed bin(31), /* ip (binary) */
   port_t,        /* port */
   conncb,        /* socket file descriptor */
   fixed bin(31)  /* address family */
 ) returns(fixed bin(31))
   options(linkage(system)) external('connect_to_ip');

 dcl connect entry (
   pointer,      /* host */
   port_t,       /* port */
   conn_t,  /* socket file descriptor */
   fixed bin(31) /* address family */
 ) returns(fixed bin(31))  /* socket descriptor */
   options(linkage(system)) external('connect_to_host');

 dcl send entry (
   conn_t,  /* socket descriptor */
   pointer,      /* request */
   size_t,       /* length */
   flags_t       /* flags */
 ) returns(size_t)  /* num of bytes sent */
   options(linkage(system)) external('send');

 dcl receive entry (
   conn_t,  /* socket descriptor */
   pointer,      /* buffer */
   size_t,       /* length */
   flags_t       /* flags */
 ) returns(size_t)  /* num of bytes received  */
   options(linkage(system)) external('recv');

 dcl read entry (
   conn_t,  /* socket descriptor */
   pointer,      /* buffer */
   size_t        /* length */
 ) returns(size_t)  /* num of bytes read */
   options(linkage(system)) external('read');

 dcl write entry (
   conn_t,  /* socket descriptor */
   pointer,      /* buffer */
   size_t        /* length */
 ) returns(size_t)  /* num of bytes written */
   options(linkage(system)) external('write');

 dcl close entry (
   conn_t  /* socket descriptor */
 ) returns(fixed bin(31))  /* error code */
   options(linkage(system)) external('close');