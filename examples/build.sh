#!/bin/bash
# Copyright 2026 Ross Shkurat - Apache-2.0, see LICENSE

set -e

MODE="build"
if [ "$1" = "run" ]; then
  MODE="run"
  shift
fi

if [ $# -lt 1 ]; then
  echo "Usage: $0 [run] <source.pli> [output_name]"
  echo ""
  echo "Builds a PL/I program that uses the net library."
  echo "  source.pli    - PL/I source file (required)"
  echo "  output_name   - Executable name (default: basename of source without .pli)"
  echo "  run           - build and run the executable (via docker on macos)"
  echo ""
  echo "Examples:"
  echo "  $0 use_socket.pli"
  echo "  $0 run readme_usage.pli"
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

if [ "$MODE" = "run" ]; then
  echo "=== Running $OUTPUT ==="
  if ! command -v plic >/dev/null 2>&1; then
    docker run --rm --platform linux/386 -v "$ROOT:/workspace" -w /workspace/examples ghcr.io/ross-shk/pli "./$OUTPUT"
  else
    "./$OUTPUT"
  fi
fi
