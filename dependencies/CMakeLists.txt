# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

# ----------------------------------------------------
# Command overrides
# ----------------------------------------------------

# Override install command to add FRAMEWORK DESTINATION if missing. This is needed for framework (apple) builds
if(CMAKE_FRAMEWORK)
    macro(install)
        set (list_args "${ARGN}")
        if ("TARGETS" IN_LIST list_args AND NOT "FRAMEWORK" IN_LIST list_args)
            list(APPEND list_args "FRAMEWORK" "DESTINATION" "framework" "BUNDLE" "DESTINATION" "bundle")
            message(STATUS "Adding FRAMEWORK DESTINATION to install: ${list_args}")
            _install(${list_args})
        else ()
            message(STATUS "unchanged install: ${list_args}")
            _install(${ARGN})
        endif ()
    endmacro()
endif()

# --------------------------------------------------------
# Submodules
# --------------------------------------------------------

# Note: We use spdlog's bundled fmt even with mvfst to avoid
# version conflicts. The mvfst_compat.h header handles the
# macro conflicts between macOS math.h and fmt.

if (NOT TARGET spdlog)
    add_subdirectory(spdlog)
endif()

if(BUILD_TESTING AND QUICR_BUILD_TESTS AND NOT TARGET doctest)
    add_subdirectory(doctest)
endif()

set(BUILD_SHARED_LIBS OFF)
set(BUILD_STATIC_LIBS ON)

# mbedtls
if (${USE_MBEDTLS})
    message("Transport building with MBedTLS")

    if (NOT TARGET mbedcrypto) # If not already available on build server
        # Setup python environment for mbedtls to find.
        find_package(Python3 REQUIRED)
        execute_process(
                COMMAND
                ${Python3_EXECUTABLE} -m venv ${CMAKE_CURRENT_BINARY_DIR}/venv
                RESULT_VARIABLE
                MBEDTLS_VIRTUALENV
        )
        if (MBEDTLS_VIRTUALENV)
            message(FATAL_ERROR "Failed to create mbedtls virtual envrionment")
        endif (MBEDTLS_VIRTUALENV)

        # Override python lookup to use virtualenv when mbedtls later searches for it.
        set(Python3_EXECUTABLE ${CMAKE_CURRENT_BINARY_DIR}/venv/bin/python)

        # Install mbedtls dependencies into virtualenv.
        execute_process(
                COMMAND
                ${Python3_EXECUTABLE} -m pip install -r ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/scripts/basic.requirements.txt
                RESULT_VARIABLE
                MBEDTLS_PIP
        )
        if (MBEDTLS_PIP)
            message(FATAL_ERROR "Failed to install mbedtls dependencies")
        endif (MBEDTLS_PIP)

        # Continue with mbedtls inclusion.
        option(ENABLE_TESTING OFF)
        option(ENABLE_PROGRAMS OFF)
        set(MBEDTLS_AS_SUBPROJECT ON)
        add_subdirectory(mbedtls)

        set(MBEDTLS_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls)
        set(MBEDTLS_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/include)

    elseif(PLATFORM_ESP_IDF)
        set(MBEDTLS_ROOT_DIR ${IDF_PATH}/components/mbedtls/mbedtls)
        set(MBEDTLS_INCLUDE_DIRS ${IDF_PATH}/components/mbedtls/mbedtls ${IDF_PATH}/components/mbedtls/mbedtls/include ${IDF_PATH}/components/mbedtls/port/include)
    endif()

    set(MBEDTLS_LIBRARIES mbedtls mbedcrypto mbedx509)
    set(MBEDTLS_LIBRARY mbedtls)
    set(MBEDTLS_CRYPTO mbedcrypto)
    set(MBEDTLS_X509 mbedx509)


    # picoTLS
    # picoTLS uses its own find mbedtls, which works with the above settings.
    set(WITH_MBEDTLS ON)
    option(WITH_FUSION OFF)
    set(PICOTLS_USE_BROTLI OFF)
    set(picotls_BUILD_TESTS OFF)
    add_subdirectory(picotls)
    add_dependencies(picotls-mbedtls mbedtls)

    # picoQUIC
    # Picoquic uses its own find mbedtls, which works with a prefix and will not work if mbedtls include dirs is set
    if(NOT PLATFORM_ESP_IDF)
        unset(MBEDTLS_INCLUDE_DIRS)
    endif()
    set(MbedTLS_FOUND ON)
    if (PLATFORM_ESP_IDF)
     set(MBEDTLS_PREFIX ${IDF_PATH}/components/mbedtls/mbedtls)
    else()
     set(MBEDTLS_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls)
    endif()
    option(WITH_MBEDTLS ON)

    set(OPENSSL_INCLUDE_DIR "") # Picoquic will still error if this is not set
    set(PICOQUIC_PTLS_SUBMODULE ON)
    option(WITH_OPENSSL OFF)
    option(BUILD_DEMO OFF)
    option(BUILD_HTTP ON)
    option(BUILD_LOGREADER OFF) # Disable picolog_t build
    set(picoquic_BUILD_TESTS OFF)
    set(PICOQUIC_ADDITIONAL_C_FLAGS -Wno-error=format)
    set(PICOQUIC_ADDITIONAL_CXX_FLAGS -Wno-error=format)
    add_subdirectory(picoquic)
    add_dependencies(picoquic-core picotls-core mbedtls)
else ()
    option(WITH_FUSION OFF)
    OPTION(ENABLE_TESTING OFF)

    # picoTLS
    set(WITH_OPENSSL ON)
    set(PICOTLS_USE_BROTLI OFF)
    set(picotls_BUILD_TESTS OFF)
    add_subdirectory(picotls)

    # picoQUIC
    set(PICOQUIC_PTLS_SUBMODULE ON)
    option(WITH_OPENSSL ON)
    set(picoquic_BUILD_TESTS OFF)
    add_subdirectory(picoquic)
    add_dependencies(picoquic-core picotls-core)

endif ()

# --------------------------------------------------------
# mvfst QUIC Stack (optional)
# --------------------------------------------------------
#
# mvfst and its dependencies (folly, fizz) have complex build requirements.
# They expect to be installed system-wide and found via find_package().
#
# RECOMMENDED: Install mvfst via homebrew or getdeps.py before building:
#   macOS: brew install mvfst folly fizz
#   Other: cd dependencies/mvfst && python3 build/fbcode_builder/getdeps.py build mvfst
#
# The submodules are provided for reference but building from source requires
# additional configuration (see plan document for details).

if(USE_MVFST)
    message(STATUS "Building with mvfst QUIC stack support")

    # Try to find system-installed mvfst first (recommended approach)
    # Note: CMAKE_MODULE_PATH with FindSodium.cmake is set in root CMakeLists.txt
    find_package(mvfst CONFIG QUIET)
    find_package(folly CONFIG QUIET)
    find_package(fizz CONFIG QUIET)
    find_package(proxygen CONFIG QUIET)

    if(mvfst_FOUND AND folly_FOUND AND fizz_FOUND)
        message(STATUS "Found system-installed mvfst, folly, fizz - using those")
        set(MVFST_FROM_SYSTEM ON CACHE BOOL "Using system-installed mvfst" FORCE)

        if(proxygen_FOUND)
            message(STATUS "Found system-installed proxygen - WebTransport support enabled")
            set(PROXYGEN_FOUND ON CACHE BOOL "Proxygen found for WebTransport support" FORCE)
        else()
            message(STATUS "proxygen not found - WebTransport will use basic QUIC close/drain")
            set(PROXYGEN_FOUND OFF CACHE BOOL "Proxygen not found" FORCE)
        endif()
        # Done - skip all submodule builds
    else()
        # System mvfst not found - submodule build is complex and typically fails
        # Recommend installing via homebrew/getdeps.py instead
        message(FATAL_ERROR
            "mvfst, folly, or fizz not found. Please install them first:\n"
            "  macOS: brew install mvfst\n"
            "  Linux: cd dependencies/mvfst && python3 build/fbcode_builder/getdeps.py build mvfst\n"
            "\n"
            "Building from submodules is not supported due to Facebook's CMake design.\n"
            "See the plan document for details on why system installation is required."
        )
    endif()

    message(STATUS "mvfst and dependencies configured")
endif()
