# AGENTS.md

Build/test/dev commands for the libnet PL/I socket library.

## Prerequisites

libnet depends on **libsock** (the socket compatibility layer) as an external
black-box dependency. Build and install libsock first:

```bash
# In the libsock project
cmake -S . -B build && cmake --build build
cmake --install build          # to CMAKE_INSTALL_PREFIX (default /usr/local)
```

## Build

```bash
cmake -S . -B build -DLIBSOCK_ROOT=<libsock install prefix> && cmake --build build
```

`LIBSOCK_ROOT` is auto-detected from `/usr/local` or `/opt/libsock`; set it
explicitly when libsock is installed elsewhere.

For a release build with optimization:
```bash
cmake -S . -B build -DLIBSOCK_ROOT=<prefix> -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

## Install

```bash
cmake --install build --prefix /usr/local
```

## Uninstall

```bash
cmake --build build --target libnet_uninstall
```

## Clean

```bash
cmake --build build --target clean
rm -rf build
```

## Build an Example Program (requires pli-llvm)

```bash
cmake --build build --target simple_usage
./build/simple_usage
```

## Trial-Compile All Examples (requires pli-llvm)

```bash
cmake --build build --target libnet-examples
```

## Environment

- `PLI_LLVM`: pli-llvm install prefix (sets the PL/I compiler location)
- `PLI_EXECUTABLE`: explicit path to the PL/I compiler
- `LIBNET_PLIFLAGS`: extra flags forwarded to the PL/I compiler
- `LIBNET_ENABLE_PLI`: ON (default) — build the PL/I module when the PL/I compiler is found
- `LIBNET_BUILD_EXAMPLES`: ON (default) — trial-compile examples when the PL/I compiler is found
- `LIBSOCK_ROOT`: libsock install prefix (auto-detected from `/usr/local` or `/opt/libsock`)
