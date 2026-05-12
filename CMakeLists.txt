# SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.13)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(CMakeDependentOption)

cmake_dependent_option(QUICR_BUILD_TESTS "Build tests for quicr" OFF DEFINED PROJECT_NAME ON)
cmake_dependent_option(QUICR_BUILD_BENCHMARKS "Build benchmarks for quicr" OFF DEFINED PROJECT_NAME ON)
cmake_dependent_option(QUICR_BUILD_C_BRIDGE "Build C Bridge API for quicr" OFF DEFINED PROJECT_NAME ON)

project(quicr
        VERSION 1.14.0
        DESCRIPTION "QuicR, a Media over QUIC Library"
        LANGUAGES C CXX)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
    message(FATAL_ERROR
    "In-source builds are disallowed, prefer libquicr's Makefile or cmake -B <build> -S <source>
You may need to delete the already generated Cache and CMake folder to continue.")
endif()

configure_file(cmake/version.h.in ${CMAKE_BINARY_DIR}/include/quicr/version.h)

#=============================================================================#
# Check System Requirements
#=============================================================================#

if(NOT UNIX AND NOT APPLE AND NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR "Unsupported platform; Linux, Apple, and ESP-IDF are the only supported platforms")
endif ()

set(supported_archs arm64 x86_64 aarch64 i686 armv7-a arm64-v8a)
if (NOT ${CMAKE_SYSTEM_PROCESSOR} IN_LIST supported_archs)
    message(FATAL_ERROR "Unsupported system architecture '${CMAKE_SYSTEM_PROCESSOR}'. Supported is arm64 and x86_64")
endif()

message(STATUS "Building for ${CMAKE_SYSTEM_PROCESSOR}")

#=============================================================================#
# Options
#=============================================================================#

option(QUICR_BUILD_SHARED "Build quicr as a SHARED library" OFF)
option(QUICR_BUILD_FUZZ "Build fuzzer targets" OFF)
option(QUICR_BUILD_EXAMPLES "Build examples" OFF)

option(USE_MBEDTLS OFF)
option(LINT "Perform linting with clang-tidy" OFF)

if (NOT DEFINED ENV{IDF_PATH})
  find_package(Threads REQUIRED)
  find_package(PkgConfig REQUIRED)
else()
  set(USE_MBEDTLS ON)
endif()

set (BUILD_SHARED_LIBS OFF)
set (BUILD_STATIC_LIBS ON)

if (NOT DEFINED ENV{IDF_PATH})
    set (CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/transport/cmake)
endif()


#=============================================================================#
# Dependencies
#=============================================================================#

add_subdirectory(dependencies)

#=============================================================================#
# Build the library
#=============================================================================#

add_subdirectory(src)

#=============================================================================#
# Testing
#=============================================================================#

if(BUILD_TESTING)
    include(CTest)

    if(QUICR_BUILD_TESTS)
        add_subdirectory(test)
    endif()

    if(QUICR_BUILD_BENCHMARKS)
        add_subdirectory(benchmark)
    endif()

    if(QUICR_BUILD_FUZZ)
       add_subdirectory(fuzz)
    endif()

    if(QUICR_BUILD_EXAMPLES)
       add_subdirectory(cmd)
    endif()
endif()


#=============================================================================#
# C Bridge API
#=============================================================================#

if(QUICR_BUILD_C_BRIDGE)
   add_subdirectory(c-bridge)
endif()
