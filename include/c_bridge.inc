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

 /* --- Socket lifecycle (raw syscalls) --- */
 dcl netc_socket entry (fixed bin(31), fixed bin(31), fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_socket');

 dcl netc_close  entry (fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_close');

 dcl netc_shutdown entry (fixed bin(31), fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_shutdown');

 dcl netc_bind entry (fixed bin(31), char(*), fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_bind');

 dcl netc_listen entry (fixed bin(31), fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_listen');

 dcl netc_accept entry (fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_accept');

 /* --- Raw connect (takes sockaddr_in buffer) --- */
 dcl netc_connect entry (fixed bin(31), char(*), fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_connect');

 /* --- Raw send/recv --- */
 dcl netc_send entry (fixed bin(31), char(*), fixed bin(31),
     fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_send');

 dcl netc_recv entry (fixed bin(31), char(*), fixed bin(31),
     fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_recv');

 /* --- Raw sendto/recvfrom (take/fill sockaddr_in buffers) --- */
 dcl netc_sendto entry (fixed bin(31), char(*), fixed bin(31),
     fixed bin(31), char(*), fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_sendto');

 dcl netc_recvfrom entry (fixed bin(31), char(*), fixed bin(31),
     fixed bin(31), char(*), pointer)
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_recvfrom');

 /* --- Raw socket options ---
  * level/optname are the real SOL_SOCKET/SO_* values; PL/I
  * maps NETOPT_* codes via map_opt. */
 dcl netc_setsockopt entry (fixed bin(31), fixed bin(31), fixed bin(31),
     pointer, fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_setsockopt');

 dcl netc_getsockopt entry (fixed bin(31), fixed bin(31), fixed bin(31),
     pointer, pointer)
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_getsockopt');

 /* --- Raw address queries (fill raw sockaddr_in buffer) --- */
 dcl netc_getpeername entry (fixed bin(31), char(*), pointer)
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_getpeername');

 dcl netc_getsockname entry (fixed bin(31), char(*), pointer)
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_getsockname');

 /* --- Nonblocking I/O control --- */
 dcl netc_setnonblock entry (fixed bin(31), fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_setnonblock');

 /* --- poll(2): single and multi-fd --- */
 dcl netc_poll entry (fixed bin(31), fixed bin(31), fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_poll');

 /* Multi-fd poll: fds/ev/revents arrays, nfds count, ms timeout. */
 dcl netc_poll_multi entry (ptr, ptr, ptr, fixed bin(31), fixed bin(31))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_poll_multi');

 /* --- DNS — raw getaddrinfo wrapper ---
  * host     - raw host string buffer
  * hostlen  - effective length of host (PL/I char is blank-padded)
  * port     - port number (set into sin_port by C in network order)
  * addr_buf - output buffer for raw struct sockaddr_in (16 bytes) */
 dcl netc_getaddrinfo entry (char(*), fixed bin(31), fixed bin(31),
     char(*))
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_getaddrinfo');

 /* --- Error --- */
 dcl netc_errno entry ()
     returns (fixed bin(31))
     options (linkage(system)) external ('netc_errno');

 dcl netc_clearerr entry ()
     options (linkage(system)) external ('netc_clearerr');
