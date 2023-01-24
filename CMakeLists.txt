cmake_minimum_required(VERSION 3.13)

# Building tests by default depends on whether this is a subproject
if(DEFINED PROJECT_NAME)
    option(quicr_BUILD_TESTS "Build Tests for quicr" OFF)
else()
    option(quicr_BUILD_TESTS "Build Tests for quicr" ON)
endif()

project(quicr
        VERSION 1.0.0.0
        DESCRIPTION "quicr library"
        LANGUAGES CXX)

option(CLANG_TIDY "Perform linting with clang-tidy" OFF)

###
### Global Config
###
if(CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY  ${CLANG_TIDY_EXE})
    else()
        message(WARNING "clang-tidy requested, but not found")
    endif()
endif()

###
### Dependencies
###

# Fetch external content
add_subdirectory(dependencies)

###
### Build the quicr library
###

add_subdirectory(src)

###
### Enable testing and add tests if quicr_BUILD_TESTS is ON
###
include(CTest)
if(BUILD_TESTING AND quicr_BUILD_TESTS)
    find_package(doctest QUIET)
    if(doctest_FOUND)
        # Include for doctest_discover_tests()
        include(doctest)
    else()
        # Include the CMake file for doctest_discover_tests() if content fetched
        include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
    endif()
    add_subdirectory(test)
endif()

###
### Applications
###
###
add_subdirectory(cmd)
