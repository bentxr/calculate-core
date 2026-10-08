<div align="center">

# calculate-core

**A scientific calculator that shows every result with its error and with the digits you can trust. <br> The computational engine behind the [**calculate**](https://github.com/bentxr/calculate) project.**

[![ci](https://github.com/bentxr/calculate-core/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/bentxr/calculate-core/actions/workflows/ci.yml)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![CMake](https://img.shields.io/badge/CMake-3.25%2B-064F8C?logo=cmake&logoColor=white)
![Tests](https://img.shields.io/badge/tests-GoogleTest-4285F4?logo=google&logoColor=white)
![Status](https://img.shields.io/badge/status-early%20development-orange)
![License](https://img.shields.io/badge/license-TBD-lightgrey)

</div>

```text
$ calc "0.1 + 0.2" "sin(1e10)"
0.1 + 0.2
= 0.300000000000000|0444089209850062616169452667236328125
  ± 4.4e-17  input 1.7e-17 · rounding 2.8e-17 · library 0
  measured 4.4e-17 · κ 1e+0 · 15 trusted digits
sin(1e10)
= -0.487506025087510|674875801441885414533317089080810546875
  ± 1.1e-16  input 0 · rounding 0 · library 1.1e-16
  measured 1.7e-17 · κ 1.8e+10 · 15 trusted digits
```

- **Honest results.** Every answer comes with a guaranteed error bound, the error actually
  measured, and the digits that can be trusted.
- **Seven number types.** `float`, `double`, `long double`, exact rationals, and IEEE 754
  binary128 / binary256 / binary512 in software.
- **The same bits everywhere.** Its own elementary functions, built only from IEEE-guaranteed
  operations, give identical results natively and in WebAssembly.
- **Small surface.** C++17, one static library, one public header, and a command-line tool, `calc`.

## Reading a result

```text
= 0.300000000000000|0444089209850062616169452667236328125
  └─── trusted ───┘ └───── stored, not guaranteed ──────┘
```

The value is the exact number stored, digit for digit. The `|` marks where the trusted digits end.

| Part | Meaning |
|---|---|
| **±** | A guaranteed bound on the error, split by where it comes from: the **input** (turning the decimals you typed into the type), each **rounding**, and the **library** functions such as `sin` or `exp` |
| **measured** | The actual error, found by repeating the calculation with far more precision |
| **κ** | The condition number: how much the problem itself magnifies small changes in its inputs |
| **trusted digits** | How many digits the bound guarantees |

## Build and test

Requires CMake ≥ 3.25, Ninja and a C++17 compiler. Boost.Multiprecision and GoogleTest are
fetched and pinned automatically.

```bash
cmake --preset native && cmake --build --preset native && ctest --preset native
```

<details>
<summary>WebAssembly</summary>

The tests run under Node.js.

```bash
source /path/to/emsdk/emsdk_env.sh
cmake --preset wasm && cmake --build --preset wasm && ctest --preset wasm
```

</details>

The tests include golden files: the exact output of `calc`, which must be the same byte for byte
natively and in WebAssembly.

## The command line

```bash
calc [options] [expression ...]
```

`calc` evaluates each expression, or each line of standard input when none is given. They all
share `Ans` and the memory `M`, which the lines `M+`, `M-` and `MC` update.

| Option | Effect |
|---|---|
| `--type <t>` | The number type: `double` by default (see below) |
| `--angle <u>` | `rad` (default), `deg` or `grad` |
| `--log 10\|e` | What `log(x)` means: base 10 (default) or natural |
| `--mod truncated\|floored` | The sign of `mod`: the dividend's (default) or the divisor's |
| `--percent divide\|of-value` | `x + p%` adds p/100 (default) or p% of x |
| `--json` | One JSON object per expression |
| `--color <when>` | `auto` (default), `always` or `never` |
| `--allow-uncertain` | Let `!`, `nCr`, `gcd`… take arguments that carry an error |
| `--list-types` | Describe the number types of this build |

Errors point at their cause:

```text
$ calc "2π"
2π
 ^ Missing operator before 'π' (write 2×π, not 2π)
```

## Number types

| `--type` | Format | Significand | ≈ Digits |
|---|---|--:|--:|
| `float` | IEEE binary32 | 24 bits | 7 |
| `double` | IEEE binary64 | 53 bits | 16 |
| `long-double` | The platform's: x87 extended, or binary128 | 64 / 113 bits | 19 / 34 |
| `exact` | Rational numbers, no rounding | — | all |
| `binary128` | IEEE binary128, in software | 113 bits | 34 |
| `binary256` | IEEE binary256, in software | 237 bits | 71 |
| `binary512` | IEEE binary512, in software | 489 bits | 147 |

The software types have no subnormal numbers. With `exact`, results are fractions, and their decimals
show the repeating block (`1/3 = 0.(3)`). Roots and powers work when the result is rational;
transcendental functions are unavailable.

## The language

| Kind | Syntax |
|---|---|
| Operators | `+  -  *  /  ^` and postfix `!  %  ²  ³`, prefix `√  ∛` (also `×  ÷  −`) |
| Constants | `pi` (or `π`), `e`, `Ans`, `M` |
| Roots, powers and logarithms | `sqrt  cbrt  root(x, n)  exp  ln  log(x)  log10(x)  log(x, b)  abs` |
| Trigonometry | `sin  cos  tan  asin  acos  atan` and `sinh  cosh  tanh  asinh  acosh  atanh` |
| Integers | `mod(a, b)  rem(a, b)  floormod(a, b)  gcd  lcm  nCr  nPr` |
| Statistics | `mean  median  var  stdev  varp  stdevp` of any number of values |
| Sums and products | `sum(f; from; to)`, `sum(f; from; to; k)`, `product(…)`, also `Σ  ∑  Π  ∏`: the variable is `x` unless named, the limits exact whole numbers |
| Conversions | `… to fraction` (also `->` and `→`): the same result in another form; `to fraction` alone converts Ans |
| Comments | `# …` after an expression, or alone as a note: kept in the history, never evaluated |
| Variables | `name := expression` stores the expression (as text, with names expanded) under any name that is not a constant, function or reserved word; it is recomputed in each number type |

`^` is right-associative and `-2^2` is −4. There is no implicit multiplication: write `2π` as `2×π`.
Arguments are separated by `,` or `;`.

`log(x)` is base 10, `mod(a, b)` keeps the sign of a, and `%` divides by 100, by default; each is a setting
(`Options::conventions`; `--log e`, `--mod floored`, `--percent of-value` for `calc`). `log10`, `ln`, `rem` (sign
of a) and `floormod` (sign of b) never change. Results and stored texts (Ans, M) are written with these explicit
names, so changing a setting never changes what an earlier result means.
The Spanish names work too: `sen  arcsen  arccos  arctan`, `senh  arcsenh  arccosh  arctanh`,
`mcd` (gcd) and `mcm` (lcm).

## Using the library

C++:
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

CMake:
```cmake
include(FetchContent)
FetchContent_Declare(calculate-core GIT_REPOSITORY https://github.com/bentxr/calculate-core.git GIT_TAG <tag>)
FetchContent_MakeAvailable(calculate-core)
target_link_libraries(your-app PRIVATE calculate-core)
```

## Status

WIP. The desktop and web app is being built in
[**calculate**](https://github.com/bentxr/calculate).

## License

Not chosen yet. Until one is added, all rights are reserved.
