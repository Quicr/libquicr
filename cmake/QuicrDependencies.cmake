# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

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

set(BUILD_SHARED_LIBS OFF)
set(BUILD_STATIC_LIBS ON)

CPMAddPackage("gh:quicr/timeq#898d45c")

CPMAddPackage(
    URI "gh:quicr/picotls#sync-092226"
    OPTIONS
        "WITH_FUSION OFF"
        "picotls_BUILD_TESTS OFF"
)

if (WITH_MBEDTLS)
    set(WITH_OPENSSL OFF)
    set(PTLS_INCLUDE_DIR ${picotls_SOURCE_DIR}/include)
else()
    if (TARGET picotls-openssl)
        set(WITH_OPENSSL ON)
    else()
        set(WITH_OPENSSL OFF)
        message(WARNING
            "No external TLS provider was found; using picotls-minicrypto. "
            "This backend does not verify peer certificates in the current picoquic integration. "
            "Use it only where peer authentication is not required.")
    endif()
endif()

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

CPMAddPackage(
    URI "gh:private-octopus/picoquic@2026.10.25.1d81a73"
    OPTIONS
        "BUILD_DEMO OFF"
        "BUILD_PQBENCH OFF"
        "BUILD_LOGREADER OFF"
        "picoquic_BUILD_TESTS OFF"
        "PICOQUIC_FETCH_PTLS ON"
)

set(WARN_SUPPRESS_TARGET_LIST picoquic-core picoquic-log picohttp-core picotls-core picotls-openssl picotls-minicrypto)

if (WITH_MBEDTLS)
    list(APPEND WARN_SUPPRESS_TARGET_LIST picotls-mbedtls)
endif()

if (WIN32)
    target_link_libraries(picoquic-core PUBLIC ws2_32 iphlpapi)
endif()

foreach(tgt ${WARN_SUPPRESS_TARGET_LIST})
    if (TARGET ${tgt})
        target_compile_options(${tgt} PRIVATE
            $<$<C_COMPILER_ID:MSVC>:/w>
            $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>)
    endif()
endforeach()
