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

# For bare-metal builds, use stub spdlog from platform/baremetal
if (NOT PLATFORM_ESP_HAL)
    if (NOT TARGET spdlog)
        add_subdirectory(spdlog)
    endif()
else()
    # Create a header-only stub target for bare-metal
    add_library(spdlog INTERFACE)
    target_include_directories(spdlog INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/../platform/baremetal/include)
    # Create alias to match real spdlog's namespace
    add_library(spdlog::spdlog ALIAS spdlog)
    message(STATUS "Using bare-metal spdlog stub")
endif()

if(BUILD_TESTING AND QUICR_BUILD_TESTS AND NOT TARGET doctest)
    add_subdirectory(doctest)
endif()

set(BUILD_SHARED_LIBS OFF)
set(BUILD_STATIC_LIBS ON)

# mbedtls
if (${USE_MBEDTLS})
    message("Transport building with MBedTLS")

    if (PLATFORM_ESP_HAL)
        # For bare-metal ESP-HAL, build mbedtls with cross-compilation settings
        message(STATUS "Building mbedtls for bare-metal ESP-HAL")

        option(ENABLE_TESTING OFF)
        option(ENABLE_PROGRAMS OFF)
        set(MBEDTLS_AS_SUBPROJECT ON)

        # Disable features that require OS support
        set(MBEDTLS_THREADING OFF CACHE BOOL "" FORCE)
        set(MBEDTLS_FS_IO OFF CACHE BOOL "" FORCE)

        # Add compile definitions for bare-metal
        add_compile_definitions(MBEDTLS_NO_PLATFORM_ENTROPY)

        add_subdirectory(mbedtls)

        # Remove net_sockets.c from mbedtls target for bare-metal
        # net_sockets.c requires Unix/Windows and has a compile-time #error
        get_target_property(MBEDTLS_SOURCES mbedtls SOURCES)
        if(MBEDTLS_SOURCES)
            list(FILTER MBEDTLS_SOURCES EXCLUDE REGEX "net_sockets\\.c$")
            set_target_properties(mbedtls PROPERTIES SOURCES "${MBEDTLS_SOURCES}")
        endif()

        # Disable -Werror for mbedtls on bare-metal (various header redefinition warnings)
        if(TARGET mbedtls)
            target_compile_options(mbedtls PRIVATE -Wno-error)
        endif()
        if(TARGET mbedx509)
            target_compile_options(mbedx509 PRIVATE -Wno-error)
        endif()
        if(TARGET mbedcrypto)
            target_compile_options(mbedcrypto PRIVATE -Wno-error)
        endif()
        if(TARGET builtin)
            target_compile_options(builtin PRIVATE -Wno-error)
        endif()
        if(TARGET tfpsacrypto)
            target_compile_options(tfpsacrypto PRIVATE -Wno-error)
        endif()

        set(MBEDTLS_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls)
        set(MBEDTLS_INCLUDE_DIRS
            ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls
            ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/include
            ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/tf-psa-crypto/include
            ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/tf-psa-crypto/drivers/builtin/include)

    elseif (NOT TARGET mbedcrypto) # If not already available on build server
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
        set(MBEDTLS_INCLUDE_DIRS
            ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls
            ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/include
            ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/tf-psa-crypto/include
            ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/tf-psa-crypto/drivers/builtin/include)

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

    # For bare-metal, QUICR_BAREMETAL is already defined and compat header is force-included
    # via parent CMakeLists.txt

    add_subdirectory(picotls)
    add_dependencies(picotls-mbedtls mbedtls)

    # picoQUIC
    # Picoquic uses its own find mbedtls, which works with a prefix and will not work if mbedtls include dirs is set
    if(NOT PLATFORM_ESP_IDF AND NOT PLATFORM_ESP_HAL)
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
    # Set PTLS_INCLUDE_DIR for FindPTLS.cmake when using submodule with mbedtls
    set(PTLS_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/picotls/include)
    set(WITH_OPENSSL OFF CACHE BOOL "" FORCE)
    set(BUILD_DEMO OFF CACHE BOOL "" FORCE)
    set(BUILD_HTTP ON CACHE BOOL "" FORCE)
    set(BUILD_LOGREADER OFF CACHE BOOL "" FORCE)
    set(picoquic_BUILD_TESTS OFF)

    # For bare-metal, disable socket-dependent features
    # (CMAKE_C/CXX_FLAGS already include the baremetal include directory)
    if (PLATFORM_ESP_HAL)
        set(PICOQUIC_ADDITIONAL_C_FLAGS -Wno-error=format)
        set(PICOQUIC_ADDITIONAL_CXX_FLAGS -Wno-error=format)
        # Disable socket loop and demo for bare-metal
        set(BUILD_SOCKLOOP OFF CACHE BOOL "" FORCE)
    else()
        set(PICOQUIC_ADDITIONAL_C_FLAGS -Wno-error=format)
        set(PICOQUIC_ADDITIONAL_CXX_FLAGS -Wno-error=format)
    endif()

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
