cmake_minimum_required(VERSION 3.14)
project(LLTI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -march=native -DNDEBUG")

include(FetchContent)

# Google Test
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

# Google Benchmark
FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.8.3
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

# Header-only library
add_library(llti INTERFACE)
target_include_directories(llti INTERFACE ${CMAKE_SOURCE_DIR}/include)

# Tests
add_executable(llti_tests tests/lookup_test.cpp tests/eytzinger_test.cpp)
target_link_libraries(llti_tests PRIVATE llti GTest::gtest_main)

# Benchmarks
add_executable(llti_benchmarks benchmarks/lookup_benchmark.cpp)
target_link_libraries(llti_benchmarks PRIVATE llti benchmark::benchmark benchmark::benchmark_main)

# Demo driver
add_executable(llti_demo src/main.cpp)
target_link_libraries(llti_demo PRIVATE llti)
