 
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

 dcl shutdown entry (
   socket_fd_t,   /* socket descriptor */
   shut_how_t     /* 0=read, 1=write, 2=both */
 ) returns(fixed bin(31))  /* status */
   options(linkage(system)) external('shutdown');

 dcl accept entry (
   socket_fd_t  /* server socket descriptor */
 ) returns(socket_fd_t)  /* client socket descriptor */
   options(linkage(system)) external('default_accept');

 dcl socket entry () returns(socket_fd_t)
     options(linkage(system)) external('default_socket');

 dcl listen entry (
    socket_fd_t,  /* socket descriptor */
    backlog_t     /* backlog */
 ) returns(fixed bin(31))  /* status */
   options(linkage(system)) external('listen');

 dcl bind entry (
   socket_fd_t,  /* socket descriptor */
   port_t        /* port */
 ) returns (fixed bin(31))  /* status */
   options(linkage(system)) external('bind_to_port');

 dcl connect entry (
   char(*) varying,  /* host */
   port_t            /* port */
 ) returns(socket_fd_t);  /* socket descriptor */

 dcl send entry (
   socket_fd_t,  /* socket descriptor */
   pointer,      /* request */
   size_t,       /* length */
   flags_t       /* flags */
 ) returns(size_t)  /* num of bytes sent */
   options(linkage(system)) external('send');

 dcl receive entry (
   socket_fd_t,  /* socket descriptor */
   pointer,     /* buffer */
   size_t,       /* length */
   flags_t       /* flags */
 ) returns(size_t)  /* num of bytes received  */
   options(linkage(system)) external('recv');

 dcl close entry (
   socket_fd_t  /* socket descriptor */
 ) returns(fixed bin(31))  /* error code */
   options(linkage(system)) external('close');