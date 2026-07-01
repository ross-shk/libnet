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
