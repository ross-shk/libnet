/* Copyright 2026 Ross Shkurat - Apache-2.0, see LICENSE */

 /* package-level helper: apply read/write timeout from conncb */
 apply_timeout: proc(conn, is_read);
   dcl conn like conncb;
   dcl is_read fixed bin(31);
   dcl rc fixed bin(31);
   if is_read then do;
     if conn.read_timeout ¬= 0 then
       rc = c_set_timeout(conn.fd, 1, conn.read_timeout);
     else rc = c_set_timeout(conn.fd, 1, 0);
   end;
   else do;
     if conn.write_timeout ¬= 0 then
       rc = c_set_timeout(conn.fd, 0, conn.write_timeout);
     else rc = c_set_timeout(conn.fd, 0, 0);
   end;
 end apply_timeout;
