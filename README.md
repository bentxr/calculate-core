# calculate-core

The engine of **calculate**, a scientific calculator in which every result is shown
with its error and with the digits you can trust.

- C++17, one static library, one public header: `<calculate-core/calculate-core.hpp>`
- Seven number types: `float`, `double`, `long double`, exact rationals, and IEEE 754
  binary128 / binary256 / binary512 in software
- Builds natively and for WebAssembly (Emscripten)

## Build and test

Requires CMake ≥ 3.25, Ninja and a C++17 compiler. Dependencies (Boost.Multiprecision,
GoogleTest) are fetched and pinned automatically.

```bash
cmake --preset native && cmake --build --preset native && ctest --preset native
```

WebAssembly (tests run under Node.js):

```bash
source /path/to/emsdk/emsdk_env.sh
cmake --preset wasm && cmake --build --preset wasm && ctest --preset wasm
```

## Status

Early development.

## License

To be decided.
