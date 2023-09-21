cmake_minimum_required(VERSION 3.13)

# Build tests by default only if not a sub-project
if(DEFINED PROJECT_NAME)
    option(QUICR_BUILD_TESTS "Build tests for quicr" OFF)
    option(quicr_BUILD_BENCHMARKS "Build benchmarks for quicr" OFF)
else()
    option(QUICR_BUILD_TESTS "Build tests for quicr" ON)
    option(quicr_BUILD_BENCHMARKS "Build benchmarks for quicr" ON)
endif()

project(quicr
        VERSION 1.0.0.0
        DESCRIPTION "quicr library"
        LANGUAGES CXX)

option(CLANG_TIDY "Perform linting with clang-tidy" OFF)

###
### Include Ctest to ensure BUILD_TESTING is set
###
include(CTest)

###
### Dependencies
###
add_subdirectory(dependencies)

###
### Global Config
###
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  add_compile_options(-Wall -pedantic -Wextra -Werror -Wmissing-declarations)
elseif(MSVC)
  add_compile_options(/W4 /WX)

  # MSVC helpfully recommends safer equivalents for things like
  # getenv, but they are not portable.
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)  
endif()

if(CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
    if(CLANG_TIDY_EXE)
        message(STATUS "Found clang-tidy: ${CLANG_TIDY_EXE}")
        set(CMAKE_CXX_CLANG_TIDY  ${CLANG_TIDY_EXE})
    else()
        message(WARNING "clang-tidy requested, but not found")
    endif()
endif()

###
### Build the quicr library
###
add_subdirectory(src)

###
### Enable testing and add tests if QUICR_BUILD_TESTS is ON
###
if(BUILD_TESTING AND QUICR_BUILD_TESTS)
    add_subdirectory(test)
endif()

###
### Applications
###
if(BUILD_TESTING AND QUICR_BUILD_TESTS)
  add_subdirectory(cmd)
endif()

if (BUILD_BENCHMARKING AND quicr_BUILD_BENCHMARKS)
    include(FetchContent)
    set(BENCHMARK_USE_BUNDLED_GTEST OFF)
    set(BENCHMARK_ENABLE_TESTING OFF)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG main
    )
    FetchContent_MakeAvailable(benchmark)
    add_subdirectory(benchmark)
endif()
