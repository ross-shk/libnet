/* Copyright 2026 Ross Shkurat - Apache-2.0, see LICENSE */

 dcl net_listen entry (
   like conncb,
   port_t,
   backlog_t
 );

 dcl net_accept entry (
   like conncb,
   like conncb
 );