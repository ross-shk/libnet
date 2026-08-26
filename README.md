# PL/I Networking Library

A basic networking library for PL/I

## Build & Install

Requires Iron Spring PL/I and `gcc`, or similar.

The Makefile auto-uses `ghcr.io/ross-shk/pli:latest` (also available from the GitHub [repo](https://github.com/ross-shk/pli-docker)) if no Iron Spring PL/I is available:

```sh
make                
make test
```

Install - only on systems with PL/I installed natively:

```sh
sudo make install             
```

Or just use `libnet.a` available in the project root after build directly.

## Usage

See `examples/readme_usage.pli`:

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

## Run an Example

You can build and run code in `./examples` using the provided `build.sh` which either uses the Docker image or natively installed PL/I compiler if available:

```sh
cd examples

./build.sh run readme_usage.pli
```

On a system with the Iron Spring PL/I or a similar compiler installed directly, you can compile and link using the following commands (make sure to build and install this library first, see [Build & Install](#build--install) above):

```sh
cd examples

plic -C -dELF readme_usage.pli    \
  $(pkg-config --cflags net)      \  
  -o readme_usage.o

gcc -m32 -no-pie -z muldefs        \   
  -o readme_usage readme_usage.o   \
  $(pkg-config --libs net)                 
```

**NOTE:** `pkg-config` handles the library paths only. The remaining flags are toolchain requirements (Iron Spring PL/I's 32-bit ELF target) and don't change between projects.

Without installing, compile and link against the local build:

```sh
# from examples/

plic -C -dELF -i../include readme_usage.pli -o readme_usage.o

gcc -m32 -no-pie -z muldefs -o readme_usage readme_usage.o ../libnet.a \
  -lprf /usr/lib/pli/alt/fhs.o /usr/lib/pli/alt/ghs.o
```

then run:

```sh
./readme_usage
```

Linking against `fhs.o` and `ghs.o` (shipped as part of Iron Spring PL/I) is required for interoperability with C networking functions.

If using Docker, mount the project root so `../include` and `../libnet.a` are visible - same commands via helper (works in `bash`/`zsh`):

```sh
# from examples/

dockerize() { docker run --rm --platform linux/386 -v $PWD/..:/workspace -w /workspace/examples ghcr.io/ross-shk/pli "$@"; }

dockerize plic -C -dELF -i../include readme_usage.pli -o readme_usage.o

dockerize gcc -m32 -no-pie -z muldefs -o readme_usage readme_usage.o ../libnet.a -lprf /usr/lib/pli/alt/fhs.o /usr/lib/pli/alt/ghs.o

dockerize ./readme_usage
```

## License

Apache 2.0
