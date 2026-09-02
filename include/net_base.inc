/* Copyright 2026 Ross Shkurat - Apache-2.0, see LICENSE */

 %include type_defs;

 dcl net_open entry (
   like conncb, 
   fixed bin(31), 
   fixed bin(31), 
   fixed bin(31)
 );

 dcl net_connect entry (
   like conncb,
   char(*) varying,
   port_t
 );
 
 dcl net_dial entry (
   like conncb,
   char(*) varying,
   fixed bin(31)
 );

 dcl net_send entry (
   like conncb,
   char(*) varying,
   flags_t
 ) returns(size_t);

  dcl net_write entry (
    like conncb,
    char(*) varying
  ) returns(size_t);

 dcl net_write_all entry (
   like conncb,
   char(*) varying
 ) returns(size_t);
 
 dcl net_recv entry (
   like conncb,
   char(*),  
   flags_t
 ) returns(size_t);

dcl net_read entry (
    like conncb,
    char(*)
  ) returns(size_t);

 dcl net_read_all entry (
   like conncb,
   char(*)
 ) returns(size_t);

 dcl net_close entry (
   like conncb
 ) returns(fixed bin(31));

 dcl net_shutdown entry (
   like conncb,
   shut_how_t  /* 0=read, 1=write, 2=both */
 ) returns(fixed bin(31));

 dcl net_resolve entry (
   like conncb,
   char(*) varying
 );

 dcl net_poll entry (
   like conncb,
   fixed bin(31),  /* timeout ms, -1 infinite, 0 immediate */
   fixed bin(31)   /* events mask POLL.IN etc */
 ) returns(fixed bin(31));
  