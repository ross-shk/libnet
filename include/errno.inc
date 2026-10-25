 /* errno.inc - POSIX errno constants as %replace. Named constants keep
 * libnet's error handling readable and match the values returned
 * by the C bridge's cached errno.
 *
 * Values are macOS (arm64); Linux values are noted for portability.
 * Only the subset relevant to sockets is defined here; add more
 * as needed.
 */

 %replace EPERM        by   1;  /* Operation not permitted        */
 %replace ENOENT       by   2;  /* No such file or directory      */
 %replace EINTR        by   4;  /* Interrupted system call        */
 %replace EBADF        by   9;  /* Bad file descriptor            */
 %replace ECHILD       by  10;  /* No child processes             */
 %replace EAGAIN       by  35;  /* Try again (Linux: 11)          */
 %replace ENOMEM       by  12;  /* Out of memory                  */
 %replace EACCES       by  13;  /* Permission denied              */
 %replace EFAULT       by  14;  /* Bad address                    */
 %replace EINVAL       by  22;  /* Invalid argument               */
 %replace EPIPE        by  32;  /* Broken pipe                    */
 %replace EMFILE       by  24;  /* Too many open files            */
 %replace ENOTSOCK     by  38;  /* Socket operation on non-socket */
 %replace EMSGSIZE     by  40;  /* Message too long               */
 %replace EADDRINUSE   by  48;  /* Address already in use         */
 %replace EADDRNOTAVAIL by  49; /* Cannot assign requested address */
 %replace ENETUNREACH  by  51;  /* Network is unreachable         */
 %replace ECONNRESET   by  54;  /* Connection reset by peer       */
 %replace ENOBUFS      by  55;  /* No buffer space available      */
 %replace ENOTCONN     by  57;  /* Transport endpoint not conn.   */
 %replace ETIMEDOUT    by  60;  /* Connection timed out           */
 %replace ECONNREFUSED by  61;  /* Connection refused             */
 %replace EHOSTDOWN    by  64;  /* Host is down                   */
 %replace EHOSTUNREACH by  65;  /* No route to host               */
 %replace EINPROGRESS  by  36;  /* Operation now in progress      */
