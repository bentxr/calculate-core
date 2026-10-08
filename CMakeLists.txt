cmake_minimum_required(VERSION 3.25)
project(calculate-core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Numerical correctness: no FMA contraction. No exceptions: Qt for WebAssembly has none,
# and every target must behave the same.
add_compile_options(-ffp-contract=off -fno-exceptions)

include(FetchContent)
FetchContent_Declare(boost_config
    URL      https://github.com/boostorg/config/archive/refs/tags/boost-1.92.0.tar.gz
    URL_HASH SHA256=b4171037f13373203ba79cbc141d612982052283e696a315185ab5bea46102a0
    SOURCE_SUBDIR no-cmake)
FetchContent_Declare(boost_multiprecision
    URL      https://github.com/boostorg/multiprecision/archive/refs/tags/boost-1.92.0.tar.gz
    URL_HASH SHA256=9da997843edd802f9d5e25d53872a7676330f31a52a5e47a418d1c94ba3b01d0
    SOURCE_SUBDIR no-cmake)
FetchContent_MakeAvailable(boost_config boost_multiprecision)

# Header-only Boost.Multiprecision in standalone mode (needs only Boost.Config).
add_library(calculate-core-boost INTERFACE)
target_include_directories(calculate-core-boost SYSTEM INTERFACE
    ${boost_config_SOURCE_DIR}/include
    ${boost_multiprecision_SOURCE_DIR}/include)
target_compile_definitions(calculate-core-boost INTERFACE BOOST_MP_STANDALONE)

add_library(calculate-core-warnings INTERFACE)
target_compile_options(calculate-core-warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow)
if(PROJECT_IS_TOP_LEVEL)  # warnings are errors here, but never break a project that includes this one
    target_compile_options(calculate-core-warnings INTERFACE -Werror)
endif()

add_library(calculate-core STATIC src/calculate.cpp src/catalogue.cpp src/parser.cpp src/targets.cpp)
target_include_directories(calculate-core
    PUBLIC  ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(calculate-core PRIVATE calculate-core-boost calculate-core-warnings)

add_library(calc-cli STATIC cli/cli.cpp)
target_include_directories(calc-cli PUBLIC ${PROJECT_SOURCE_DIR}/cli)
target_compile_definitions(calc-cli PRIVATE CALCULATE_VERSION="${PROJECT_VERSION}")
target_link_libraries(calc-cli PUBLIC calculate-core PRIVATE calculate-core-warnings)

add_executable(calc cli/main.cpp)
target_link_libraries(calc PRIVATE calc-cli calculate-core-warnings)
if(EMSCRIPTEN)
    target_link_options(calc PRIVATE -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sNODERAWFS=1)
endif()

include(CTest)
if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
    add_subdirectory(tests)
endif()
