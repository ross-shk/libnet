# PLISocket

A socket library for PL/I with a C bridge, object-oriented wrappers, and PL/I condition-based error handling.

## Files

| File | Purpose |
|------|---------|
| `type_defs.inc` | Named types (`net_fd_t`, `port_t`, `buffer_t`, etc.) and the `net_t` structure |
| `net_bridge.inc` | C function external declarations (`socket`, `bind`, `listen`, ...) |
| `net.pli` / `.inc` | Object-oriented client socket methods (`net_connect`, `net_send`, `net_receive`, `net_close`, `net_shutdown`, `socket_errno`) |
| `server_net.pli` / `.inc` | Server socket methods (`create_server`, `server_accept`, `server_error`) |
| `net_errors.inc` | `net_error` condition and `get_errno` entry |
| `net_bridge.c` | C bridge: `default_accept`, `bind_to_port`, `connect_to_host`, `get_errno_value`, `resolve_hostname` |

## Usage

```pli
 main: procedure options(main);
 %include net;
 
   declare
     request  char(2048) varying,
     response char(2048),
     bytes    size_t,
     sock     like net_t;

   request =
       'GET / HTTP/1.1' || LINE_END ||
       'Host: 127.0.0.1:' || '8080' || LINE_END ||
       'Connection: close' || LINE_END || LINE_END;
   
   call net_new(sock, AF.INET, TYPE.STREAM, 0); 
   call net_connect(sock, '127.0.0.1', 8080);
   bytes = net_send(sock, request, 0);
   bytes = net_receive(sock, response, 0);

   put skip list('Response ', substr(response, 1, bytes));

   call net_close(sock); 
 end;
```

## Build (32-bit ELF)

```sh
cd examples
./build.sh use_net.pli
```

Requires Iron Spring PL/I (`plic`), `gcc` with `-m32`, and `libprf` (32-bit).

## Layers

```
 .──────────────────────────────────────────.
 |  Application (uses %include socket)      |
 |──────────────────────────────────────────|
 |  net.pli / server_net.pli          |
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
