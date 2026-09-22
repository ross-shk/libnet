/* net_base.inc — libnet client API, PL/I-centric.
 *
 * Module:  net_base
 * Author:  libnet
 * Purpose: the bulk of libnet. Owns a CONTROLLED connection record per
 *          socket (private to this include), and provides a handle-based
 *          client API. Callers hold only a POINTER handle and never see the
 *          layout — the classic PL/I "task context" pattern.
 *
 * Calling sequence (all procedures take a handle produced by net_open):
 *   h  = net_open( family, type, proto )            returns pointer
 *   rc = net_connect( h, host, port )               returns fixed bin(31)
 *   rc = net_dial( h, hostport, family )            returns fixed bin(31)
 *   rc = net_connect_nb( h, host, port )            returns fixed bin(31)
 *   rc = net_connect_finish( h )                    returns fixed bin(31)
 *   call net_close( h )
 *   rc = net_shutdown( h, how )                     returns fixed bin(31)
 *   n  = net_read( h, buf, buflen )                 returns fixed bin(31)
 *   n  = net_read_all( h, buf )                     returns fixed bin(31)
 *   n  = net_read_until( h, buf, delim )            returns fixed bin(31)
 *   n  = net_write( h, buf )                        returns fixed bin(31)
 *   n  = net_send( h, buf, flags )                  returns fixed bin(31)
 *   n  = net_send_all( h, buf )                     returns fixed bin(31)
 *   n  = net_send_once( h, buf, flags )             returns fixed bin(31)
 *   n  = net_sendto( h, buf, ip, port )             returns fixed bin(31) [UDP]
 *   n  = net_recvfrom( h, buf, ip, port )           returns fixed bin(31) [UDP]
 *   r  = net_poll( h, events, ms )                  returns fixed bin(31)
 *   rc = net_set_timeout( h, rto, wto )             returns fixed bin(31)
 *   rc = net_set_nonblocking( h, on )               returns fixed bin(31)
 *   rc = net_setopt( h, opt, value )                returns fixed bin(31)
 *   rc = net_set_linger( h, on, seconds )           returns fixed bin(31)
 *   rc = net_peer( h, ip, port )                    returns fixed bin(31)
 *   rc = net_local( h, ip, port )                   returns fixed bin(31)
 *   rc = net_resolve( host, ip )                    returns fixed bin(31)
 *   call net_strerror( e, buf )
 *
 * Written against the pli-llvm wishlist (see WISHLIST.md): CONTROLLED
 * storage, variable-length SUBSTR, char(*), BASED(P) on params, and
 * based-member RETURN.
 */

/* Shared declarations are supplied by the master net.inc. */

/*--------------------------------------------------------------------------*/
/* Private connection record (CONTROLLED, library-owned)                    */
/*--------------------------------------------------------------------------*/
dcl 1 conn_rec controlled aligned,
   2 fd               fixed bin(31),
   2 is_connected     bit(1),
   2 read_timeout     fixed bin(31),   /* ms; 0 = blocking */
   2 write_timeout    fixed bin(31),   /* ms; 0 = blocking */
   2 peer_ip          char(16),
   2 peer_port        fixed bin(31),
   2 inbuf            char(NET_BUF_CAP),
   2 in_len           fixed bin(31);   /* bytes currently buffered */

/* A based view of the same layout, declared on a local pointer, so each
 * method can address a caller-supplied handle. */
dcl 1 conn_view based(pconn),
   2 fd               fixed bin(31),
   2 is_connected     bit(1),
   2 read_timeout     fixed bin(31),
   2 write_timeout    fixed bin(31),
   2 peer_ip          char(16),
   2 peer_port        fixed bin(31),
   2 inbuf            char(NET_BUF_CAP),
   2 in_len           fixed bin(31);
dcl pconn pointer;

/* Declare the builtins used throughout. */
dcl (addr, null, length, maxlength, min, index, substr, trim, fixed)
                                                builtin;

/*--------------------------------------------------------------------------*/
/* Internal error helpers                                                    */
/*--------------------------------------------------------------------------*/
/* Raise neterror with the current errno, unless 0. */
raise_err: procedure;
   dcl e fixed bin(31);
   e = netc_errno();
   call netc_clearerr();
   if e ^= 0 then signal condition(neterror) set oncode(e);
end raise_err;

/* Raise nettimeout (EAGAIN/EINTR/timeout expiry). */
raise_timeout: procedure;
   signal condition(nettimeout) set oncode(NET_TIMEOUT);
end raise_timeout;

/*--------------------------------------------------------------------------*/
/* net_open                                                                  */
/*--------------------------------------------------------------------------*/
/* Allocate a connection (CONTROLLED push), create the socket, return a
 * pointer handle. On failure raise neterror and return the null handle. */
net_open: procedure (family, type, proto) returns (pointer);
   dcl family fixed bin(31);
   dcl type   fixed bin(31);
   dcl proto  fixed bin(31);
   dcl h      pointer;

   allocate conn_rec;                /* implicit push — CONTROLLED */
   conn_rec.fd            = 0;
   conn_rec.is_connected  = '0'b;
   conn_rec.read_timeout  = 0;
   conn_rec.write_timeout = 0;
   conn_rec.peer_ip       = '';
   conn_rec.peer_port     = 0;
   conn_rec.in_len        = 0;

   conn_rec.fd = netc_socket(family, type, proto);
   if conn_rec.fd < 0 then do;
      call raise_err();
      h = null();
      return h;
   end;

   h = addr(conn_rec);
   return h;
end net_open;

/*--------------------------------------------------------------------------*/
/* net_connect / net_dial                                                    */
/*--------------------------------------------------------------------------*/
/* Connect an opened connection to host:port. Fills peer_* on success. */
net_connect: procedure (h, host, port) returns (fixed bin(31));
   dcl h    pointer;
   dcl host char(*);
   dcl port fixed bin(31);
   dcl rc   fixed bin(31);

   pconn = h;
   rc = netc_connect(conn_view.fd, host, length(host), port);
   if rc < 0 then do;
      call raise_err();
      return NET_ERR;
   end;
   conn_view.is_connected = '1'b;
   conn_view.peer_port    = port;
   return NET_OK;
end net_connect;

/* Resolve host:port ("127.0.0.1:8090" or hostname:port) and connect. */
net_dial: procedure (h, hostport, family) returns (fixed bin(31));
   dcl h        pointer;
   dcl hostport char(*);
   dcl family   fixed bin(31);
   dcl colon    fixed bin(31);
   dcl hoststr  char(NET_HOST_MAX);
   dcl portstr  char(6);
   dcl port     fixed bin(31);
   dcl rc       fixed bin(31);

   colon = index(hostport, ':');
   if colon = 0 then return NET_ERR;
   hoststr = substr(hostport, 1, colon - 1);
   portstr = substr(hostport, colon + 1, length(hostport) - colon);
   port    = fixed(trim(portstr));
   h = net_open(family, SOCK_STREAM, 0);
   if h = null() then return NET_ERR;
   rc = net_connect(h, trim(hoststr), port);
   return rc;
end net_dial;

/*--------------------------------------------------------------------------*/
/* net_close / net_shutdown                                                  */
/*--------------------------------------------------------------------------*/
net_close: procedure (h);
   dcl h pointer;
   pconn = h;
   if conn_view.fd > 0 then call netc_close(conn_view.fd);
   conn_view.fd            = 0;
   conn_view.is_connected  = '0'b;
   conn_view.in_len        = 0;
   free conn_rec;                    /* CONTROLLED pop */
end net_close;

net_shutdown: procedure (h, how) returns (fixed bin(31));
   dcl h   pointer;
   dcl how fixed bin(31);
   dcl rc  fixed bin(31);
   pconn = h;
   rc = netc_shutdown(conn_view.fd, how);
   if rc < 0 then do;
      call raise_err();
      return NET_ERR;
   end;
   return NET_OK;
end net_shutdown;

/*--------------------------------------------------------------------------*/
/* net_read family (multi-entry parametrization)                             */
/*--------------------------------------------------------------------------*/
/* Read up to buflen bytes. Returns bytes read, NET_EOF (peer closed),
 * NET_TIMEOUT, or NET_ERR. net_read_all / net_read_until are ENTRY points
 * sharing this body. */
net_read: procedure (h, buffer, buflen) returns (fixed bin(31));
   dcl h      pointer;
   dcl buffer char(*);
   dcl buflen fixed bin(31);
   dcl got    fixed bin(31);

   pconn = h;
   if conn_view.fd <= 0 then do;
      signal condition(neterror) set oncode(EBADF);
      return NET_ERR;
   end;

   got = netc_recv(conn_view.fd, buffer, buflen, 0);
   if got = 0 then return NET_EOF;
   if got < 0 then do;
      if netc_errno() = EAGAIN then do;
         call raise_timeout();
         return NET_TIMEOUT;
      end;
      call raise_err();
      return NET_ERR;
   end;
   return got;
end net_read;

/* Read repeatedly until the peer closes or an error/timeout. Appends all
 * received bytes to buffer via the variable-length slice idiom. */
net_read_all: entry (h, buffer);
   dcl h      pointer;
   dcl buffer char(*);
   dcl tmp    char(NET_BUF_CAP);
   dcl total  fixed bin(31);
   dcl got    fixed bin(31);
   dcl cap    fixed bin(31);
   dcl done   bit(1);

   pconn = h;
   total = 0;
   done  = '0'b;
   do while (^done);
      cap = min(NET_BUF_CAP, maxlength(buffer) - total);
      if cap <= 0 then return total;  /* caller buffer full */
      got = netc_recv(conn_view.fd, tmp, cap, 0);
      if got = 0 then do;             /* EOF */
         done = '1'b;
         return total;
      end;
      if got < 0 then do;
         if netc_errno() = EAGAIN then do;
            call raise_timeout();
            return NET_TIMEOUT;
         end;
         call raise_err();
         return NET_ERR;
      end;
      buffer = substr(buffer, 1, total) || substr(tmp, 1, got);
      total  = total + got;
   end; /* do while */
   return total;
end net_read_all;

/* Read until a delimiter character is seen. Returns bytes read up to and
 * including the delimiter, or NET_EOF/NET_TIMEOUT/NET_ERR. */
net_read_until: entry (h, buffer, delim);
   dcl h      pointer;
   dcl buffer char(*);
   dcl delim  char(1);
   dcl total  fixed bin(31);
   dcl pos    fixed bin(31);
   dcl got    fixed bin(31);
   dcl tmp    char(NET_BUF_CAP);
   dcl cap    fixed bin(31);
   dcl done   bit(1);

   pconn = h;
   total = 0;
   done  = '0'b;
   do while (^done);
      cap = min(NET_BUF_CAP, maxlength(buffer) - total);
      if cap <= 0 then return total;  /* caller buffer full, no delimiter */
      got = netc_recv(conn_view.fd, tmp, cap, 0);
      if got = 0 then return NET_EOF;
      if got < 0 then do;
         if netc_errno() = EAGAIN then do;
            call raise_timeout();
            return NET_TIMEOUT;
         end;
         call raise_err();
         return NET_ERR;
      end;
      buffer = substr(buffer, 1, total) || substr(tmp, 1, got);
      total  = total + got;
      pos = index(buffer, delim);
      if pos > 0 then return pos;
   end; /* do while */
   return total;
end net_read_until;

/*--------------------------------------------------------------------------*/
/* net_write / net_send family (multi-entry parametrization)                 */
/*--------------------------------------------------------------------------*/
/* Send buffer verbatim. Returns bytes sent, or NET_ERR/NET_TIMEOUT. */
net_write: procedure (h, buffer) returns (fixed bin(31));
   dcl h      pointer;
   dcl buffer char(*);
   dcl n      fixed bin(31);

   pconn = h;
   n = netc_send(conn_view.fd, buffer, length(buffer), 0);
   if n < 0 then do;
      if netc_errno() = EAGAIN then do;
         call raise_timeout();
         return NET_TIMEOUT;
      end;
      call raise_err();
      return NET_ERR;
   end;
   return n;
end net_write;

net_send: entry (h, buffer, flags) returns (fixed bin(31));
   dcl h      pointer;
   dcl buffer char(*);
   dcl flags  fixed bin(31);
   dcl n      fixed bin(31);

   pconn = h;
   n = netc_send(conn_view.fd, buffer, length(buffer), flags);
   if n < 0 then do;
      if netc_errno() = EAGAIN then do;
         call raise_timeout();
         return NET_TIMEOUT;
      end;
      call raise_err();
      return NET_ERR;
   end;
   return n;
end net_send;

/* Send the whole buffer, retrying on partial sends. Returns total bytes
 * sent, or NET_ERR. */
net_send_all: entry (h, buffer) returns (fixed bin(31));
   dcl h      pointer;
   dcl buffer char(*);
   dcl total  fixed bin(31);
   dcl want   fixed bin(31);
   dcl n      fixed bin(31);
   dcl chunk  char(NET_BUF_CAP);
   dcl take   fixed bin(31);

   pconn = h;
   total = 0;
   want  = length(buffer);
   do while (total < want);
      take  = min(NET_BUF_CAP, want - total);
      chunk = substr(buffer, total + 1, take);
      n = netc_send(conn_view.fd, chunk, take, 0);
      if n < 0 then do;
         if netc_errno() = EAGAIN then iterate;
         call raise_err();
         return NET_ERR;
      end;
      if n = 0 then iterate;
      total = total + n;
   end; /* do while */
   return total;
end net_send_all;

/* A single send() with optional flags; no retry. */
net_send_once: entry (h, buffer, flags) returns (fixed bin(31));
   dcl h      pointer;
   dcl buffer char(*);
   dcl flags  fixed bin(31);
   dcl n      fixed bin(31);

   pconn = h;
   n = netc_send(conn_view.fd, buffer, length(buffer), flags);
   if n < 0 then do;
      if netc_errno() = EAGAIN then do;
         call raise_timeout();
         return NET_TIMEOUT;
      end;
      call raise_err();
      return NET_ERR;
   end;
   return n;
end net_send_once;

/*--------------------------------------------------------------------------*/
/* net_poll / net_set_timeout / net_set_nonblocking                          */
/*--------------------------------------------------------------------------*/
net_poll: procedure (h, events, ms) returns (fixed bin(31));
   dcl h      pointer;
   dcl events fixed bin(31);
   dcl ms     fixed bin(31);
   dcl r      fixed bin(31);

   pconn = h;
   r = netc_poll(conn_view.fd, events, ms);
   if r < 0 then do;
      call raise_err();
      return NET_ERR;
   end;
   return r;
end net_poll;

net_set_timeout: procedure (h, rto, wto) returns (fixed bin(31));
   dcl h   pointer;
   dcl rto fixed bin(31);
   dcl wto fixed bin(31);
   dcl rc  fixed bin(31);

   pconn = h;
   rc = netc_settimeout(conn_view.fd, rto, wto);
   if rc < 0 then do;
      call raise_err();
      return NET_ERR;
   end;
   conn_view.read_timeout  = rto;
   conn_view.write_timeout = wto;
   return NET_OK;
end net_set_timeout;

net_set_nonblocking: procedure (h, on) returns (fixed bin(31));
   dcl h  pointer;
   dcl on bit(1);
   dcl rc fixed bin(31);

   pconn = h;
   if on then rc = netc_setnonblock(conn_view.fd, 1);
   else        rc = netc_setnonblock(conn_view.fd, 0);
   if rc < 0 then do;
      call raise_err();
      return NET_ERR;
   end;
   return NET_OK;
end net_set_nonblocking;

/*--------------------------------------------------------------------------*/
/* net_setopt                                                                */
/*--------------------------------------------------------------------------*/
/* Set a socket option by libnet code (NETOPT_REUSEADDR / NETOPT_KEEPALIVE /
 * NETOPT_NODELAY). value is 0 (off) or 1 (on). Returns NET_OK or NET_ERR. */
net_setopt: procedure (h, opt, value) returns (fixed bin(31));
   dcl h     pointer;
   dcl opt   fixed bin(31);
   dcl value fixed bin(31);
   dcl rc    fixed bin(31);

   pconn = h;
   rc = netc_sockopt(conn_view.fd, opt, value);
   if rc < 0 then do;
      call raise_err();
      return NET_ERR;
   end;
   return NET_OK;
end net_setopt;

/* SO_LINGER: enable/disable with a timeout in seconds. */
net_set_linger: procedure (h, on, seconds) returns (fixed bin(31));
   dcl h       pointer;
   dcl on      fixed bin(31);
   dcl seconds fixed bin(31);
   dcl rc      fixed bin(31);

   pconn = h;
   rc = netc_setlinger(conn_view.fd, on, seconds);
   if rc < 0 then do;
      call raise_err();
      return NET_ERR;
   end;
   return NET_OK;
end net_set_linger;

/*--------------------------------------------------------------------------*/
/* net_peer / net_local                                                      */
/*--------------------------------------------------------------------------*/
net_peer: procedure (h, ip, port) returns (fixed bin(31));
   dcl h    pointer;
   dcl ip   char(*);
   dcl port fixed bin(31);
   dcl rc   fixed bin(31);

   pconn = h;
   rc = netc_getpeername(conn_view.fd, ip, maxlength(ip), addr(port));
   if rc < 0 then do;
      call raise_err();
      return NET_ERR;
   end;
   return NET_OK;
end net_peer;

/* Local (bound) address into ip/port. */
net_local: procedure (h, ip, port) returns (fixed bin(31));
   dcl h    pointer;
   dcl ip   char(*);
   dcl port fixed bin(31);
   dcl rc   fixed bin(31);

   pconn = h;
   rc = netc_getsockname(conn_view.fd, ip, maxlength(ip), addr(port));
   if rc < 0 then do;
      call raise_err();
      return NET_ERR;
   end;
   return NET_OK;
end net_local;

/*--------------------------------------------------------------------------*/
/* net_resolve                                                               */
/*--------------------------------------------------------------------------*/
/* Resolve a host name (or dotted quad) to a dotted-quad string in ip.
 * Returns NET_OK or NET_ERR. */
net_resolve: procedure (host, ip) returns (fixed bin(31));
   dcl host char(*);
   dcl ip   char(*);
   dcl rc   fixed bin(31);

   rc = netc_resolve(host, length(host), ip, maxlength(ip));
   if rc < 0 then do;
      call raise_err();
      return NET_ERR;
   end;
   return NET_OK;
end net_resolve;

/*--------------------------------------------------------------------------*/
/* net_strerror                                                              */
/*--------------------------------------------------------------------------*/
/* Human-readable text for an errno value, into buf. */
net_strerror: procedure (e, buf);
   dcl e   fixed bin(31);
   dcl buf char(*);
   call netc_strerror(e, buf, maxlength(buf));
end net_strerror;

/*--------------------------------------------------------------------------*/
/* net_connect_nb                                                            */
/*--------------------------------------------------------------------------*/
/* Start a nonblocking connect. Returns NET_OK if completed immediately,
 * NET_TIMEOUT if in progress (caller must poll for writability and then call
 * net_connect_finish), NET_ERR on failure. The fd is made nonblocking. */
net_connect_nb: procedure (h, host, port) returns (fixed bin(31));
   dcl h    pointer;
   dcl host char(*);
   dcl port fixed bin(31);
   dcl rc   fixed bin(31);

   pconn = h;
   call netc_setnonblock(conn_view.fd, 1);
   rc = netc_connect_nb(conn_view.fd, host, length(host), port);
   if rc = 0 then do;
      conn_view.is_connected = '1'b;
      conn_view.peer_port    = port;
      return NET_OK;
   end;
   if rc = -1 then do;                 /* EINPROGRESS */
      call netc_clearerr();
      return NET_TIMEOUT;
   end;
   call raise_err();
   return NET_ERR;
end net_connect_nb;

/* Finalize a nonblocking connect after the fd became writable. Returns
 * NET_OK on success, NET_ERR on failure. */
net_connect_finish: procedure (h) returns (fixed bin(31));
   dcl h  pointer;
   dcl e  fixed bin(31);
   dcl rc fixed bin(31);

   pconn = h;
   e = 0;
   rc = netc_getsockerr(conn_view.fd, addr(e));
   if rc < 0 then do;
      call raise_err();
      return NET_ERR;
   end;
   if e ^= 0 then do;
      signal condition(neterror) set oncode(e);
      return NET_ERR;
   end;
   conn_view.is_connected = '1'b;
   return NET_OK;
end net_connect_finish;

/*--------------------------------------------------------------------------*/
/* UDP datagrams                                                             */
/*--------------------------------------------------------------------------*/
/* Send a datagram to ip:port on this (datagram) connection. Returns bytes
 * sent, NET_ERR, or NET_TIMEOUT on EAGAIN. */
net_sendto: procedure (h, buffer, ip, port) returns (fixed bin(31));
   dcl h      pointer;
   dcl buffer char(*);
   dcl ip     char(*);
   dcl port   fixed bin(31);
   dcl n      fixed bin(31);

   pconn = h;
   n = netc_sendto(conn_view.fd, buffer, length(buffer), 0,
                ip, length(ip), port);
   if n < 0 then do;
      if netc_errno() = EAGAIN then do;
         call raise_timeout();
         return NET_TIMEOUT;
      end;
      call raise_err();
      return NET_ERR;
   end;
   return n;
end net_sendto;

/* Receive a datagram; the sender's address is returned in ip/port. Returns
 * bytes received, NET_ERR, or NET_TIMEOUT on EAGAIN. */
net_recvfrom: procedure (h, buffer, ip, port) returns (fixed bin(31));
   dcl h      pointer;
   dcl buffer char(*);
   dcl ip     char(*);
   dcl port   fixed bin(31);
   dcl n      fixed bin(31);

   pconn = h;
   n = netc_recvfrom(conn_view.fd, buffer, maxlength(buffer), 0,
                  ip, maxlength(ip), addr(port));
   if n < 0 then do;
      if netc_errno() = EAGAIN then do;
         call raise_timeout();
         return NET_TIMEOUT;
      end;
      call raise_err();
      return NET_ERR;
   end;
   return n;
end net_recvfrom;
