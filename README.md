# PL/I Networking Library

A basic networking library for PL/I

## Files

| File | Purpose |
|------|---------|
| `type_defs.inc` | Named types (`conncb`, `port_t`, `buffer_t`, etc.) and the `conncb` structure |
| `net_bridge.inc` | C function external declarations (`socket`, `bind`, `listen`, ...) |
| `net.pli` / `.inc` | Object-oriented client socket methods (`net_dial`, `net_send`, `net_receive`, `net_close`, `net_shutdown`, `socket_errno`) |
| `net_server.pli` / `.inc` | Server socket methods (`net_listen`, `net_accept`, `server_error`) |
| `net_errors.inc` | `net_error` condition and `get_errno` entry |
| `net_bridge.c` | C bridge: `default_accept`, `bind_to_port`, `connect_to_host`, `get_errno_value`, `resolve_hostname` |

## Usage

```pli
 main: procedure options(main);
 %include net;

   declare
     request  char(1024) varying,
     response char(1024),
     bytes    size_t,
     conn     like conncb;

   /* assuming some server is running on localhost at 8080 */
   request =
       'GET / HTTP/1.1' || LINE_END ||
       'Host: 127.0.0.1:' || '8080' || LINE_END ||
       'Connection: close' || LINE_END || LINE_END;
   
   call netdial(conn, '127.0.0.1:8080', AF.INET);
   
   call netwriteall(conn, request);
   bytes = netreadall(conn, response);

   put skip list('Response ', substr(response, 1, bytes));

   call netclose(conn); 
 end;
```

## Build & Install

Requires Iron Spring PL/I (`plic`) and `gcc` (32-bit).

```sh
make
sudo make install              # or: make install PREFIX=$HOME/.local
```

## Run an Example

After `make install`, compile and link against the installed library:

```sh
cd examples

plic -C -dELF -ew readall_test.pli    \
  $(pkg-config --cflags net)              \   # -i/usr/local/include
  -o readall_test.o

gcc -m32 -no-pie -z muldefs               \   # 32-bit ELF target
  -Wl,--oformat=elf32-i386                \
  -o readall_test readall_test.o          \
  $(pkg-config --libs net)                   # -L/usr/local/lib -lnet -lprf
```

`pkg-config` handles the library paths only. The remaining flags are toolchain requirements (Iron Spring PL/I's 32-bit ELF target) and don't change between projects.

Or build directly in-tree with the existing script (no install needed):

```sh
cd examples
./build.sh readall_test.pli
```

## Layers

```
 .──────────────────────────────────────────.
 |  Application (uses %include socket)      |
 |──────────────────────────────────────────|
 |  net.pli / net_server.pli          |
 |  (OO methods, error signaling)           |
 |──────────────────────────────────────────|
 |  net_bridge.inc                         |
 |  (C function declarations)               |
 |──────────────────────────────────────────|
 |  net_bridge.c                           |
 |  (POSIX sockets / errno capture)         |
 `──────────────────────────────────────────'
```

## License

Apache 2.0
