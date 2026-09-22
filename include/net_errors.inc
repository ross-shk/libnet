/* net_errors.inc — libnet conditions and error helpers.
 *
 * Module:  net_errors
 * Purpose: declare the networking conditions and translate errno/oncode
 *          into human-readable text.
 *
 * Two conditions model networking errors:
 *   neterror   — a hard error; oncode() holds the errno value.
 *   nettimeout — a read/write timeout or EAGAIN/EINTR; oncode() = NET_TIMEOUT.
 *
 * Callers establish handlers and may interrogate oncode(). The library raises
 * these via SIGNAL ... SET ONCODE.
 *
 * Calling sequence:
 *   net_errtext( ) returns char(32)
 */

/* The shared errno constants and types are supplied by the master net.inc
 * (see the %include order there); this file is included once. */

dcl neterror   condition;
dcl nettimeout condition;

/* Translate the current oncode()/errno into a human-readable string.
 * Returns a fixed char(32). Implemented entirely in PL/I — no C involved. */
net_errtext: procedure returns (char(32));
   dcl e fixed bin(31);

   e = oncode();
   if e = NET_TIMEOUT then return ('timeout');
   if e = 0 then return ('success');
   select;
      when (e = EINTR)      return ('interrupted');
      when (e = EBADF)      return ('bad file descriptor');
      when (e = EAGAIN)     return ('try again (EAGAIN)');
      when (e = EACCES)     return ('permission denied');
      when (e = EFAULT)     return ('bad address');
      when (e = EINVAL)     return ('invalid argument');
      when (e = EPIPE)      return ('broken pipe');
      when (e = ENOTCONN)   return ('not connected');
      when (e = ECONNRESET) return ('connection reset');
      when (e = ETIMEDOUT)  return ('connection timed out');
      when (e = ECONNREFUSED) return ('connection refused');
      when (e = EHOSTUNREACH) return ('host unreachable');
      when (e = ENETUNREACH)  return ('network unreachable');
      otherwise              return ('errno ' || char(e));
   end; /* select */
end net_errtext;
