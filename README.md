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

## Using it

```cpp
#include <calculate-core/calculate-core.hpp>

calculate_core::Options options;
options.type = calculate_core::NumberType::Double;

const calculate_core::Result r = calculate_core::evaluate("0.1 + 0.2", options);
// r.value.digits      == "3000000000000000444089209850062616169452667236328125", exponent10 == -1
// r.trustedDigits     == 15          (digits guaranteed by the error bound)
// r.bound             == "4.4e-17"   (input 1.7e-17 + rounding 2.8e-17 + library 0)
// r.measured          == "4.4e-17"   (against a 2017-bit reference)
// r.conditionNumber   == "1e+0"
```

`numberTypes()` describes the seven types as built on this platform, `functions()` lists the
language, and `Session` keeps history, `Ans` and memory.

With CMake:

```cmake
include(FetchContent)
FetchContent_Declare(calculate-core GIT_REPOSITORY https://github.com/bentxr/calculate-core.git GIT_TAG <tag>)
FetchContent_MakeAvailable(calculate-core)
target_link_libraries(your-app PRIVATE calculate-core)
```

## Status

Early development.

## License

To be decided.
