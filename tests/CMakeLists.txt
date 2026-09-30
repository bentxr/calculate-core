FetchContent_Declare(googletest
    URL      https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
    URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
    SYSTEM)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(calculate-core-tests
    test_build.cpp
    test_numbers.cpp
)
target_include_directories(calculate-core-tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(calculate-core-tests PRIVATE
    calculate-core calculate-core-boost calculate-core-warnings GTest::gtest_main)
if(EMSCRIPTEN)
    target_link_options(calculate-core-tests PRIVATE -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576)
endif()

include(GoogleTest)
gtest_discover_tests(calculate-core-tests DISCOVERY_MODE PRE_TEST)
