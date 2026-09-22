/* net.inc — libnet master include.
 *
 * Module:  net
 * Purpose: supply the whole libnet surface to a program. A program that wants
 *          networking does `%include net;` and gets the shared types, the
 *          by-value C bridge, the errno constants, the error conditions, and
 *          the client + server APIs.
 *
 * Include order matters: the shared declarations must precede the procedures
 * that reference them.
 */

%include errno;
%include type_defs;
%include c_bridge;
%include net_errors;
%include net_base;
%include net_server;
