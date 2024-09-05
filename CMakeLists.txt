# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.13)

# Build tests by default only if not a sub-project
if(DEFINED PROJECT_NAME)
    option(quicr_BUILD_TESTS "Build tests for quicr" OFF)
else()
    option(quicr_BUILD_TESTS "Build tests for quicr" ON)
endif()

project(quicr
        VERSION 1.0.0.0
        DESCRIPTION "quicr library"
        LANGUAGES CXX)

option(LINT "Perform linting with clang-tidy" OFF)
option(QUICR_BUILD_SHARED "Build quicr as a SHARED library" OFF)
option(PLATFORM_ESP_IDF "Enabble support for esp-idf (Default OFF)" OFF)
option(USE_MBEDTLS OFF)

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
set (CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/transport/cmake)

###
### Include Ctest to ensure BUILD_TESTING is set
###
include(CTest)

###
### Dependencies
###
add_subdirectory(dependencies)

###
### Build the quicr library
###
add_subdirectory(src)

###
### Enable testing and add tests if quicr_BUILD_TESTS is ON
###
if(BUILD_TESTING AND quicr_BUILD_TESTS)
    add_subdirectory(test)
endif()

###
### Applications
###
if(BUILD_TESTING AND quicr_BUILD_TESTS)
   add_subdirectory(cmd)
endif()
