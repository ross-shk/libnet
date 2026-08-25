/* Copyright 2026 Ross Shkurat - Apache-2.0, see LICENSE */

 %include type_defs;

 dcl netnew entry (
   like conncb, 
   fixed bin(31), 
   fixed bin(31), 
   fixed bin(31)
 );

 dcl netconnect entry (
   like conncb,
   char(*) varying,
   port_t
 );
 
 dcl netdial entry (
   like conncb,
   char(*) varying,
   fixed bin(31)
 );

 dcl netsend entry (
   like conncb,
   char(*) varying,
   flags_t
 ) returns(size_t);

  dcl netwrite entry (
    like conncb,
    char(*) varying
  ) returns(size_t);

 dcl netwriteall entry (
   like conncb,
   char(*) varying
 ) returns(size_t);
 
 dcl netreceive entry (
   like conncb,
   char(*),  
   flags_t
 ) returns(size_t);

dcl netread entry (
    like conncb,
    char(*)
  ) returns(size_t);

 dcl netreadall entry (
   like conncb,
   char(*)
 ) returns(size_t);

 dcl netclose entry (
   like conncb
 ) returns(fixed bin(31));

 dcl netshutdown entry (
   like conncb,
   shut_how_t  /* 0=read, 1=write, 2=both */
 ) returns(fixed bin(31));

 dcl netresolve entry (
   like conncb,
   char(*) varying
 );
  