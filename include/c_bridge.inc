 /* c_bridge.inc - by-value FFI declarations for the thin C bridge.
  *
  * Reached through pli-llvm's OPTIONS(LINKAGE(SYSTEM)) /
  * OPTIONS(BYVALUE) C ABI: scalar fixed bin(31) / float params
  * pass by value, character buffers pass as char* with an
  * explicit length, pointers pass by value. No NUL termination
  * is assumed (PL/I CHAR is blank-padded).
  *
  * These are the ONLY functions implemented in C; all policy
  * lives in PL/I.
  */

 /* socket / listen / accept */
 dcl netc_socket entry (fixed bin(31), fixed bin(31), fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_socket');

 dcl netc_bind entry (fixed bin(31), fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_bind');

 dcl netc_listen entry (fixed bin(31), fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_listen');

 dcl netc_accept entry (fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_accept');

 /* Connect using a host buffer with an explicit length. */
 dcl netc_connect entry (fixed bin(31), char(*), fixed bin(31),
     fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_connect');

 /* Send and receive using a buffer with an explicit length. */
 dcl netc_send entry (fixed bin(31), char(*), fixed bin(31),
     fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_send');

 dcl netc_recv entry (fixed bin(31), char(*), fixed bin(31),
     fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_recv');

 /* close / shutdown */
 dcl netc_close entry (fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_close');

 dcl netc_shutdown entry (fixed bin(31), fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_shutdown');

 /* timeouts / nonblocking / poll */
 dcl netc_settimeout entry (fixed bin(31), fixed bin(31), fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_settimeout');

 dcl netc_setnonblock entry (fixed bin(31), fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_setnonblock');

 dcl netc_poll entry (fixed bin(31), fixed bin(31), fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_poll');

 /* peer address: ip is a char(*) output buffer; port is an out-param */
 dcl netc_getpeername entry (fixed bin(31), char(*), fixed bin(31),
                          pointer)
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_getpeername');

 /* local address (getsockname): ip char(*) out, port out-param */
 dcl netc_getsockname entry (fixed bin(31), char(*), fixed bin(31),
                          pointer)
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_getsockname');

 /* socket option by libnet code: netc_sockopt(fd, opt, value) */
 dcl netc_sockopt entry (fixed bin(31), fixed bin(31), fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_sockopt');

 /* SO_LINGER(fd, on, seconds) */
 dcl netc_setlinger entry (fixed bin(31), fixed bin(31), fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_setlinger');

 /* nonblocking connect: 0 done, -1 in progress (errno=EINPROGRESS),
  * -2 error. host is char(*) with an explicit length. */
 dcl netc_connect_nb entry (fixed bin(31), char(*), fixed bin(31),
                         fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_connect_nb');

 /* SO_ERROR check after a nonblocking connect; err via out-param */
 dcl netc_getsockerr entry (fixed bin(31), pointer)
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_getsockerr');

  /* resolve host to dotted-quad in ip */
  dcl netc_resolve entry (char(*), fixed bin(31), char(*),
      fixed bin(31))
      returns (fixed bin(31))
      options (linkage(system)) external ('netc_resolve');

 /* strerror(e) into buf (char(*) out) */
 dcl netc_strerror entry (fixed bin(31), char(*), fixed bin(31))
     options (linkage(system)) external ('netc_strerror');

 /* UDP datagrams */
  dcl netc_sendto entry (fixed bin(31), char(*), fixed bin(31),
      fixed bin(31), char(*), fixed bin(31), fixed bin(31))
      returns (fixed bin(31))
      options (linkage(system)) external ('netc_sendto');

  dcl netc_recvfrom entry (fixed bin(31), char(*), fixed bin(31),
      fixed bin(31), char(*), fixed bin(31), pointer)
      returns (fixed bin(31))
      options (linkage(system)) external ('netc_recvfrom');

 /* errno cache */
 dcl netc_errno entry ()
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_errno');

 dcl netc_clearerr entry ()
     options (linkage(system)) external ('netc_clearerr');
