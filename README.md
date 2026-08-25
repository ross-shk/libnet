# PL/I Networking Library

A basic networking library for PL/I

## Usage

```pli
main: procedure options(main);
 %include net;

   declare
     request  char(256) varying,
     response char(2048),
     host     char(256) varying init('example.com:80'),
     bytes    size_t,
     conn     like conncb;
  
   on condition(neterror) begin;  /* handle low-level network errors */
     display('Networking error, code = ' || oncode());
     goto done;
   end;

   request =
       'GET / HTTP/1.1'    || LINE_END ||
       'Host: ' || host    || LINE_END ||
       'Connection: close' || LINE_END || LINE_END;
   
   call netdial(conn, host, AF.INET);  /* host is auto-resolved */

   call netwriteall(conn, request); 
   bytes = netreadall(conn, response);

   /* only reached unless there's a network error */
   display('Response ' || substr(response, 1, bytes));
 
 done:
   call netclose(conn); 
 end;
```

## Build & Install

Requires Iron Spring PL/I (`plic`) and `gcc` (`-m32`) - or Docker.

On macOS without `plic` the Makefile auto-uses `ghcr.io/ross-shk/pli:latest`
(`linux/386`) for `plic`/`gcc -m32`/`ar`, so no VM copy is needed:

```sh
make                # builds libnet.a via Docker on macOS, native on Linux
make test
make test-client-server
```

Override with `USE_DOCKER=0` (force native) or `USE_DOCKER=1` (force Docker):

```sh
make USE_DOCKER=1
make test USE_DOCKER=1
```

Install:

```sh
# Linux VM / natively-installed plic
sudo make install              # or: make install PREFIX=$HOME/.local

# Docker host (macOS) - host-visible prefix, not system /usr/local
make install PREFIX=$PWD/local          # -> ./local/include/net.inc
make install PREFIX=$HOME/.local
make install DESTDIR=$PWD/out PREFIX=/usr/local  # staging
```

`make install` to default `PREFIX=/usr/local` without `DESTDIR` is blocked
when `USE_DOCKER=1` (would write linux/386 `libnet.a` to macOS `/usr/local`).

## Run an Example

After `make install`, compile and link against the installed library:

```sh
cd examples

plic -C -dELF readall_test.pli    \
  $(pkg-config --cflags net)      \  
  -o readall_test.o

gcc -m32 -no-pie -z muldefs        \   
  -o readall_test readall_test.o   \
  $(pkg-config --libs net)                 
```

**NOTE:** `pkg-config` handles the library paths only. The remaining flags are toolchain requirements (Iron Spring PL/I's 32-bit ELF target) and don't change between projects.

Without installing, link against the local build:

```sh
make                       # provides libnet.a + dist/net.inc
# then compile manually (needs plic/gcc -m32 or Docker):
plic -C -dELF -I../include myprog.pli -o myprog.o
gcc -m32 -no-pie -z muldefs -o myprog myprog.o libnet.a -lprf /usr/lib/pli/alt/fhs.o /usr/lib/pli/alt/ghs.o
```

Or build with a script:

```sh
cd examples
./build.sh readall_test.pli   # uses pkg-config after install, or local libnet.a
```

On macOS without `plic`, the `make` targets above are auto-Dockerized. For manual
`plic`/`gcc` invocations use Docker:

```sh
docker run --rm --platform linux/386 -v $PWD:/workspace -w /workspace \
  ghcr.io/ross-shk/pli plic -C -dELF myprog.pli -I../include -o myprog.o
docker run --rm --platform linux/386 -v $PWD:/workspace -w /workspace \
  ghcr.io/ross-shk/pli gcc -m32 -no-pie -z muldefs -o myprog myprog.o libnet.a -lprf /usr/lib/pli/alt/fhs.o /usr/lib/pli/alt/ghs.o
```

## License

Apache 2.0
