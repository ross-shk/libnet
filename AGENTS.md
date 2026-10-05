# AGENTS.md

Build/test/dev commands for the libnet PL/I socket library.

## Build

```bash
cmake -S . -B build && cmake --build build
```

For a release build with optimization:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
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

## Build an Example Program (requires plic)

```bash
cmake --build build --target simple_usage
./build/simple_usage
```

## Trial-Compile All Examples (requires plic)

```bash
cmake --build build --target libnet-examples
```

## Environment

- `PLI_LLVM`: pli-llvm install prefix (sets `plic` location)
- `PLIC_EXECUTABLE`: explicit path to the plic compiler
- `LIBNET_PLIFLAGS`: extra flags forwarded to plic
- `LIBNET_ENABLE_PLI`: ON (default) — build the PL/I module when plic is found
- `LIBNET_BUILD_EXAMPLES`: ON (default) — trial-compile examples when plic is found
