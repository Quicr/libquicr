# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.13)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# Build tests by default only if not a sub-project
if(DEFINED PROJECT_NAME)
    option(QUICR_BUILD_TESTS "Build tests for quicr" OFF)
    option(quicr_BUILD_BENCHMARKS "Build benchmarks for quicr" OFF)
else()
    option(QUICR_BUILD_TESTS "Build tests for quicr" ON)
    option(quicr_BUILD_BENCHMARKS "Build benchmarks for quicr" ON)
    option(QUICR_BUILD_INTEGRATION_TESTS "Also build integration tests for quicr" ON)
endif()

project(quicr
        VERSION 1.12.4
        DESCRIPTION "QuicR, a Media over QUIC Library"
        LANGUAGES CXX)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
    message(FATAL_ERROR
    "In-source builds are disallowed, prefer libquicr's Makefile or cmake -B <build> -S <source>
You may need to delete the already generated Cache and CMake folder to continue.")
endif()

configure_file( src/version.h.in ${CMAKE_BINARY_DIR}/include/quicr/version.h )

IF (NOT UNIX AND NOT APPLE AND NOT PLATFORM_ESP_IDF)
    message(FATAL_ERROR "Unsupported platform, Linux and Apple are the only supported platforms")
endif ()

set (supported_archs arm64 x86_64 aarch64)
if (NOT ${CMAKE_SYSTEM_PROCESSOR} IN_LIST supported_archs)
    message(FATAL_ERROR "Unsupported system architecture '${CMAKE_SYSTEM_PROCESSOR}'. Supported is arm64 and x86_64")
endif()

message(STATUS "Building for ${CMAKE_SYSTEM_PROCESSOR}")

option(LINT "Perform linting with clang-tidy" OFF)
option(QUICR_BUILD_SHARED "Build quicr as a SHARED library" OFF)
option(PLATFORM_ESP_IDF "Enabble support for esp-idf (Default OFF)" OFF)
option(USE_MBEDTLS OFF)
option(DRAFT_PARSER_SETUP_VENV "Set up Python virtual environment for draft parser" ON)
option(QUICR_BUILD_FUZZ "Build fuzzer targets" OFF)

# Which MOQ draft to use.
set(DEFAULT_DRAFT "${CMAKE_CURRENT_SOURCE_DIR}/tools/draft_parser/drafts/moq_transport_draft_v12_with_addendum.txt")
set(DRAFT "" CACHE FILEPATH "MOQ Draft to build against")
if (DRAFT STREQUAL "")
    set(DRAFT_INTERNAL ${DEFAULT_DRAFT})
else()
    set(DRAFT_INTERNAL ${DRAFT})
endif()

if (NOT PLATFORM_ESP_IDF)
  find_package(Threads REQUIRED)
  find_package(PkgConfig REQUIRED)
else()
  set(USE_MBEDTLS ON)
endif()

###
### Global Config
###
set (BUILD_SHARED_LIBS OFF)
set (BUILD_STATIC_LIBS ON)

if (NOT PLATFORM_ESP_IDF)
    set (CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/transport/cmake)
endif()

###
### Include Ctest to ensure BUILD_TESTING is set
###
include(CTest)

###
### Dependencies
###
add_subdirectory(dependencies)

###
### Draft Parser Integration
###
include(tools/draft_parser/cmake/ParseDraft.cmake)
parse_draft(
    RFC_FILE ${DRAFT_INTERNAL}
    OUTPUT_NAME "ctrl_messages"
    OUTPUT_DIR "${CMAKE_BINARY_DIR}/include/quicr/detail"
    SETUP_VENV ${DRAFT_PARSER_SETUP_VENV}
)

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

if (BUILD_BENCHMARKING AND quicr_BUILD_BENCHMARKS)
   add_subdirectory(benchmark)
endif()

###
### Fuzzing targets
###
if(QUICR_BUILD_FUZZ)
   add_subdirectory(fuzz)
endif()

###
### Applications
###
if(BUILD_TESTING AND QUICR_BUILD_TESTS)
   add_subdirectory(cmd)
endif()
