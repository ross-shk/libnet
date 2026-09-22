/* net_base.inc — libnet client API, PL/I-centric.
 *
 * Module:  net_base
 * Author:  libnet
 * Purpose: the bulk of libnet. Owns a CONTROLLED connection record per
 *          socket (private to this include), and provides a handle-based
 *          client API. Callers hold only a POINTER handle and never see the
 *          layout — the classic PL/I "task context" pattern.
 *
 * Error handling (conditions-only — the idiomatic PL/I mechanism):
 *   libnet reports ALL failures through conditions, never return codes.
 *   A returned value is DATA only: a byte count, a delimiter position, a
 *   readiness mask, or a pointer handle. A procedure that can fail raises
 *   one of these conditions, which the caller intercepts with ON:
 *
 *       condition(neterror)    hard error;          oncode() = errno
 *       condition(nettimeout)  timeout / EAGAIN;    oncode() = NET_TIMEOUT
 *       condition(netEOF)      peer closed;         oncode() = NET_EOF
 *       condition(netOverflow) caller buffer full;  oncode() = NET_OVERFLOW
 *
 *   Pointer-returning procedures return the null handle on failure (after
 *   raising the appropriate condition).
 *
 * Calling sequence (all procedures take a handle produced by net_open):
 *   h  = net_open( family, type, proto )            returns pointer
 *   call net_connect( h, host, port )
 *   call net_dial( h, hostport, family )
 *   call net_connect_nb( h, host, port )
 *   call net_connect_finish( h )
 *   call net_close( h )
 *   call net_shutdown( h, how )
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
 *   call net_set_timeout( h, rto, wto )
 *   call net_set_nonblocking( h, on )
 *   call net_setopt( h, opt, value )
 *   call net_set_linger( h, on, seconds )
 *   call net_peer( h, ip, port )
 *   call net_local( h, ip, port )
 *   call net_resolve( host, ip )
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
/* Internal error helpers (raise conditions — the only error channel)       */
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

/* Raise netEOF (peer closed / end of stream). */
raise_eof: procedure;
   signal condition(netEOF) set oncode(NET_EOF);
end raise_eof;

/* Raise netOverflow (caller buffer exhausted mid-stream). */
raise_overflow: procedure;
   signal condition(netOverflow) set oncode(NET_OVERFLOW);
end raise_overflow;

/*--------------------------------------------------------------------------*/
/* net_open                                                                  */
/*--------------------------------------------------------------------------*/
/* Allocate a connection (CONTROLLED push) and create a socket.
 *
 * returns:  a non-null POINTER handle on success;
 *           the null handle on failure (condition neterror raised).
 *
 * raises:   condition(neterror) if the socket could not be created.
 *
 * family:   AF_INET / AF_INET6;  type: SOCK_STREAM / SOCK_DGRAM;
 *           proto: IPPROTO_TCP / IPPROTO_UDP / 0. */
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
/* Connect an opened connection to host:port.
 *
 * raises:   condition(neterror) on connect failure.
 *
 * host is a host name or dotted quad; it is resolved via getaddrinfo. On
 * success the peer_* fields are filled. */
net_connect: procedure (h, host, port);
   dcl h    pointer;
   dcl host char(*);
   dcl port fixed bin(31);
   dcl rc   fixed bin(31);

   pconn = h;
   rc = netc_connect(conn_view.fd, host, length(host), port);
   if rc < 0 then do;
      call raise_err();
      return;
   end;
   conn_view.is_connected = '1'b;
   conn_view.peer_port    = port;
end net_connect;

/* Resolve host:port ("127.0.0.1:8090" or hostname:port) and connect.
 *
 * raises:   condition(neterror) on a malformed host:port or connect failure. */
net_dial: procedure (h, hostport, family);
   dcl h        pointer;
   dcl hostport char(*);
   dcl family   fixed bin(31);
   dcl colon    fixed bin(31);
   dcl hoststr  char(NET_HOST_MAX);
   dcl portstr  char(6);
   dcl port     fixed bin(31);

   colon = index(hostport, ':');
   if colon = 0 then do;
      signal condition(neterror) set oncode(EINVAL);
      return;
   end;
   hoststr = substr(hostport, 1, colon - 1);
   portstr = substr(hostport, colon + 1, length(hostport) - colon);
   port    = fixed(trim(portstr));
   h = net_open(family, SOCK_STREAM, 0);
   if h = null() then return;
   call net_connect(h, trim(hoststr), port);
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

/* Shut down one direction of the connection.
 * raises:   condition(neterror) on failure. */
net_shutdown: procedure (h, how);
   dcl h   pointer;
   dcl how fixed bin(31);
   dcl rc  fixed bin(31);
   pconn = h;
   rc = netc_shutdown(conn_view.fd, how);
   if rc < 0 then call raise_err();
end net_shutdown;

/*--------------------------------------------------------------------------*/
/* net_read family (multi-entry parametrization)                             */
/*--------------------------------------------------------------------------*/
/* Read up to buflen bytes into buffer.
 *
 * returns:  the number of bytes read (0 <= n <= buflen).
 *
 * raises:   condition(netEOF) if the peer closed the stream;
 *           condition(nettimeout) on timeout/EAGAIN;
 *           condition(neterror) on a hard error. */
net_read: procedure (h, buffer, buflen) returns (fixed bin(31));
   dcl h      pointer;
   dcl buffer char(*);
   dcl buflen fixed bin(31);
   dcl got    fixed bin(31);

   pconn = h;
   if conn_view.fd <= 0 then do;
      signal condition(neterror) set oncode(EBADF);
      return 0;
   end;

   got = netc_recv(conn_view.fd, buffer, buflen, 0);
   if got = 0 then do;
      call raise_eof();
      return 0;
   end;
   if got < 0 then do;
      if netc_errno() = EAGAIN then call raise_timeout();
      else call raise_err();
      return 0;
   end;
   return got;
end net_read;

/* Read repeatedly until the peer closes or an error/timeout, appending all
 * received bytes to buffer.
 *
 * returns:  the total number of bytes read. EOF is the normal end, reported
 *           as the byte count accumulated so far (no condition).
 *
 * raises:   condition(nettimeout), condition(neterror), or
 *           condition(netOverflow) if the caller buffer filled before EOF. */
net_read_all: entry (h, buffer) returns (fixed bin(31));
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
      if cap <= 0 then do;            /* caller buffer full */
         call raise_overflow();
         return total;
      end;
      got = netc_recv(conn_view.fd, tmp, cap, 0);
      if got = 0 then do;             /* EOF: normal end */
         done = '1'b;
         return total;
      end;
      if got < 0 then do;
         if netc_errno() = EAGAIN then call raise_timeout();
         else call raise_err();
         return total;
      end;
      buffer = substr(buffer, 1, total) || substr(tmp, 1, got);
      total  = total + got;
   end; /* do while */
   return total;
end net_read_all;

/* Read until a delimiter character is seen, appending received bytes to
 * buffer.
 *
 * returns:  the position of the first delimiter in buffer (1..len).
 *
 * raises:   condition(netEOF) if the peer closed before a delimiter;
 *           condition(nettimeout), condition(neterror), or
 *           condition(netOverflow) if the caller buffer filled. */
net_read_until: entry (h, buffer, delim) returns (fixed bin(31));
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
      if cap <= 0 then do;            /* caller buffer full, no delimiter */
         call raise_overflow();
         return 0;
      end;
      got = netc_recv(conn_view.fd, tmp, cap, 0);
      if got = 0 then do;
         call raise_eof();
         return 0;
      end;
      if got < 0 then do;
         if netc_errno() = EAGAIN then call raise_timeout();
         else call raise_err();
         return 0;
      end;
      buffer = substr(buffer, 1, total) || substr(tmp, 1, got);
      total  = total + got;
      pos = index(buffer, delim);
      if pos > 0 then return pos;
   end; /* do while */
   return 0;
end net_read_until;

/*--------------------------------------------------------------------------*/
/* net_write / net_send family (multi-entry parametrization)                 */
/*--------------------------------------------------------------------------*/
/* Send buffer verbatim (flags = 0).
 *
 * returns:  the number of bytes sent (0 <= n <= length(buffer)).
 *
 * raises:   condition(nettimeout) on timeout/EAGAIN;
 *           condition(neterror) on a hard error.
 *
 * net_send / net_send_all / net_send_once are ENTRY points sharing the body
 * with the same RETURNS attributes. */
net_write: procedure (h, buffer) returns (fixed bin(31));
   dcl h      pointer;
   dcl buffer char(*);
   dcl n      fixed bin(31);

   pconn = h;
   n = netc_send(conn_view.fd, buffer, length(buffer), 0);
   if n < 0 then do;
      if netc_errno() = EAGAIN then call raise_timeout();
      else call raise_err();
      return 0;
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
      if netc_errno() = EAGAIN then call raise_timeout();
      else call raise_err();
      return 0;
   end;
   return n;
end net_send;

/* Send the whole buffer, retrying on partial sends.
 *
 * returns:  the total number of bytes sent.
 *
 * raises:   condition(neterror) on a hard (non-EAGAIN) error. */
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
         return total;
      end;
      if n = 0 then iterate;
      total = total + n;
   end; /* do while */
   return total;
end net_send_all;

/* A single send() with optional flags; no retry.
 *
 * returns:  the number of bytes sent.
 *
 * raises:   condition(nettimeout) / condition(neterror). */
net_send_once: entry (h, buffer, flags) returns (fixed bin(31));
   dcl h      pointer;
   dcl buffer char(*);
   dcl flags  fixed bin(31);
   dcl n      fixed bin(31);

   pconn = h;
   n = netc_send(conn_view.fd, buffer, length(buffer), flags);
   if n < 0 then do;
      if netc_errno() = EAGAIN then call raise_timeout();
      else call raise_err();
      return 0;
   end;
   return n;
end net_send_once;

/*--------------------------------------------------------------------------*/
/* net_poll / net_set_timeout / net_set_nonblocking                          */
/*--------------------------------------------------------------------------*/
/* Poll for readiness.
 *
 * returns:  the ready mask (POLLIN/POLLOUT), or 0 on timeout. A timeout is
 *           NOT an error for poll — it is the normal "nothing ready" result.
 *
 * raises:   condition(neterror) on a hard error. */
net_poll: procedure (h, events, ms) returns (fixed bin(31));
   dcl h      pointer;
   dcl events fixed bin(31);
   dcl ms     fixed bin(31);
   dcl r      fixed bin(31);

   pconn = h;
   r = netc_poll(conn_view.fd, events, ms);
   if r < 0 then call raise_err();
   return r;
end net_poll;

/* Set read/write timeouts (ms; 0 = blocking).
 * raises:   condition(neterror) on failure. */
net_set_timeout: procedure (h, rto, wto);
   dcl h   pointer;
   dcl rto fixed bin(31);
   dcl wto fixed bin(31);
   dcl rc  fixed bin(31);

   pconn = h;
   rc = netc_settimeout(conn_view.fd, rto, wto);
   if rc < 0 then do;
      call raise_err();
      return;
   end;
   conn_view.read_timeout  = rto;
   conn_view.write_timeout = wto;
end net_set_timeout;

/* Enable or disable nonblocking I/O.
 * raises:   condition(neterror) on failure. */
net_set_nonblocking: procedure (h, on);
   dcl h  pointer;
   dcl on bit(1);
   dcl rc fixed bin(31);

   pconn = h;
   if on then rc = netc_setnonblock(conn_view.fd, 1);
   else        rc = netc_setnonblock(conn_view.fd, 0);
   if rc < 0 then call raise_err();
end net_set_nonblocking;

/*--------------------------------------------------------------------------*/
/* net_setopt                                                                */
/*--------------------------------------------------------------------------*/
/* Set a socket option by libnet code (NETOPT_REUSEADDR / NETOPT_KEEPALIVE /
 * NETOPT_NODELAY). value is 0 (off) or 1 (on).
 * raises:   condition(neterror) on failure. */
net_setopt: procedure (h, opt, value);
   dcl h     pointer;
   dcl opt   fixed bin(31);
   dcl value fixed bin(31);
   dcl rc    fixed bin(31);

   pconn = h;
   rc = netc_sockopt(conn_view.fd, opt, value);
   if rc < 0 then call raise_err();
end net_setopt;

/* SO_LINGER: enable/disable with a timeout in seconds.
 * raises:   condition(neterror) on failure. */
net_set_linger: procedure (h, on, seconds);
   dcl h       pointer;
   dcl on      fixed bin(31);
   dcl seconds fixed bin(31);
   dcl rc      fixed bin(31);

   pconn = h;
   rc = netc_setlinger(conn_view.fd, on, seconds);
   if rc < 0 then call raise_err();
end net_set_linger;

/*--------------------------------------------------------------------------*/
/* net_peer / net_local                                                      */
/*--------------------------------------------------------------------------*/
/* Fill ip/port with the peer (remote) address.
 * raises:   condition(neterror) on failure. */
net_peer: procedure (h, ip, port);
   dcl h    pointer;
   dcl ip   char(*);
   dcl port fixed bin(31);
   dcl rc   fixed bin(31);

   pconn = h;
   rc = netc_getpeername(conn_view.fd, ip, maxlength(ip), addr(port));
   if rc < 0 then call raise_err();
end net_peer;

/* Fill ip/port with the local (bound) address.
 * raises:   condition(neterror) on failure. */
net_local: procedure (h, ip, port);
   dcl h    pointer;
   dcl ip   char(*);
   dcl port fixed bin(31);
   dcl rc   fixed bin(31);

   pconn = h;
   rc = netc_getsockname(conn_view.fd, ip, maxlength(ip), addr(port));
   if rc < 0 then call raise_err();
end net_local;

/*--------------------------------------------------------------------------*/
/* net_resolve                                                               */
/*--------------------------------------------------------------------------*/
/* Resolve a host name (or dotted quad) to a dotted-quad string in ip.
 * raises:   condition(neterror) on resolution failure. */
net_resolve: procedure (host, ip);
   dcl host char(*);
   dcl ip   char(*);
   dcl rc   fixed bin(31);

   rc = netc_resolve(host, length(host), ip, maxlength(ip));
   if rc < 0 then call raise_err();
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
/* Start a nonblocking connect (the fd is made nonblocking). If the connect
 * completes immediately, is_connected is set. If it is still in progress,
 * the procedure simply returns — the caller polls for writability and then
 * calls net_connect_finish. No condition is raised for in-progress (it is
 * not an error).
 *
 * raises:   condition(neterror) on a hard connect failure. */
net_connect_nb: procedure (h, host, port);
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
      return;
   end;
   if rc = -1 then do;                 /* EINPROGRESS */
      call netc_clearerr();
      return;
   end;
   call raise_err();
end net_connect_nb;

/* Finalize a nonblocking connect after the fd became writable.
 * raises:   condition(neterror) if the connect ultimately failed. */
net_connect_finish: procedure (h);
   dcl h  pointer;
   dcl e  fixed bin(31);
   dcl rc fixed bin(31);

   pconn = h;
   e = 0;
   rc = netc_getsockerr(conn_view.fd, addr(e));
   if rc < 0 then do;
      call raise_err();
      return;
   end;
   if e ^= 0 then do;
      signal condition(neterror) set oncode(e);
      return;
   end;
   conn_view.is_connected = '1'b;
end net_connect_finish;

/*--------------------------------------------------------------------------*/
/* UDP datagrams                                                             */
/*--------------------------------------------------------------------------*/
/* Send a datagram to ip:port on this (datagram) connection.
 *
 * returns:  the number of bytes sent.
 *
 * raises:   condition(nettimeout) on EAGAIN; condition(neterror) otherwise. */
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
      if netc_errno() = EAGAIN then call raise_timeout();
      else call raise_err();
      return 0;
   end;
   return n;
end net_sendto;

/* Receive a datagram; the sender's address is returned in ip/port.
 *
 * returns:  the number of bytes received.
 *
 * raises:   condition(nettimeout) on EAGAIN; condition(neterror) otherwise. */
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
      if netc_errno() = EAGAIN then call raise_timeout();
      else call raise_err();
      return 0;
   end;
   return n;
end net_recvfrom;
