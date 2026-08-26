cmake_minimum_required(VERSION 3.16)
project(calculate-core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# One shared library: build/libcalculate-core.so.0.1.0 (+ soname symlinks).
add_library(calculate-core SHARED
    src/algebra.cpp
)

# Consumers get this on their include path:
#   #include <calculate-core/calculate-core.hpp>
target_include_directories(calculate-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Defined only while compiling the library itself, so export.hpp knows to
# EXPORT symbols (consumers, who never define it, IMPORT them instead).
target_compile_definitions(calculate-core PRIVATE CALCULATE_CORE_BUILD)

set_target_properties(calculate-core PROPERTIES
    # Versioning: the real file carries VERSION; SOVERSION drives the soname.
    #   libcalculate-core.so -> .so.0 (soname) -> .so.0.1.0 (real file)
    # Bump SOVERSION whenever you break the ABI; bump VERSION every release.
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}

    # Hide every symbol unless it is tagged CALCULATE_CORE_API.
    CXX_VISIBILITY_PRESET     hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# ---- Tests ----
# Defines the BUILD_TESTING option (default ON) and enables CTest.
# Skip building tests with:  cmake -S . -B build -DBUILD_TESTING=OFF
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
