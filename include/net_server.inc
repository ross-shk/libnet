/* Copyright 2026 Ross Shkurat - Apache-2.0, see LICENSE */

 dcl netlisten entry (
   like conncb,
   port_t,
   backlog_t
 );

 dcl netaccept entry (
   like conncb,
   like conncb
 );