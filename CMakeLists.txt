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

include(CMakeDependentOption)

cmake_dependent_option(QUICR_BUILD_TESTS "Build tests for quicr" ON CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OFF)
cmake_dependent_option(QUICR_BUILD_BENCHMARKS "Build benchmarks for quicr" ON CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OFF)
cmake_dependent_option(QUICR_BUILD_C_BRIDGE "Build C Bridge API for quicr" ON CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME OFF)

option(QUICR_BUILD_SHARED "Build quicr as a SHARED library" OFF)
option(QUICR_BUILD_FUZZ "Build fuzzer targets" OFF)
option(QUICR_BUILD_EXAMPLES "Build examples" OFF)
option(QUICR_ENABLE_SANITIZERS "Build with ASAN and UBSAN" OFF)

option(USE_MBEDTLS OFF)
option(LINT "Perform linting with clang-tidy" OFF)

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
  set(USE_MBEDTLS ON)
endif()

#=============================================================================#
# Dependencies
#=============================================================================#

if (NOT DEFINED ENV{IDF_PATH})
    set (CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
endif()

include(CPM)

include(CheckIncludeFileCXX)
check_include_file_cxx(format HAVE_STD_FORMAT)
if (HAVE_STD_FORMAT)
    unset(HAS_STD_VFORMAT CACHE)
    set(CMAKE_REQUIRED_FLAGS "-std=c++20")

    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("
        #include <format>
        #include <string>
        int main() {
            std::string fmt = \"Hello {}\";
            std::string result = std::vformat(fmt, std::make_format_args(\"World\"));
            return result.empty() ? 1 : 0;
        }
    " HAS_STD_VFORMAT)

    unset(CMAKE_REQUIRED_FLAGS)
endif()

if (NOT HAS_STD_VFORMAT)
    message(STATUS "Using fmt::format")
    CPMAddPackage("gh:fmtlib/fmt#12.2.0")
else()
    add_compile_definitions(QUICR_HAVE_STD_FORMAT)
    message(STATUS "Using std::format")
endif()

add_subdirectory(dependencies)


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
