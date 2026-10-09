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

if (WITH_MBEDTLS)
    message(STATUS "Transport building with MbedTLS")

    set(WITH_OPENSSL OFF)
endif ()

set(WITH_FUSION OFF)
set(BUILD_DEMO OFF)
set(BUILD_PQBENCH OFF)
set(BUILD_LOGREADER OFF)
set(picotls_BUILD_TESTS OFF)

CPMAddPackage("gh:quicr/picotls#sync-092226")

if (NOT WITH_MBEDTLS)
    if (TARGET picotls-openssl)
        set(WITH_OPENSSL ON)
    else()
        set(WITH_OPENSSL OFF)
    endif()
endif()

set(PTLS_INCLUDE_DIR ${picotls_SOURCE_DIR}/include)

if (WIN32 AND NOT WITH_MBEDTLS)
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

set(picoquic_BUILD_TESTS OFF)
set(PICOQUIC_FETCH_PTLS ON)
CPMAddPackage("gh:private-octopus/picoquic#poll-recvmmsg")
add_dependencies(picoquic-core picotls-core)

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
