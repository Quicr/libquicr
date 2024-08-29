# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

add_library(quicr)
target_sources (quicr PRIVATE
        message_buffer.cpp
        quic/transport.cpp
        quic/transport_picoquic.cpp
        moq/messages.cpp
        moq/publish_track_handler.cpp
        moq/transport.cpp
        moq/server.cpp
        moq/client.cpp
)

# Suppress external lib warnings
set_property(GLOBAL PROPERTY RULE_MESSAGES OFF)

target_compile_options(quicr
    PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
        $<$<CXX_COMPILER_ID:MSVC>: >)
set_target_properties(quicr
    PROPERTIES
        CXX_STANDARD 17
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

if (NOT PLATFORM_ESP_IDF)
    target_link_libraries(quicr
            PUBLIC
            picoquic-core picoquic-log)
endif()
if (PLATFORM_ESP_IDF)
    add_compile_definitions(${LIB_NAME} PLATFORM_ESP)
endif()

target_include_directories(quicr PUBLIC ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)

