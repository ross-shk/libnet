 /* errno.inc - POSIX errno constants as %replace, after the
  * Iron Spring lib/include/errno.inc idiom. Named
  * constants keep libnet's error handling readable and
  * match the values returned by the C bridge's cached errno.
  *
  * Only the subset relevant to sockets is defined here; add more as
   * needed.
  */

 %replace EPERM      by   1;  /* Operation not permitted */
 %replace ENOENT     by   2;  /* No such file or directory */
 %replace EINTR      by   4;  /* Interrupted system call */
 %replace EBADF      by   9;  /* Bad file descriptor */
 %replace ECHILD     by  10;  /* No child processes */
 %replace EAGAIN     by  11;  /* Try again (also EWOULDBLOCK) */
 %replace ENOMEM     by  12;  /* Out of memory */
 %replace EACCES     by  13;  /* Permission denied */
 %replace EFAULT     by  14;  /* Bad address */
 %replace EINVAL     by  22;  /* Invalid argument */
 %replace EPIPE      by  32;  /* Broken pipe */
 %replace ENOTSOCK   by  88;  /* Socket operation on non-socket */
 %replace EMSGSIZE   by  90;  /* Message too long */
 %replace EADDRINUSE by  98;  /* Address already in use */
 %replace EADDRNOTAVAIL by 99;/* Cannot assign requested address */
 %replace ENETUNREACH by 101; /* Network is unreachable */
 %replace ECONNRESET by 104;  /* Connection reset by peer */
 %replace ENOBUFS    by 105;  /* No buffer space available */
 %replace ENOTCONN   by 107;  /* Transport endpoint is not connected */
 %replace ETIMEDOUT  by 110;  /* Connection timed out */
 %replace ECONNREFUSED by 111;/* Connection refused */
 %replace EHOSTDOWN  by 112;  /* Host is down */
 %replace EHOSTUNREACH by 113;/* No route to host */
 %replace EINPROGRESS by 115; /* Operation now in progress */
