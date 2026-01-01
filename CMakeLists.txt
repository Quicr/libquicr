# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.13)

set (CMAKE_POLICY_VERSION_MINIMUM 3.5)

# Build tests by default only if not a sub-project
if(DEFINED PROJECT_NAME)
    option(QUICR_BUILD_TESTS "Build tests for quicr" OFF)
    option(quicr_BUILD_BENCHMARKS "Build benchmarks for quicr" OFF)
    option(QUICR_BUILD_C_BRIDGE "Build C Bridge API for quicr" OFF)
else()
    option(QUICR_BUILD_TESTS "Build tests for quicr" ON)
    option(quicr_BUILD_BENCHMARKS "Build benchmarks for quicr" ON)
    option(QUICR_BUILD_INTEGRATION_TESTS "Also build integration tests for quicr" ON)
    option(QUICR_BUILD_C_BRIDGE "Build C Bridge API for quicr" ON)
endif()

project(quicr
        VERSION 1.14.0
        DESCRIPTION "QuicR, a Media over QUIC Library"
        LANGUAGES C CXX)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
    message(FATAL_ERROR
    "In-source builds are disallowed, prefer libquicr's Makefile or cmake -B <build> -S <source>
You may need to delete the already generated Cache and CMake folder to continue.")
endif()

configure_file( src/version.h.in ${CMAKE_BINARY_DIR}/include/quicr/version.h )

IF (NOT UNIX AND NOT APPLE AND NOT PLATFORM_ESP_IDF AND NOT PLATFORM_ESP_HAL)
    message(FATAL_ERROR "Unsupported platform, Linux and Apple are the only supported platforms")
endif ()

set (supported_archs arm64 x86_64 aarch64 i686 armv7-a arm64-v8a xtensa riscv32)
if (NOT ${CMAKE_SYSTEM_PROCESSOR} IN_LIST supported_archs)
    message(FATAL_ERROR "Unsupported system architecture '${CMAKE_SYSTEM_PROCESSOR}'. Supported is arm64, x86_64, xtensa, riscv32")
endif()

message(STATUS "Building for ${CMAKE_SYSTEM_PROCESSOR}")

option(LINT "Perform linting with clang-tidy" OFF)
option(QUICR_BUILD_SHARED "Build quicr as a SHARED library" OFF)
option(PLATFORM_ESP_IDF "Enable support for esp-idf (Default OFF)" OFF)
option(PLATFORM_ESP_HAL "Enable support for esp-hal bare-metal (Default OFF)" OFF)
option(USE_MBEDTLS OFF)
option(DRAFT_PARSER_SETUP_VENV "Set up Python virtual environment for draft parser" ON)
option(QUICR_BUILD_FUZZ "Build fuzzer targets" OFF)

# Which MOQ draft to use.
# Addendum is added automatically
set(DEFAULT_DRAFT "${CMAKE_CURRENT_SOURCE_DIR}/tools/draft_parser/drafts/draft-ietf-moq-transport-14_edited.txt")
set(DRAFT "" CACHE FILEPATH "MOQ Draft to build against")
if (DRAFT STREQUAL "")
    set(DRAFT_INTERNAL ${DEFAULT_DRAFT})
else()
    set(DRAFT_INTERNAL ${DRAFT})
endif()

if (NOT PLATFORM_ESP_IDF AND NOT PLATFORM_ESP_HAL)
  find_package(Threads REQUIRED)
  find_package(PkgConfig REQUIRED)
else()
  set(USE_MBEDTLS ON)
endif()

# Bare-metal (esp-hal) specific configuration
if (PLATFORM_ESP_HAL)
  message(STATUS "Building for bare-metal ESP-HAL platform")
  add_compile_definitions(QUICR_BAREMETAL=1)

  # Add baremetal include directory for POSIX compatibility headers
  set(QUICR_BAREMETAL_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/platform/baremetal/include)
  include_directories(BEFORE ${QUICR_BAREMETAL_INCLUDE_DIR})

  # Force include the bare-metal compatibility header
  set(QUICR_BAREMETAL_COMPAT_HEADER ${QUICR_BAREMETAL_INCLUDE_DIR}/quicr_baremetal_compat.h)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include ${QUICR_BAREMETAL_COMPAT_HEADER}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include ${QUICR_BAREMETAL_COMPAT_HEADER}")

  # Disable features not available on bare-metal
  set(QUICR_BUILD_TESTS OFF)
  set(QUICR_BUILD_C_BRIDGE ON)  # Build C bridge for Rust FFI
  set(BUILD_BRIDGE_EXAMPLES OFF CACHE BOOL "" FORCE)  # No examples on bare-metal
  set(quicr_BUILD_BENCHMARKS OFF)
  set(DRAFT_PARSER_SETUP_VENV OFF)

  # Use lwIP for networking if available
  if (DEFINED LWIP_DIR)
    add_compile_definitions(QUICR_USE_LWIP=1)
    include_directories(${LWIP_DIR}/src/include)
  endif()
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
# Skip draft parser for bare-metal builds - use pre-generated headers
if (NOT PLATFORM_ESP_HAL)
    include(tools/draft_parser/cmake/ParseDraft.cmake)
    parse_draft(
        RFC_FILE ${DRAFT_INTERNAL}
        OUTPUT_NAME "ctrl_messages"
        OUTPUT_DIR "${CMAKE_BINARY_DIR}/include/quicr/detail"
        SETUP_VENV ${DRAFT_PARSER_SETUP_VENV}
    )
else()
    message(STATUS "Skipping draft parser for bare-metal build - using pre-generated headers")
    # For bare-metal, copy pre-generated ctrl_messages from platform/baremetal
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/include/quicr/detail")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/baremetal/pregenerated/quicr/detail/ctrl_messages.h"
        "${CMAKE_BINARY_DIR}/include/quicr/detail/ctrl_messages.h"
        COPYONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/baremetal/pregenerated/quicr/detail/ctrl_messages.cpp"
        "${CMAKE_BINARY_DIR}/include/quicr/detail/ctrl_messages.cpp"
        COPYONLY)
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
### C Bridge API
###
if(QUICR_BUILD_C_BRIDGE)
   add_subdirectory(c-bridge)
endif()

###
### Applications
###
if(BUILD_TESTING AND QUICR_BUILD_TESTS)
   add_subdirectory(cmd)
endif()
