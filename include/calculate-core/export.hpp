#pragma once

// Visibility / linkage macros for the shared library's public ABI.
//
// Tag every public declaration with CALCULATE_CORE_API so it is exported
// from the .so (and imported by consumers). Everything left untagged stays
// hidden — internal to the library — thanks to -fvisibility=hidden.
//
// CALCULATE_CORE_BUILD is defined by the build system (see CMakeLists.txt)
// only while compiling the library itself. Consumers never define it.

#if defined(_WIN32) || defined(__CYGWIN__)
  #ifdef CALCULATE_CORE_BUILD
    #define CALCULATE_CORE_API __declspec(dllexport)
  #else
    #define CALCULATE_CORE_API __declspec(dllimport)
  #endif
  #define CALCULATE_CORE_LOCAL
#else
  #define CALCULATE_CORE_API   __attribute__((visibility("default")))
  #define CALCULATE_CORE_LOCAL __attribute__((visibility("hidden")))
#endif
