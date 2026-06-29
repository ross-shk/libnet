# PLISocket

A socket library for PL/I with a C bridge, object-oriented wrappers, and PL/I condition-based error handling.

## Files

| File | Purpose |
|------|---------|
| `type_defs.inc` | Named types (`socket_fd_t`, `port_t`, `buffer_t`, etc.) and the `socket_t` structure |
| `socket_bridge.inc` | C function external declarations (`socket`, `bind`, `listen`, ...) |
| `socket.pli` / `.inc` | Object-oriented client socket methods (`socket_connect`, `socket_send`, `socket_receive`, `close_socket`, `shutdown_socket`, `socket_errno`) |
| `server_socket.pli` / `.inc` | Server socket methods (`create_server`, `server_accept`, `server_error`) |
| `socket_errors.inc` | `socket_error` condition and `get_errno` entry |
| `socket_bridge.c` | C bridge: `default_accept`, `bind_to_port`, `connect_to_host`, `get_errno_value`, `resolve_hostname` |

## Usage

```pli
 main: procedure options(main);
 %include socket;
 
   declare
     request  char(2048) varying,
     response char(2048),
     bytes    size_t,
     sock     like socket_t;

   request =
       'GET / HTTP/1.1' || LINE_END ||
       'Host: 127.0.0.1:' || '8080' || LINE_END ||
       'Connection: close' || LINE_END || LINE_END;
   
   call new_socket(sock, AF.INET, TYPE.STREAM, 0); 
   call socket_connect(sock, '127.0.0.1', 8080);
   bytes = socket_send(sock, request, 0);
   bytes = socket_receive(sock, response, 0);

   put skip list('Response ', substr(response, 1, bytes));

   call close_socket(sock); 
 end;
```

## Build (32-bit ELF)

```sh
cd examples
./build.sh use_socket.pli
```

Requires Iron Spring PL/I (`plic`), `gcc` with `-m32`, and `libprf` (32-bit).

## Layers

```
 .──────────────────────────────────────────.
 |  Application (uses %include socket)      |
 |──────────────────────────────────────────|
 |  socket.pli / server_socket.pli          |
 |  (OO methods, error signaling)           |
 |──────────────────────────────────────────|
 |  socket_bridge.inc                         |
 |  (C function declarations)               |
 |──────────────────────────────────────────|
 |  socket_bridge.c                           |
 |  (POSIX sockets / errno capture)         |
 `──────────────────────────────────────────'
```

## License

Apache 2.0
