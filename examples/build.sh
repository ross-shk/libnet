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

# auto use docker on hosts without plic (macos), fallback to local libnet.a if not installed
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
if ! command -v plic >/dev/null 2>&1; then
  DOCKER="docker run --rm --platform linux/386 -v $ROOT:/workspace -w /workspace/examples ghcr.io/ross-shk/pli"
  PLIC="$DOCKER plic"
  GCC="$DOCKER gcc"
else
  PLIC="plic"
  GCC="gcc"
fi

if pkg-config --exists net 2>/dev/null; then
  CFLAGS=$(pkg-config --cflags net)
  LIBS=$(pkg-config --libs net)
else
  echo "note: net.pc not found, using local libnet.a + include"
  if [ ! -f "$ROOT/libnet.a" ]; then
    echo "building $ROOT/libnet.a first..."
    make -C "$ROOT" all
  fi
  # use workspace paths inside docker, host paths otherwise
  if ! command -v plic >/dev/null 2>&1; then
    CFLAGS="-i/workspace/include"
    LIBS="/workspace/libnet.a -lprf /usr/lib/pli/alt/fhs.o /usr/lib/pli/alt/ghs.o"
  else
    CFLAGS="-i$ROOT/include"
    LIBS="$ROOT/libnet.a -lprf /usr/lib/pli/alt/fhs.o /usr/lib/pli/alt/ghs.o"
  fi
fi

echo "=== Compiling $SOURCE ==="
$PLIC -C -dELF "$SOURCE" $CFLAGS -o "${OUTPUT}.o"

echo "=== Linking $OUTPUT ==="
$GCC -m32 -no-pie -z muldefs \
  -o "$OUTPUT" "${OUTPUT}.o" \
  $LIBS

echo "=== Build complete: $OUTPUT ==="
