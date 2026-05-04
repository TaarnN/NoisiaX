# NoisiaX

Starter scaffold for a modern C++ library using CMake.

## Layout

- `include/` public headers
- `src/` library implementation
- `tests/` basic test executable
- `cmake/` package configuration template

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

## Install

```sh
cmake -S . -B build
cmake --build build
cmake --install build --prefix ./install
```
