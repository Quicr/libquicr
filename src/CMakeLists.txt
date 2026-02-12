# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

if (${QUICR_BUILD_SHARED})
    add_library(quicr SHARED)
else()
    add_library(quicr)
endif()
target_sources (quicr PRIVATE
    base_track_handler.cpp
    client.cpp
    ctrl_message_types.cpp
    ${ctrl_messages_SOURCE}
    messages.cpp
    publish_fetch_handler.cpp
    publish_track_handler.cpp
    publish_namespace_handler.cpp
    fetch_track_handler.cpp
    subscribe_track_handler.cpp
    subscribe_namespace_handler.cpp
    server.cpp
    quic_transport.cpp
    transport.cpp
    transport_picoquic.cpp
    joining_fetch_handler.cpp
)

set_source_files_properties(${ctrl_messages_SOURCE} PROPERTIES GENERATED TRUE)

target_include_directories(quicr PUBLIC ${CMAKE_BINARY_DIR}/include )

# Suppress external lib warnings
set_property(GLOBAL PROPERTY RULE_MESSAGES OFF)

target_compile_options(quicr
    PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall -Werror=switch>
        $<$<CXX_COMPILER_ID:MSVC>: >)
set_target_properties(quicr
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(quicr PRIVATE -Wall -pedantic -Wextra  -Wmissing-declarations)
endif()

if(MSVC)
    target_compile_options(quicr PRIVATE /W4 /WX)
    target_compile_definitions(quicr _CRT_SECURE_NO_WARNINGS)
endif()

target_compile_definitions(quicr PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

if(LINT)
    include(Lint)
    lint(quicr)
endif()

target_link_libraries(quicr
    PUBLIC
        spdlog)

target_link_libraries(quicr
    PUBLIC
        picoquic-core picoquic-log picohttp-core)

add_dependencies(quicr ${ctrl_messages_TARGET})

if (PLATFORM_ESP_IDF)
    add_compile_definitions(${LIB_NAME} PLATFORM_ESP)
endif()

target_include_directories(quicr PUBLIC ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/dependencies/picoquic/picohttp)
