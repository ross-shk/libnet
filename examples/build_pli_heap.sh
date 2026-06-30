#!/bin/bash
# Copyright 2026 Ross Shkurat
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -e

if [ $# -lt 1 ]; then
  echo "Usage: $0 <source.pli> [output_name]"
  echo ""
  echo "Builds a PL/I program that uses the socket library."
  echo "  source.pli    - PL/I source file (required)"
  echo "  output_name   - Executable name (default: basename of source without .pli)"
  echo ""
  echo "Examples:"
  echo "  $0 use_net.pli"
  echo "  $0 client_server/server_app.pli server_app"
  echo "  $0 client_server/client_app.pli client_app"
  exit 1
fi

SOURCE="$1"
OUTPUT="${2:-$(basename "$SOURCE" .pli)}"
INCDIR="-i../include"

echo "=== Building C bridge ==="
gcc -m32 -c ../source/net_bridge.c -o net_bridge.o

echo "=== Compiling library packages ==="
plic -C -dELF -ew -O ../source/net.pli      $INCDIR -o net.o
# plic -C -dELF -ew -O ../source/net_server.pli $INCDIR -o net_server.o

echo "=== Compiling $SOURCE ==="
plic -C -dELF -ew -O "$SOURCE" $INCDIR -o "${OUTPUT}.o"

echo "=== Linking $OUTPUT ==="
gcc -m32 -no-pie -z muldefs -Wl,-M -Wl,--oformat=elf32-i386 \
  -static-libgcc -nostartfiles -e main \
  -o "$OUTPUT" "${OUTPUT}.o" net_bridge.o net.o \
  -lprf > "${OUTPUT}.map"

echo "=== Build complete: $OUTPUT ==="
