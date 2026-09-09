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


set(BUILD_SHARED_LIBS OFF)
set(BUILD_STATIC_LIBS ON)

# Time queue
add_subdirectory(timeq)

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
        add_subdirectory(mbedtls) #TODO: Remove this and use CPMAddPackage instead.

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

    CPMAddPackage("gh:quicr/picotls#3927c48be674d97ca8242580fab3b4af4eb6df2a")

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
    set(BUILD_LOGREADER OFF)

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
    set(BUILD_PQBENCH OFF)
    set(BUILD_LOGREADER OFF)

    CPMAddPackage("gh:quicr/picotls#3927c48be674d97ca8242580fab3b4af4eb6df2a")

    if (WIN32)
        target_compile_definitions(picotls-core PUBLIC _WINDOWS)
        target_include_directories(picotls-core PUBLIC
            "$<BUILD_INTERFACE:${picotls_SOURCE_DIR}/picotlsvs/picotls>")
        target_sources(picotls-core PRIVATE
            "${picotls_SOURCE_DIR}/picotlsvs/picotls/wintimeofday.c")

        target_link_libraries(picotls-minicrypto bcrypt)

        foreach(tgt cli test-openssl.t)
            if (TARGET ${tgt})
                set_target_properties(${tgt} PROPERTIES EXCLUDE_FROM_ALL ON)
            endif()
        endforeach()
    endif()

    set(PICOQUIC_FETCH_PTLS ON)
    set(picoquic_BUILD_TESTS OFF)
    add_subdirectory(picoquic)
    add_dependencies(picoquic-core picotls-core)

    if (WIN32)
        target_link_libraries(picoquic-core PUBLIC ws2_32 iphlpapi)
    endif()

    set(WARN_SUPPRESS_TARGET_LIST picoquic-core picoquic-log picohttp-core picotls-core picotls-openssl picotls-minicrypto)
    foreach(tgt ${WARN_SUPPRESS_TARGET_LIST})
        target_compile_options(${tgt} PRIVATE
            $<$<C_COMPILER_ID:MSVC>:/w>
            $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>)
    endforeach()
endif ()
