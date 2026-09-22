/* c_bridge.inc — by-value FFI declarations for the thin C bridge.
 *
 * Reached through pli-llvm's OPTIONS(LINKAGE(SYSTEM)) / OPTIONS(BYVALUE) C
 * ABI: scalar fixed bin(31) / float params pass by value, character buffers
 * pass as char* with an explicit length, pointers pass by value. No NUL
 * termination is assumed (PL/I CHAR is blank-padded).
 *
 * These are the ONLY functions implemented in C; all policy lives in PL/I.
 */

/* socket / listen / accept */
dcl s_socket entry (fixed bin(31), fixed bin(31), fixed bin(31))
    returns (fixed bin(31))
    options (linkage(system)) external ('s_socket');

dcl s_bind entry (fixed bin(31), fixed bin(31))
    returns (fixed bin(31))
    options (linkage(system)) external ('s_bind');

dcl s_listen entry (fixed bin(31), fixed bin(31))
    returns (fixed bin(31))
    options (linkage(system)) external ('s_listen');

dcl s_accept entry (fixed bin(31))
    returns (fixed bin(31))
    options (linkage(system)) external ('s_accept');

/* connect: host is a char* with an explicit length; port is by value */
dcl s_connect entry (fixed bin(31), char(*), fixed bin(31), fixed bin(31))
    returns (fixed bin(31))
    options (linkage(system)) external ('s_connect');

/* send / recv: buffer is char* with an explicit length; flags by value */
dcl s_send entry (fixed bin(31), char(*), fixed bin(31), fixed bin(31))
    returns (fixed bin(31))
    options (linkage(system)) external ('s_send');

dcl s_recv entry (fixed bin(31), char(*), fixed bin(31), fixed bin(31))
    returns (fixed bin(31))
    options (linkage(system)) external ('s_recv');

/* close / shutdown */
dcl s_close entry (fixed bin(31))
    returns (fixed bin(31))
    options (linkage(system)) external ('s_close');

dcl s_shutdown entry (fixed bin(31), fixed bin(31))
    returns (fixed bin(31))
    options (linkage(system)) external ('s_shutdown');

/* timeouts / nonblocking / poll */
dcl s_settimeout entry (fixed bin(31), fixed bin(31), fixed bin(31))
    returns (fixed bin(31))
    options (linkage(system)) external ('s_settimeout');

dcl s_setnonblock entry (fixed bin(31), fixed bin(31))
    returns (fixed bin(31))
    options (linkage(system)) external ('s_setnonblock');

dcl s_poll entry (fixed bin(31), fixed bin(31), fixed bin(31))
    returns (fixed bin(31))
    options (linkage(system)) external ('s_poll');

/* peer address: ip is a char(*) output buffer; port is an out-param */
dcl s_getpeername entry (fixed bin(31), char(*), fixed bin(31),
                         pointer)
    returns (fixed bin(31))
    options (linkage(system)) external ('s_getpeername');

/* local address (getsockname): ip char(*) out, port out-param */
dcl s_getsockname entry (fixed bin(31), char(*), fixed bin(31),
                         pointer)
    returns (fixed bin(31))
    options (linkage(system)) external ('s_getsockname');

/* socket option by libnet code: s_sockopt(fd, opt, value) */
dcl s_sockopt entry (fixed bin(31), fixed bin(31), fixed bin(31))
    returns (fixed bin(31))
    options (linkage(system)) external ('s_sockopt');

/* SO_LINGER(fd, on, seconds) */
dcl s_setlinger entry (fixed bin(31), fixed bin(31), fixed bin(31))
    returns (fixed bin(31))
    options (linkage(system)) external ('s_setlinger');

/* nonblocking connect: 0 done, -1 in progress (errno=EINPROGRESS),
 * -2 error. host is char(*) with an explicit length. */
dcl s_connect_nb entry (fixed bin(31), char(*), fixed bin(31),
                        fixed bin(31))
    returns (fixed bin(31))
    options (linkage(system)) external ('s_connect_nb');

/* SO_ERROR check after a nonblocking connect; err via out-param */
dcl s_getsockerr entry (fixed bin(31), pointer)
    returns (fixed bin(31))
    options (linkage(system)) external ('s_getsockerr');

/* resolve host to dotted-quad in ip (char(*) out, explicit len) */
dcl s_resolve entry (char(*), fixed bin(31), char(*), fixed bin(31))
    returns (fixed bin(31))
    options (linkage(system)) external ('s_resolve');

/* strerror(e) into buf (char(*) out) */
dcl s_strerror entry (fixed bin(31), char(*), fixed bin(31))
    options (linkage(system)) external ('s_strerror');

/* UDP datagrams */
dcl s_sendto entry (fixed bin(31), char(*), fixed bin(31), fixed bin(31),
                    char(*), fixed bin(31), fixed bin(31))
    returns (fixed bin(31))
    options (linkage(system)) external ('s_sendto');

dcl s_recvfrom entry (fixed bin(31), char(*), fixed bin(31), fixed bin(31),
                      char(*), fixed bin(31), pointer)
    returns (fixed bin(31))
    options (linkage(system)) external ('s_recvfrom');

/* errno cache */
dcl s_errno entry ()
    returns (fixed bin(31))
    options (linkage(system)) external ('s_errno');

dcl s_clearerr entry ()
    options (linkage(system)) external ('s_clearerr');
