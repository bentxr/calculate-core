<div align="center">

# calculate-core

**A compiled C++ library of specialised math functions.**

Split into categories at the source, shipped as one library.

![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-3.16%2B-064F8C?logo=cmake&logoColor=white)
![Tests](https://img.shields.io/badge/tests-GoogleTest-4285F4?logo=google&logoColor=white)
![Status](https://img.shields.io/badge/status-early%20development-orange)
![License](https://img.shields.io/badge/license-TBD-lightgrey)

</div>

---

## What is this?

`calculate-core` is the computational engine behind the `calculate` project — a standalone C++ library where math 
functions are organised into categories but compiled down to **a single shared library**.


## Project layout

```
calculate-core/
├── CMakeLists.txt                  # build recipe (library + tests)
├── include/
│   └── calculate-core/
│       ├── calculate-core.hpp      # umbrella header — include this
│       ├── export.hpp              # CALCULATE_CORE_API visibility macros
│       └── algebra.hpp             # public API, per category
├── src/
│   └── algebra.cpp                 # implementations, per category
├── tests/
│   ├── CMakeLists.txt              # fetches GoogleTest, registers tests
│   └── test_algebra.cpp
└── examples/                       # sample consumers (coming soon)
```

## Building

Requires a C++17 compiler and CMake ≥ 3.16.

```bash
cmake -S . -B build          # configure (fetches GoogleTest on first run)
cmake --build build          # → build/libcalculate-core.so.0.1.0 (+ symlinks)
```

Build just the library, skipping tests:

```bash
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build
```

## Using it in another project

You need **two things**: the headers (to compile against) and the `.so` (to link
and run against).

### With CMake (recommended)

Drop the repo in and pull the target into your build:

```cmake
add_subdirectory(calculate-core)

target_link_libraries(your_app PRIVATE calculate-core)
# include path + rpath are handled for you.
```

```cpp
#include <calculate-core/calculate-core.hpp>

int main() {
    // e.g. double d = calculate_core::determinant2x2(1, 2, 3, 4);
}
```

### By hand

Copy `include/` and the `.so` set (keep the symlinks), then:

```bash
g++ main.cpp -I path/to/include -L path/to/lib -lcalculate-core -o app
```

At runtime the loader must find the library — install it to a system path, or
point `LD_LIBRARY_PATH` at its directory.

## Testing

Tests use [GoogleTest](https://github.com/google/googletest), fetched
automatically at configure time and wired into CTest.

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Add a `test_<category>.cpp` per source category, list it in
`tests/CMakeLists.txt`, and register cases with `TEST(...)` — they're
auto-discovered. For floating-point results, assert with a tolerance
(`EXPECT_NEAR`), never exact equality.

## Versioning & ABI

The library carries a full version and a separate soname:

```
libcalculate-core.so         → .so.0       (soname, embedded in consumers)
libcalculate-core.so.0       → .so.0.1.0   (the real file)
```

- **Compatible change** (bug fix, faster internals, *new* exported function):
  bump `VERSION`, leave `SOVERSION`. Consumers swap the file and keep working.
- **Breaking change** (changed/removed signature, altered layout): bump
  `SOVERSION`. Old consumers keep resolving the previous soname until rebuilt.

## License

_To be decided_ — add a `LICENSE` file and update this section (MIT and
Apache-2.0 are common choices for a library like this).
