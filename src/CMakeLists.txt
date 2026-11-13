# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

#=============================================================================#
# QuicR Library
#=============================================================================#

if (QUICR_BUILD_SHARED)
    add_library(quicr SHARED)
else()
    add_library(quicr)
endif()

add_library(quicr::quicr ALIAS quicr)

set_target_properties(quicr PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)

target_compile_features(quicr PUBLIC cxx_std_20)
target_compile_options(quicr PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>)
target_compile_options(quicr PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wall -pedantic -Wextra -Wmissing-declarations -Werror=switch>
    $<$<CXX_COMPILER_ID:MSVC>:/MP>
)

include(CheckThreadSafetyAnnotations)
if(QUICR_HAS_THREAD_SAFETY_ANNOTATIONS)
    target_compile_definitions(quicr PUBLIC
        QUICR_HAS_THREAD_SAFETY_ANNOTATIONS
        _LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS)
endif()

if(NOT QUICR_BUILD_FUZZ)
    target_compile_options(quicr PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>: -Wthread-safety -Werror=thread-safety>
    )
endif()

target_sources(quicr PRIVATE
    clock.cpp
    connection.cpp
    ctrl_message_types.cpp
    joining_fetch_handler.cpp
    log.cpp
    message_serialisation.cpp
    picoquic_connection.cpp
    containers/priority_queue.h
    publish_fetch_handler.cpp
    publish_namespace_handler.cpp
    publish_track_handler.cpp
    session_callbacks.cpp
    session_manager.cpp
    session.cpp
    subscribe_namespace_handler.cpp
    subscribe_track_handler.cpp
    track_handler.cpp
    track_status_handler.cpp
    transport_picoquic.cpp
    transport.cpp
)

target_link_libraries(quicr PRIVATE
    timeq::timeq
    picoquic-core
    picoquic-log
    picohttp-core
)

if (WIN32)
    target_compile_definitions(quicr PRIVATE NOMINMAX)
    target_link_libraries(quicr PRIVATE ws2_32)
endif()

if (QUICR_HAS_STD_FORMAT)
    target_compile_definitions(quicr PUBLIC QUICR_HAS_STD_FORMAT)
else()
    target_link_libraries(quicr PUBLIC fmt::fmt)
endif()

target_include_directories(quicr PUBLIC
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

#=============================================================================#
# QuicR C API
#=============================================================================#

if (QUICR_BUILD_SHARED)
    add_library(quicr_c SHARED)
else()
    add_library(quicr_c)
endif()

add_library(quicr::c ALIAS quicr_c)
set_target_properties(quicr_c PROPERTIES EXPORT_NAME c)
target_compile_features(quicr_c INTERFACE c_std_11)

include(GenerateExportHeader)
generate_export_header(quicr_c
    EXPORT_FILE_NAME "${PROJECT_BINARY_DIR}/include/quicr/quicr_c_export.h"
    EXPORT_MACRO_NAME QUICR_C_API
    STATIC_DEFINE QUICR_C_STATIC_DEFINE
)

target_sources(quicr_c PRIVATE quicr.cpp)
target_link_libraries(quicr_c PRIVATE quicr::quicr)
target_include_directories(quicr_c PUBLIC
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

#=============================================================================#
# Linting
#=============================================================================#

if(QUICR_LINT)
    include(Lint)
    lint(quicr)
    lint(quicr_c)
endif()
