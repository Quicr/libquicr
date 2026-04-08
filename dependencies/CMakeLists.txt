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
        set(MBEDTLS_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/include)

    elseif(DEFINED ENV{IDF_PATH})
        set(MBEDTLS_ROOT_DIR ${IDF_PATH}/components/mbedtls/mbedtls)
        set(MBEDTLS_INCLUDE_DIR ${IDF_PATH}/components/mbedtls/mbedtls ${IDF_PATH}/components/mbedtls/mbedtls/include ${IDF_PATH}/components/mbedtls/port/include)
    endif()

    set(MBEDTLS_LIBRARY mbedtls)
    set(MBEDTLS_CRYPTO mbedcrypto)
    set(MBEDTLS_X509 mbedx509)

    set(MBEDTLS_INCLUDE_DIRS ${MBEDTLS_INCLUDE_DIR})
    set(MBEDTLS_LIBRARIES mbedtls mbedcrypto mbedx509)

    # picoTLS
    # picoTLS uses its own find mbedtls, which works with the above settings.
    set(WITH_MBEDTLS ON)
    set(WITH_FUSION OFF)
    set(picotls_BUILD_TESTS OFF)

    include(FetchContent)
    FetchContent_Declare(picotls
        GIT_REPOSITORY      https://github.com/quicr/picotls.git
        GIT_TAG             3927c48be674d97ca8242580fab3b4af4eb6df2a)
    FetchContent_MakeAvailable(picotls)

    add_dependencies(picotls-mbedtls mbedtls)

    if (DEFINED ENV{IDF_PATH})
        set(MBEDTLS_PREFIX ${IDF_PATH}/components/mbedtls/mbedtls)
        set(WITH_SELECT ON)
    else()
        set(MBEDTLS_PREFIX ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls)
    endif()

    set(WITH_MBEDTLS ON)

    set(OPENSSL_INCLUDE_DIR "") # Picoquic will still error if this is not set
    set(WITH_OPENSSL OFF)
    set(BUILD_HTTP ON)
    set(BUILD_DEMO OFF)
    set(BUILD_PQBENCH OFF)
    set(BUILD_LOGREADER OFF) # Disable picolog_t build

    set(PICOQUIC_ADDITIONAL_C_FLAGS -Wno-error=format)
    set(PICOQUIC_ADDITIONAL_CXX_FLAGS -Wno-error=format)

    set(PTLS_INCLUDE_DIR ${picotls_SOURCE_DIR}/include)
    set(picoquic_BUILD_TESTS OFF)
    set(PICOQUIC_FETCH_PTLS ON)

    add_subdirectory(picoquic)
    add_dependencies(picoquic-core picotls-core mbedtls)
else ()
    set(WITH_FUSION OFF)
    set(WITH_OPENSSL ON)
    set(ENABLE_TESTING OFF)
    set(BUILD_DEMO OFF)

        # BoringSSL (include only for upstream openssl usage)
        set(boringssl_BUILD_TESTS OFF)
        add_subdirectory(boringssl)

        # Setup OpenSSL vars for submodules
        set(OPENSSL_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/boringssl CACHE STRING "" FORCE)
        set(OPENSSL_LIBDIR ${CMAKE_CURRENT_BINARY_DIR}/boringssl CACHE STRING "" FORCE)
        set(OPENSSL_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/boringssl/include CACHE STRING "" FORCE)
        set(OPENSSL_LIBRARIES BoringSSL::ssl CACHE STRING "" FORCE)
        set(OPENSSL_SSL_LIBRARIES BoringSSL::ssl CACHE STRING "" FORCE)
        set(OPENSSL_SSL_LIBRARY BoringSSL::ssl CACHE STRING "" FORCE)
        set(OPENSSL_CRYPTO_LIBRARY BoringSSL::crypto CACHE STRING "" FORCE)
        set(OPENSSL_CRYPTO_LIBRARIES "${OPENSSL_CRYPTO_LIBRARY}" CACHE STRING "" FORCE)

        # Create aliases for this and upstream submodules
        add_library(BoringSSL::ssl ALIAS ssl)
        add_library(BoringSSL::crypto ALIAS crypto)
        add_library(BoringSSL::decrepit ALIAS decrepit)
        add_library(OpenSSL::SSL ALIAS ssl)
        add_library(OpenSSL::Crypto ALIAS crypto)
        export(TARGETS ssl crypto decrepit FILE boringssl.cmake) # Need to export as alias alone in upstream submodules doesn't work 


    # picoTLS
    set(WITH_OPENSSL ON)
    set(PICOTLS_USE_BROTLI OFF)
    set(picotls_BUILD_TESTS OFF)
    # add_subdirectory(picotls)  # Patched - FetchContent handles this
    # add_dependencies(picotls-openssl ssl decrepit crypto)  # Moved after FetchContent
    # add_dependencies(picotls-core decrepit crypto ssl)  # Moved after FetchContent
    # add_dependencies(test-openssl.t decrepit crypto ssl)  # Moved after FetchContent


    include(FetchContent)
    FetchContent_Declare(picotls
        GIT_REPOSITORY      https://github.com/quicr/picotls.git
        GIT_TAG             3927c48be674d97ca8242580fab3b4af4eb6df2a)
    FetchContent_MakeAvailable(picotls)

    # Add dependencies after FetchContent makes targets available
    add_dependencies(picotls-openssl ssl decrepit crypto)
    add_dependencies(picotls-core decrepit crypto ssl)
    if(TARGET test-openssl.t)
        add_dependencies(test-openssl.t decrepit crypto ssl)
    endif()

    set(PICOQUIC_FETCH_PTLS ON)
    set(picoquic_BUILD_TESTS OFF)
    add_subdirectory(picoquic)
    add_dependencies(picoquic-core picotls-core)
endif ()
