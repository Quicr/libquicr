# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.13)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(CMakeDependentOption)

# Build tests by default only if not a sub-project
cmake_dependent_option(QUICR_BUILD_TESTS "Build tests for quicr" ON DEFINED PROJECT_NAME OFF)
cmake_dependent_option(QUICR_BUILD_INTEGRATION_TESTS "Also build integration tests for quicr" ON DEFINED PROJECT_NAME OFF)

project(quicr
    VERSION 1.12.4
    DESCRIPTION "QuicR, a Media over QUIC Library"
    LANGUAGES CXX
)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
    message(FATAL_ERROR "In-source builds are disallowed, prefer libquicr's Makefile or cmake -B <build> -S <source>. You may need to delete the already generated Cache and CMake folder to continue.")
endif()

configure_file( src/version.h.in ${CMAKE_BINARY_DIR}/include/quicr/version.h )

#=============================================================================#
# Platform Checks
#=============================================================================#

IF (NOT UNIX AND NOT APPLE AND NOT PLATFORM_ESP_IDF)
    message(FATAL_ERROR "Unsupported platform, Linux and Apple are the only supported platforms")
endif ()

set(SUPPORTED_ARCHES arm64 x86_64 aarch64)
if (NOT ${CMAKE_SYSTEM_PROCESSOR} IN_LIST SUPPORTED_ARCHES)
    message(FATAL_ERROR "Unsupported system architecture '${CMAKE_SYSTEM_PROCESSOR}'. Supported is arm64 and x86_64")
endif()

message(STATUS "Building for ${CMAKE_SYSTEM_PROCESSOR}")

#=============================================================================#
# Options
#=============================================================================#

option(quicr_BUILD_BENCHMARKS "Build benchmarks for quicr" OFF)
option(LINT "Perform linting with clang-tidy" OFF)
option(QUICR_BUILD_SHARED "Build quicr as a SHARED library" OFF)
option(PLATFORM_ESP_IDF "Enable support for esp-idf (Default OFF)" OFF)
option(DRAFT_PARSER_SETUP_VENV "Set up Python virtual environment for draft parser" ON)
option(QUICR_BUILD_FUZZ "Build fuzzer targets" OFF)
option(quicr_BUILD_EXAMPLES "Build examples" OFF)

include(CMakeDependentOption)

cmake_dependent_option(USE_MBEDTLS "Enable MbedTLS" ON PLATFORM_ESP_IDF OFF)
message(STATUS "USE_MBEDTLS: ${USE_MBEDTLS}")

#=============================================================================#
# Global Config
#=============================================================================#

set (BUILD_SHARED_LIBS OFF)
set (BUILD_STATIC_LIBS ON)

if (NOT PLATFORM_ESP_IDF)
    set (CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/transport/cmake)
endif()

#=============================================================================#
# Dependencies
#=============================================================================#

if (NOT PLATFORM_ESP_IDF)
  find_package(Threads REQUIRED)
  find_package(PkgConfig REQUIRED)
endif()

include(CTest)

add_subdirectory(dependencies)

#=============================================================================#
# Draft Parser Integration
#=============================================================================#

# Which MOQ draft to use.
# Addendum is added automatically
set(DEFAULT_DRAFT "${CMAKE_CURRENT_SOURCE_DIR}/tools/draft_parser/drafts/draft-ietf-moq-transport-14_edited.txt")
set(DRAFT "" CACHE FILEPATH "MOQ Draft to build against")
if (DRAFT STREQUAL "")
    set(DRAFT_INTERNAL ${DEFAULT_DRAFT})
else()
    set(DRAFT_INTERNAL ${DRAFT})
endif()

include(tools/draft_parser/cmake/ParseDraft.cmake)
parse_draft(
    RFC_FILE ${DRAFT_INTERNAL}
    OUTPUT_NAME "ctrl_messages"
    OUTPUT_DIR "${CMAKE_BINARY_DIR}/include/quicr/detail"
    SETUP_VENV ${DRAFT_PARSER_SETUP_VENV}
)

#=============================================================================#
# Build the quicr library
#=============================================================================#

add_subdirectory(src)

#=============================================================================#
# Enable testing
#=============================================================================#

if(BUILD_TESTING)
    add_subdirectory(test)
    enable_testing()
endif()

#=============================================================================#
# Enable Benchmarking
#=============================================================================#

if (quicr_BUILD_BENCHMARKS)
   add_subdirectory(benchmark)
endif()

#=============================================================================#
# Build Examples
#=============================================================================#

if(quicr_BUILD_EXAMPLES)
   add_subdirectory(examples)
endif()
