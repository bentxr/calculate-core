FetchContent_Declare(googletest
    URL      https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
    URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
    SYSTEM)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(calculate-core-tests
    test_build.cpp
    test_numbers.cpp
    test_eft.cpp
    test_decimal.cpp
    test_ieee.cpp
    test_constants.cpp
    test_public.cpp
    test_ast.cpp
    test_functions.cpp
    test_engine.cpp
    test_doubleword.cpp
    test_kernels.cpp
    test_parser.cpp
    test_api.cpp
    test_catalogue.cpp
    test_fuzz.cpp
    test_cli.cpp
)
target_include_directories(calculate-core-tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(calculate-core-tests PRIVATE
    calculate-core calc-cli calculate-core-boost calculate-core-warnings GTest::gtest_main)
if(EMSCRIPTEN)
    target_link_options(calculate-core-tests PRIVATE -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576)
endif()

include(GoogleTest)
gtest_discover_tests(calculate-core-tests DISCOVERY_MODE PRE_TEST)

# Golden files: calc's exact output, which must be identical on every platform (only long double
# differs, because it is a different format: 64-bit significand on x86, 113-bit elsewhere).
if(EMSCRIPTEN OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86")
    set(long_double_bits 113)
else()
    set(long_double_bits 64)
endif()
string(REPLACE ";" "|" emulator "${CMAKE_CROSSCOMPILING_EMULATOR}")
file(GLOB golden_cases RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/golden CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/golden/*.args)
foreach(case IN LISTS golden_cases)
    string(REGEX REPLACE "\\.args$" "" name "${case}")
    add_test(NAME golden.${name}
             COMMAND ${CMAKE_COMMAND} "-DCALC=$<TARGET_FILE:calc>" "-DEMULATOR=${emulator}"
                     "-DCASE=${CMAKE_CURRENT_SOURCE_DIR}/golden/${name}"
                     "-DLONG_DOUBLE_BITS=${long_double_bits}"
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/golden/run.cmake)
endforeach()
