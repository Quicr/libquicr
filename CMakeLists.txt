# SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.13)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(quicr
    VERSION 1.18.0
    DESCRIPTION "QuicR, a Media over QUIC Library"
    LANGUAGES C CXX
)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
    message(FATAL_ERROR
    "In-source builds are disallowed, prefer libquicr's Makefile or cmake -B <build> -S <source>
You may need to delete the already generated Cache and CMake folder to continue.")
endif()

configure_file(cmake/version.h.in ${CMAKE_BINARY_DIR}/include/quicr/version.h)

#=============================================================================#
# Options
#=============================================================================#

include(CMakeDependentOption)

if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(quicr_IS_TOP_LEVEL ON)
endif()

cmake_dependent_option(QUICR_BUILD_TESTS "Build tests for quicr" ON quicr_IS_TOP_LEVEL OFF)
cmake_dependent_option(QUICR_BUILD_BENCHMARKS "Build benchmarks for quicr" ON quicr_IS_TOP_LEVEL OFF)
cmake_dependent_option(QUICR_BUILD_C_BRIDGE "Build C Bridge API for quicr" ON quicr_IS_TOP_LEVEL OFF)

option(QUICR_BUILD_SHARED "Build quicr as a SHARED library" OFF)
option(QUICR_BUILD_FUZZ "Build fuzzer targets" OFF)
option(QUICR_BUILD_EXAMPLES "Build examples" OFF)
option(QUICR_ENABLE_SANITIZERS "Build with ASAN and UBSAN" OFF)

option(WITH_MBEDTLS "Build against MbedTLS rather than OpenSSL" OFF)
option(WITH_BORINGSSL "Use BoringSSL as the OpenSSL provider (required on Android)" OFF)
option(LINT "Perform linting with clang-tidy" OFF)
option(QUICR_USE_BROTLI "Enable picotls Brotli TLS certificate compression (libbrotlidec)" OFF)

if(QUICR_ENABLE_SANITIZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang|GNU" OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "QUICR_ENABLE_SANITIZERS requires Clang or GCC")
    endif()
    add_compile_options(
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -fno-sanitize-recover=undefined
    )
    add_link_options(-fsanitize=address,undefined)
endif()

if (NOT DEFINED ENV{IDF_PATH})
  find_package(Threads REQUIRED)
  find_package(PkgConfig REQUIRED)
else()
  set(WITH_MBEDTLS ON CACHE BOOL "Build against MbedTLS rather than OpenSSL" FORCE)
endif()

# Android's NDK ships no linkable system OpenSSL, so default the OpenSSL provider
# to BoringSSL there. It can still be requested on any platform via
# -DWITH_BORINGSSL=ON.
if (ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(WITH_BORINGSSL ON)
endif()

if (WITH_MBEDTLS AND WITH_BORINGSSL)
    message(FATAL_ERROR "WITH_MBEDTLS and WITH_BORINGSSL are mutually exclusive; enable at most one crypto backend")
endif()

#=============================================================================#
# Dependencies
#=============================================================================#

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(CPM)

include(QuicrFormat)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/QuicrDependencies.cmake)

#=============================================================================#
# Build the library
#=============================================================================#

add_subdirectory(src)

#=============================================================================#
# Testing
#=============================================================================

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    include(CTest)
endif()

if(BUILD_TESTING)
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
       add_subdirectory(examples)
    endif()
endif()

#=============================================================================#
# C Bridge API
#=============================================================================#

if(QUICR_BUILD_C_BRIDGE)
   add_subdirectory(c-bridge)
endif()
