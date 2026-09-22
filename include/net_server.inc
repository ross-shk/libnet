/* net_server.inc — libnet server API, PL/I-centric.
 *
 * Module:  net_server
 * Author:  libnet
 * Purpose: thin server API on the same library-owned CONTROLLED connection
 *          model. net_listen prepares a listening socket and returns a server
 *          handle; net_accept accepts one connection into a fresh
 *          library-owned connection and returns its handle.
 *
 * Calling sequence:
 *   srv = net_listen( port, backlog )               returns pointer
 *   rc  = net_accept( srv, client )                 returns fixed bin(31)
 *
 * The server and each accepted client are CONTROLLED records owned by this
 * library; callers hold POINTER handles only.
 */

/* Shared declarations are supplied by the master net.inc. */

/*--------------------------------------------------------------------------*/
/* Private server / client records (CONTROLLED, library-owned)               */
/*--------------------------------------------------------------------------*/
/* A server is a connection record whose fd is a listening socket. */
dcl 1 srv_rec controlled aligned,
   2 fd               fixed bin(31),
   2 is_connected     bit(1),
   2 read_timeout     fixed bin(31),
   2 write_timeout    fixed bin(31),
   2 peer_ip          char(16),
   2 peer_port        fixed bin(31),
   2 inbuf            char(NET_BUF_CAP),
   2 in_len           fixed bin(31);

/* The accepted client reuses the same shape. */
dcl 1 cli_rec controlled aligned,
   2 fd               fixed bin(31),
   2 is_connected     bit(1),
   2 read_timeout     fixed bin(31),
   2 write_timeout    fixed bin(31),
   2 peer_ip          char(16),
   2 peer_port        fixed bin(31),
   2 inbuf            char(NET_BUF_CAP),
   2 in_len           fixed bin(31);

/* A based view of the server record for the accept path. */
dcl 1 srv_view based(server_ptr),
   2 fd fixed bin(31);
dcl server_ptr pointer;

/* Declare the builtins used. */
dcl (addr, null) builtin;

/*--------------------------------------------------------------------------*/
/* Internal error helper                                                     */
/*--------------------------------------------------------------------------*/
srv_raise_err: procedure;
   dcl e fixed bin(31);
   e = s_errno();
   call s_clearerr();
   if e ^= 0 then signal condition(neterror) set oncode(e);
end srv_raise_err;

/*--------------------------------------------------------------------------*/
/* net_listen                                                                */
/*--------------------------------------------------------------------------*/
/* Bind to port and listen. Returns a server handle (POINTER). */
net_listen: procedure (port, backlog) returns (pointer);
   dcl port    fixed bin(31);
   dcl backlog fixed bin(31);
   dcl fd      fixed bin(31);
   dcl h       pointer;

   fd = s_socket(AF_INET, SOCK_STREAM, 0);
   if fd < 0 then do;
      call srv_raise_err();
      return null();
   end;
   if s_bind(fd, port) < 0 then do;
      call srv_raise_err();
      call s_close(fd);
      return null();
   end;
   if s_listen(fd, backlog) < 0 then do;
      call srv_raise_err();
      call s_close(fd);
      return null();
   end;

   allocate srv_rec;
   srv_rec.fd            = fd;
   srv_rec.is_connected  = '1'b;
   srv_rec.read_timeout  = 0;
   srv_rec.write_timeout = 0;
   srv_rec.in_len        = 0;
   h = addr(srv_rec);
   return h;
end net_listen;

/*--------------------------------------------------------------------------*/
/* net_accept                                                                */
/*--------------------------------------------------------------------------*/
/* Accept one connection on the server handle. Allocates a client connection
 * and returns its handle via `client`; NET_OK or NET_ERR. */
net_accept: procedure (server, client) returns (fixed bin(31));
   dcl server pointer;
   dcl client pointer;
   dcl cfd    fixed bin(31);

   server_ptr = server;
   cfd = s_accept(srv_view.fd);
   if cfd < 0 then do;
      call srv_raise_err();
      return NET_ERR;
   end;

   allocate cli_rec;
   cli_rec.fd            = cfd;
   cli_rec.is_connected  = '1'b;
   cli_rec.read_timeout  = 0;
   cli_rec.write_timeout = 0;
   cli_rec.in_len        = 0;
   client = addr(cli_rec);
   return NET_OK;
end net_accept;
