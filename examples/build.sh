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

# Path to Iron Spring PL/I alternate storage management modules.
# These replace PL/I's internal heap with C's malloc/free.
ALT_DIR="${ALT_DIR:-/usr/lib/pli-1.4.1/lib/alt}"

if [ $# -lt 1 ]; then
  echo "Usage: $0 <source.pli> [output_name]"
  echo ""
  echo "Builds a PL/I program that uses the net library."
  echo "  source.pli    - PL/I source file (required)"
  echo "  output_name   - Executable name (default: basename of source without .pli)"
  echo ""
  echo "Examples:"
  echo "  $0 use_net.pli"
  exit 1
fi

SOURCE="$1"
OUTPUT="${2:-$(basename "$SOURCE" .pli)}"

echo "=== Compiling $SOURCE ==="
plic -C -dELF "$SOURCE" $(pkg-config --cflags net) -o "${OUTPUT}.o"

echo "=== Linking $OUTPUT ==="
gcc -m32 -no-pie -z muldefs \
  -o "$OUTPUT" "${OUTPUT}.o" \
  $(pkg-config --libs net) # ${ALT_DIR}/fhs.o ${ALT_DIR}/ghs.o -lnet -lprf

echo "=== Build complete: $OUTPUT ==="
