# SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.16)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" quicr_VERSION)
project(quicr
    VERSION "${quicr_VERSION}"
    DESCRIPTION "QuicR, a Media over QUIC Library"
    LANGUAGES C CXX
)

include(GNUInstallDirs)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not supported.\n"
        "Use libquicr's Makefile or run: cmake -B <build> -S <source>\n"
        "If an in-source build was already attempted, remove CMakeCache.txt and CMakeFiles/."
    )
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in"
    "${PROJECT_BINARY_DIR}/include/quicr/version.h"
    @ONLY
)

#=============================================================================#
# Options
#=============================================================================#

if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(quicr_IS_TOP_LEVEL ON)
endif()

option(QUICR_INSTALL "Enable install rules for quicr" ${quicr_IS_TOP_LEVEL})
option(QUICR_BUILD_SHARED "Build quicr as a SHARED library" OFF)
option(QUICR_BUILD_C_BRIDGE "Build C Bridge API for quicr" ${quicr_IS_TOP_LEVEL})
option(QUICR_BUILD_TOOLS "Build tools" OFF)
option(QUICR_BUILD_EXAMPLES "Build examples" OFF)
option(QUICR_ENABLE_SANITIZERS "Build with ASAN and UBSAN" OFF)
option(QUICR_GENERATE_THIRD_PARTY_LICENSES "Generate notices for distributed CPM dependencies" OFF)

include(CMakeDependentOption)

cmake_dependent_option(QUICR_BUILD_TESTS "Build tests for quicr" ON quicr_IS_TOP_LEVEL OFF)
cmake_dependent_option(QUICR_BUILD_BENCHMARKS "Build benchmarks for quicr" ON quicr_IS_TOP_LEVEL OFF)
cmake_dependent_option(QUICR_BUILD_FUZZ "Build fuzzer targets" OFF quicr_IS_TOP_LEVEL OFF)
cmake_dependent_option(QUICR_LINT "Perform linting with clang-tidy" OFF quicr_IS_TOP_LEVEL OFF)

if(QUICR_LINT AND CMAKE_VERSION VERSION_LESS 3.26)
    message(FATAL_ERROR "QUICR_LINT requires CMake 3.26 or newer")
endif()

#=============================================================================#
# Dependencies
#=============================================================================#

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CPM)

CPMUsePackageLock(package-lock.cmake)

include(QuicrFormat)
include(QuicrDependencies)

#=============================================================================#
# Build the library
#=============================================================================#

add_subdirectory(src)

if(QUICR_ENABLE_SANITIZERS)
    include(QuicrSanitizers)
    quicr_enable_sanitizers(quicr)
endif()

#=============================================================================#
# Tools and Examples
#=============================================================================#

if(QUICR_BUILD_TOOLS)
   add_subdirectory(tools)
endif()

if(QUICR_BUILD_EXAMPLES)
   add_subdirectory(examples)
endif()

#=============================================================================#
# Testing
#=============================================================================#

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
       add_subdirectory(test/fuzz)
    endif()
endif()

#=============================================================================#
# C Bridge API
#=============================================================================#

if(QUICR_BUILD_C_BRIDGE)
   add_subdirectory(c-bridge)
endif()

#=============================================================================#
# Install
#=============================================================================#

if (QUICR_INSTALL)
    include(CMakePackageConfigHelpers)

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/quicrConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/quicrConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/quicrConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/quicr"
    )

    install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/quicr/version.h" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/quicr")

    set(quicr_INSTALL_TARGETS quicr)
    if(TARGET quicr-bridge)
        list(APPEND quicr_INSTALL_TARGETS quicr-bridge)
    endif()

    install(TARGETS ${quicr_INSTALL_TARGETS}
        EXPORT quicrTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/quicr"
    )

    install(EXPORT quicrTargets
        FILE quicrTargets.cmake
        NAMESPACE quicr::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/quicr
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/quicrConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/quicrConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/quicr
    )
endif()

#=============================================================================#
# Licences
#=============================================================================#

if(QUICR_GENERATE_THIRD_PARTY_LICENSES)
    # CPMLicenses.cmake v0.0.7, pinned to the release commit.
    CPMAddPackage(
        NAME CPMLicenses
        GITHUB_REPOSITORY cpm-cmake/CPMLicenses.cmake
        VERSION 0.0.7
        GIT_TAG ca42334d561b83e499b11cf55fe05d97a0767fb9
        EXCLUDE_FROM_ALL YES
    )

    set(QUICR_DISTRIBUTED_CPM_PACKAGES timeq picotls picoquic)
    if(TARGET fmt)
        list(APPEND QUICR_DISTRIBUTED_CPM_PACKAGES fmt)
    endif()

    cpm_licenses_create_disclaimer_target(
        third-party-licenses
        "${PROJECT_BINARY_DIR}/THIRD_PARTY_LICENSES.txt"
        "${QUICR_DISTRIBUTED_CPM_PACKAGES}"
    )
endif()
