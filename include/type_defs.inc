 /* type_defs.inc - libnet shared types and constants.
  *
  * Module:  type_defs
  * Purpose: socket-domain constants, libnet status codes, and buffer
  *          capacities, shared by the C bridge declarations and the
  *          PL/I client/server libraries.
  *
  * The connection object is owned by the library (per-handle BASED
  * storage); callers hold only a POINTER handle. The structure layout
  * is private to the library and is declared in source/net.pli, not here.
  */

 /* --- address family / socket type / protocol (POSIX) --------- */
 /* IPv4 only: address strings are 16-byte dotted quads. AF_INET6 */
 /* is reserved for a future revision with wider buffers.         */
 %replace AF_INET      by  2;
 %replace AF_INET6     by 30;  /* macOS; Linux: 10 */
 %replace SOCK_STREAM  by  1;
 %replace SOCK_DGRAM   by  2;
 %replace IPPROTO_TCP  by  6;
 %replace IPPROTO_UDP  by 17;

 /* --- shutdown(2) directions ---------------------------------- */
 %replace SHUT_RD      by 0;
 %replace SHUT_WR      by 1;
 %replace SHUT_RDWR    by 2;

 /* --- poll(2) event bits: libnet mask, mapped onto POLLIN/    */
 /* --- POLLOUT by the C bridge (platform POLLOUT is 4, not 2) --- */
 %replace POLLIN       by 1;
 %replace POLLOUT      by 2;

 /* --- condition oncode sentinels (set via SIGNAL ... SET ONCODE)
  * -------------------------------------------------------------- */
 /* These are the oncode values carried by the net_timeout / net_eof
  * / net_overflow conditions. POSIX errno (always positive) is
  * carried by the net_error condition. They are NOT return codes:
  * libnet reports all failures through conditions. */
 %replace NET_ERR_EOF       by -2;   /* peer closed / end of stream  */
 %replace NET_ERR_TIMEOUT   by -3;   /* read/write timeout or EAGAIN */
 %replace NET_ERR_OVERFLOW  by -4;   /* caller buffer exhausted      */

 /* --- raw socket option levels (platform-specific; macOS shown) ---
  * On Linux, SOL_SOCKET = 1. The PL/I layer passes these directly to
  * the raw sock_setsockopt / sock_getsockopt wrappers.
  * ---------------------------------------------------------------- */
 %replace SOL_SOCKET     by 65535;  /* macOS; Linux: 1          */
 %replace SO_REUSEADDR   by     4;  /* macOS; Linux: 2          */
 %replace SO_KEEPALIVE   by     8;
 %replace SO_LINGER      by   128;
 %replace SO_RCVTIMEO    by  4102;  /* macOS; Linux: 20         */
 %replace SO_SNDTIMEO    by  4101;  /* macOS; Linux: 21         */
 %replace SO_ERROR       by  4103;  /* macOS; Linux: 4          */

 %replace TCP_NODELAY    by     1;  /* portable                 */

 /* --- struct sizes (for raw buffer operations) ------------------- */
 %replace SOCKADDR_IN_SIZE  by 16;   /* sizeof(struct sockaddr_in) */
 %replace LINGER_SIZE       by  8;   /* sizeof(struct linger)       */
 %replace TIMEVAL_SIZE      by 16;   /* sizeof(struct timeval)      */

 /* --- legacy net_setopt codes (still used by the public API) ---
  * PL/I maps NETOPT_* to (SOL_SOCKET, SO_*) via map_opt.
  * ---------------------------------------------------------------- */
 %replace NETOPT_REUSEADDR by 0;   /* SO_REUSEADDR */
 %replace NETOPT_KEEPALIVE  by 1;  /* SO_KEEPALIVE */
 %replace NETOPT_NODELAY    by 2;  /* TCP_NODELAY  */

 /* --- send/recv flag: nonblocking ------------------------------ */
 %replace MSG_DONTWAIT  by 128;    /* macOS; Linux: 64 */

 /* --- default buffer capacities -------------------------------- */
 %replace NET_BUF_CAP   by 4096;
 %replace NET_HOST_MAX  by 256;

 /* --- server connection pool ------------------------------------ */
 %replace NET_MAX_CLIENTS by 1024;
