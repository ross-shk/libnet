/* sock.inc - libnet socket compatibility layer interface.
 *
 * Module:  sock
 * Purpose: Declare the sock_* procedures (implemented in source/sock.pli)
 *          that provide thin PL/I wrappers over the raw libc c_* bindings
 *          declared in c_bridge.inc.
 *
 * On POSIX (Linux/macOS) c_* resolves to libc symbols directly via
 * external('socket') etc.; no C source is compiled.  On Windows, source/c_bridge.c
 * provides the c_* implementations wrapping Winsock2.
 *
 * This include transitively pulls in c_bridge.inc so callers (e.g. net.pli)
 * get the errno variable and c_bridge constants; net.pli can therefore
 * replace %include c_bridge with %include sock.
 */

%include c_bridge;

/*--- Socket lifecycle ---*/
dcl sock_socket   entry (fixed bin(31), fixed bin(31), fixed bin(31))
    returns (fixed bin(31)) external;

dcl sock_close    entry (fixed bin(31))
    returns (fixed bin(31)) external;

dcl sock_shutdown entry (fixed bin(31), fixed bin(31))
    returns (fixed bin(31)) external;

dcl sock_bind     entry (fixed bin(31), char(*), fixed bin(31))
    returns (fixed bin(31)) external;

dcl sock_listen   entry (fixed bin(31), fixed bin(31))
    returns (fixed bin(31)) external;

dcl sock_accept   entry (fixed bin(31))
    returns (fixed bin(31)) external;

/*--- Connect / data transfer ---*/
dcl sock_connect  entry (fixed bin(31), char(*), fixed bin(31))
    returns (fixed bin(31)) external;

dcl sock_send     entry (fixed bin(31), char(*), fixed bin(31),
    fixed bin(31))
    returns (fixed bin(31)) external;

dcl sock_recv     entry (fixed bin(31), char(*), fixed bin(31),
    fixed bin(31))
    returns (fixed bin(31)) external;

dcl sock_sendto   entry (fixed bin(31), char(*), fixed bin(31),
    fixed bin(31), char(*), fixed bin(31))
    returns (fixed bin(31)) external;

dcl sock_recvfrom entry (fixed bin(31), char(*), fixed bin(31),
    fixed bin(31), char(*), pointer)
    returns (fixed bin(31)) external;

/*--- Socket options ---*/
dcl sock_setsockopt entry (fixed bin(31), fixed bin(31), fixed bin(31),
    pointer, fixed bin(31))
    returns (fixed bin(31)) external;

dcl sock_getsockopt entry (fixed bin(31), fixed bin(31), fixed bin(31),
    pointer, pointer)
    returns (fixed bin(31)) external;

/*--- Address queries ---*/
dcl sock_getpeername entry (fixed bin(31), char(*), pointer)
    returns (fixed bin(31)) external;

dcl sock_getsockname entry (fixed bin(31), char(*), pointer)
    returns (fixed bin(31)) external;

/*--- Nonblocking I/O ---*/
dcl sock_setnonblock entry (fixed bin(31), fixed bin(31))
    returns (fixed bin(31)) external;

/*--- poll(2) ---*/
dcl sock_poll       entry (fixed bin(31), fixed bin(31), fixed bin(31))
    returns (fixed bin(31)) external;

dcl sock_poll_multi entry (pointer, pointer, pointer,
    fixed bin(31), fixed bin(31))
    returns (fixed bin(31)) external;

/*--- DNS ---*/
dcl sock_getaddrinfo  entry (char(*), fixed bin(31), fixed bin(31),
    char(*))
    returns (fixed bin(31)) external;

dcl sock_freeaddrinfo entry (pointer) external;
