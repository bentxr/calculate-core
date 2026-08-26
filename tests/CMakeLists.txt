# Pull in GoogleTest at configure time (no system install needed).
# Pinned to a specific release so every checkout builds against the same
# version; bump GIT_TAG to upgrade. Cached in build/_deps after first fetch.
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
# Windows-only: make GoogleTest use the same runtime as the rest of the build.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# One executable holding all the tests. Add a file per category here as the
# library grows (test_stats.cpp, test_trig.cpp, ...).
add_executable(calculate-core-tests
    test_algebra.cpp
)

# Link the library under test + GoogleTest's main() entry point.
target_link_libraries(calculate-core-tests
    PRIVATE
        calculate-core
        GTest::gtest_main
)

# Register every TEST() with CTest so `ctest` finds and runs them.
include(GoogleTest)
gtest_discover_tests(calculate-core-tests)
