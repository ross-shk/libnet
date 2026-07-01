 
 /* Copyright 2026 Ross Shkurat
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

 %include type_defs;

 dcl netnew entry (
   like conncb, 
   fixed bin(31), 
   fixed bin(31), 
   fixed bin(31)
 );

 dcl netconnect entry (
   like conncb,
   char(16) varying,
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
  