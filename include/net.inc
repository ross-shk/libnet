 /* net.inc - libnet interface include.
  *
  * Module:  net
  * Purpose: supply the whole libnet surface to a program WITHOUT
  *          pasting the implementation in. The procedures live in the
  *          separately compiled module source/net.pli (linked as
  *          libnet.a); this include supplies only what the caller's
  *          scope needs to compile against it:
  *
  *            - shared constants (address families, socket types,
  *              NET_* oncode sentinels, errno) via %replace;
  *            - the four error conditions, so caller ON-units can
  *              name them;
  *            - external ENTRY declarations for every exported net_*
  *              procedure.
  *
  * A program that wants networking does `%include net;`, links -lnet,
  * and calls the net_* entries directly. No connection logic is
  * compiled into the program - it lives once in the library module.
  */

 %include errno;
 %include type_defs;

 /*---------------------------------------------------------------*/
 /* Conditions (the only error channel)                          */
 /*---------------------------------------------------------------*/
 dcl net_error    condition;
 dcl net_timeout  condition;
 dcl net_eof      condition;
 dcl net_overflow condition;

 /*---------------------------------------------------------------*/
 /* External entry declarations for the compiled net module       */
 /*---------------------------------------------------------------*/
 dcl net_open       entry (fixed bin(31), fixed bin(31), fixed bin(31))
    returns (pointer) external;

 dcl net_connect    entry (pointer, char(NET_HOST_MAX) varying,
    fixed bin(31)) external;
 dcl net_dial       entry (pointer, char(NET_HOST_MAX) varying,
    fixed bin(31)) external;

 dcl net_close      entry (pointer) external;
 dcl net_shutdown   entry (pointer, fixed bin(31)) external;

 dcl net_read       entry (pointer, char(NET_BUF_CAP) varying,
    fixed bin(31))
    returns (fixed bin(31)) external;
 dcl net_read_all   entry (pointer, char(NET_BUF_CAP) varying)
    returns (fixed bin(31)) external;
 dcl net_read_until entry (pointer, char(NET_BUF_CAP) varying, char(1))
    returns (fixed bin(31)) external;

 dcl net_write      entry (pointer, char(NET_BUF_CAP) varying)
    returns (fixed bin(31)) external;
 dcl net_send       entry (pointer, char(NET_BUF_CAP) varying,
    fixed bin(31))
    returns (fixed bin(31)) external;
 dcl net_send_all   entry (pointer, char(NET_BUF_CAP) varying)
    returns (fixed bin(31)) external;
 dcl net_send_once  entry (pointer, char(NET_BUF_CAP) varying,
    fixed bin(31))
    returns (fixed bin(31)) external;
 dcl net_sendto     entry (pointer, char(NET_BUF_CAP) varying,
    char(16) varying, fixed bin(31))
    returns (fixed bin(31)) external;
 dcl net_recvfrom   entry (pointer, char(NET_BUF_CAP) varying,
    char(16) varying, fixed bin(31))
    returns (fixed bin(31)) external;

 dcl net_poll       entry (pointer, fixed bin(31), fixed bin(31))
    returns (fixed bin(31)) external;
 dcl net_set_timeout      entry (pointer, fixed bin(31),
    fixed bin(31)) external;
 dcl net_set_nonblocking  entry (pointer, bit(1)) external;
 dcl net_setopt           entry (pointer, fixed bin(31),
    fixed bin(31)) external;
 dcl net_set_linger       entry (pointer, fixed bin(31),
    fixed bin(31)) external;

 dcl net_peer       entry (pointer, char(16) varying,
    fixed bin(31)) external;
 dcl net_local      entry (pointer, char(16) varying,
    fixed bin(31)) external;

 dcl net_resolve    entry (char(NET_HOST_MAX) varying,
   char(16) varying) external;
 dcl net_strerror   entry (fixed bin(31),
    char(NET_BUF_CAP) varying) external;

 dcl net_connect_nb     entry (pointer, char(NET_HOST_MAX) varying,
    fixed bin(31)) external;
 dcl net_connect_finish entry (pointer) external;

 dcl net_listen     entry (fixed bin(31),
    fixed bin(31)) returns (pointer) external;
 dcl net_accept     entry (pointer, pointer) external;

 dcl net_errtext    entry returns (char(32)) external;
