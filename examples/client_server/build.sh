#!/bin/bash

set -e

# Path to Iron Spring PL/I alternate storage management modules.
# These replace PL/I's internal heap with C's malloc/free.
ALT_DIR="${ALT_DIR:-/usr/lib/pli-1.4.1/lib/alt}"

INC="-i../../include"

echo "=== Building C bridge ==="
gcc -m32 -c ../../source/net_bridge.c -o net_bridge.o

echo "=== Compiling library packages ==="
plic -C -dELF -ew -O ../../source/net.pli        $INC -o net.o
plic -C -dELF -ew -O ../../source/net_server.pli $INC -o net_server.o

echo "=== Compiling programs ==="
plic -C -dELF -ew -O server_app.pli  $INC -o server_app.o
plic -C -dELF -ew -O client_app.pli  $INC -o client_app.o

LIBS="-lprf"
LD_FLAGS="-m32 -no-pie -z muldefs -Wl,-M -Wl,--oformat=elf32-i386 -static-libgcc"
OBJS="net_bridge.o net.o net_server.o"

echo "=== Linking server_app ==="
gcc $LD_FLAGS -o server_app server_app.o $OBJS $LIBS > server_app.map

echo "=== Linking client_app ==="
gcc $LD_FLAGS -o client_app client_app.o $OBJS $LIBS > client_app.map

echo ""
echo "Build complete. Run:"
echo "  ./run.sh"
