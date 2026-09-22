/* net_errors.inc — libnet conditions and error helpers.
 *
 * Module:  net_errors
 * Author:  libnet
 * Purpose: declare the networking conditions and translate oncode/errno into
 *          human-readable text.
 *
 * libnet reports ALL failures through conditions, never through return codes
 * (the idiomatic PL/I mechanism). Four conditions cover the failure space:
 *
 *   net_error    — a hard error;        oncode() holds the POSIX errno value.
 *   net_timeout  — read/write timeout, EAGAIN, or EINTR; oncode() = NET_TIMEOUT.
 *   net_eof      — the peer closed / end of stream;       oncode() = NET_EOF.
 *   net_overflow — the caller buffer filled mid-stream;   oncode() = NET_OVERFLOW.
 *
 * Callers establish handlers with ON and interrogate oncode() to recover the
 * detail. Returned values from libnet procedures are DATA only (byte counts,
 * delimiter positions, pointers) — a procedure that can fail raises one of the
 * conditions above instead of returning a status.
 *
 * Calling sequence:
 *   net_errtext( ) returns char(32)      — text for the current oncode()
 */

/* Shared types/constants are supplied by the master net.inc. */

dcl net_error    condition;
dcl net_timeout  condition;
dcl net_eof      condition;
dcl net_overflow condition;

/* Translate the current oncode() into a human-readable string.
 * Returns a fixed char(32). Implemented entirely in PL/I — no C involved. */
net_errtext: procedure returns (char(32));
   dcl e fixed bin(31);

   e = oncode();
   if e = 0        then return ('success');
   if e = NET_EOF       then return ('end of stream');
   if e = NET_TIMEOUT   then return ('timeout');
   if e = NET_OVERFLOW  then return ('buffer overflow');
   select;
      when (e = EINTR)         return ('interrupted');
      when (e = EBADF)         return ('bad file descriptor');
      when (e = EAGAIN)        return ('try again (EAGAIN)');
      when (e = EACCES)        return ('permission denied');
      when (e = EFAULT)        return ('bad address');
      when (e = EINVAL)        return ('invalid argument');
      when (e = EPIPE)         return ('broken pipe');
      when (e = ENOTCONN)      return ('not connected');
      when (e = ECONNRESET)    return ('connection reset');
      when (e = ETIMEDOUT)     return ('connection timed out');
      when (e = ECONNREFUSED)  return ('connection refused');
      when (e = EHOSTUNREACH)  return ('host unreachable');
      when (e = ENETUNREACH)   return ('network unreachable');
      otherwise                return ('errno ' || char(e));
   end; /* select */
end net_errtext;
