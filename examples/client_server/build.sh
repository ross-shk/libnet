#!/bin/bash
# Copyright 2026 Ross Shkurat - Apache-2.0, see LICENSE

set -e

echo "=== Compiling programs ==="
plic -C -dELF -ew server_app.pli \
  $(pkg-config --cflags net) \
  -o server_app.o

plic -C -dELF -ew client_app.pli \
  $(pkg-config --cflags net) \
  -o client_app.o

LIBS="$(pkg-config --libs net)"
LD_FLAGS="-m32 -no-pie -z muldefs -Wl,--oformat=elf32-i386"

echo "=== Linking server_app ==="
gcc $LD_FLAGS -o server_app server_app.o $LIBS > server_app.map

echo "=== Linking client_app ==="
gcc $LD_FLAGS -o client_app client_app.o $LIBS > client_app.map

echo ""
echo "Build complete. Run:"
echo "  ./run.sh"
