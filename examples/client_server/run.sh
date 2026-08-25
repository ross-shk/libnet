#!/bin/bash
# Copyright 2026 Ross Shkurat - Apache-2.0, see LICENSE
set -e
cd "$(dirname "$0")"

# build if missing (uses ../build.sh which auto-docks on macos)
if [ ! -x ./server_app ] || [ ! -x ./client_app ]; then
  echo "binaries missing, building..."
  (cd .. && ./build.sh client_server/server_app.pli)
  (cd .. && ./build.sh client_server/client_app.pli)
fi

# run sharing localhost — single container on docker hosts
if ! command -v plic >/dev/null 2>&1; then
  echo "=== Starting server (docker) ==="
  docker run --rm --platform linux/386 -v "$(cd ../.. && pwd):/workspace" -w /workspace/examples ghcr.io/ross-shk/pli bash -c '
    cd client_server
    echo "=== Starting server ==="
    ./server_app &
    pid=$!
    sleep 1
    echo "=== Starting client ==="
    ./client_app
    rc=$?
    echo "=== Done ==="
    kill $pid 2>/dev/null || true
    wait $pid 2>/dev/null || true
    exit $rc
  '
else
  echo "=== Starting server ==="
  ./server_app &
  pid=$!
  sleep 1
  echo "=== Starting client ==="
  ./client_app
  rc=$?
  echo ""
  echo "=== Done ==="
  kill $pid 2>/dev/null || true
  wait $pid 2>/dev/null || true
  exit $rc
fi
