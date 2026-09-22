/* type_defs.inc — libnet shared types and constants.
 *
 * Module:  type_defs
 * Purpose: socket-domain constants, libnet status codes, and buffer
 *          capacities, shared by the C bridge declarations and the PL/I
 *          client/server libraries.
 *
 * The connection object is owned by the library (CONTROLLED storage); callers
 * hold only a POINTER handle. The structure layout is private to the library
 * and is declared in net_base.inc / net_server.inc, not here.
 */

/* --- address family / socket type / protocol (POSIX) ---------------------- */
%replace AF_INET      by  2;
%replace AF_INET6     by 10;
%replace SOCK_STREAM  by  1;
%replace SOCK_DGRAM   by  2;
%replace IPPROTO_TCP  by  6;
%replace IPPROTO_UDP  by 17;

/* --- shutdown(2) directions ------------------------------------------------ */
%replace SHUT_RD      by 0;
%replace SHUT_WR      by 1;
%replace SHUT_RDWR    by 2;

/* --- poll(2) event bits (mirrored in c_bridge.c) --------------------------- */
%replace POLLIN       by 1;
%replace POLLOUT      by 2;

/* --- libnet-internal status codes (distinct from errno) -------------------- */
%replace NET_OK        by  0;   /* success (also the count 0 for writes) */
%replace NET_ERR       by -1;   /* hard error; condition neterror raised */
%replace NET_EOF       by -2;   /* peer closed / end of stream          */
%replace NET_TIMEOUT   by -3;   /* read/write timeout or EAGAIN/EINTR   */
%replace NET_OVERFLOW  by -4;   /* caller buffer exhausted mid-stream   */

/* --- socket option codes for net_setopt (mapped to syscalls in C) ---------- */
%replace NETOPT_REUSEADDR by 0;   /* SO_REUSEADDR */
%replace NETOPT_KEEPALIVE  by 1;  /* SO_KEEPALIVE */
%replace NETOPT_NODELAY    by 2;  /* TCP_NODELAY  */

/* --- shutdown(2) directions are SHUT_* above; UDP needs none --------------- */
%replace MSG_DONTWAIT  by 64;     /* send/recv flag: nonblocking operation */

/* --- default buffer capacities --------------------------------------------- */
%replace NET_BUF_CAP   by 4096;
%replace NET_HOST_MAX  by 256;
