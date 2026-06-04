# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

if (${QUICR_BUILD_SHARED})
    add_library(quicr SHARED)
else()
    add_library(quicr)
endif()

set_target_properties(quicr PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)

target_compile_options(quicr PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wall -pedantic -Wextra -Wmissing-declarations -Werror=switch>
)

target_sources(quicr PRIVATE
    base_track_handler.cpp
    ctrl_message_types.cpp
    fetch_track_handler.cpp
    joining_fetch_handler.cpp
    messages.cpp
    publish_fetch_handler.cpp
    publish_namespace_handler.cpp
    publish_track_handler.cpp
    quic_transport.cpp
    subscribe_namespace_handler.cpp
    subscribe_track_handler.cpp
    transport_picoquic.cpp
    transport.cpp
)

target_link_libraries(quicr PUBLIC
    spdlog
    picoquic-core
    picoquic-log
    picohttp-core
)

target_include_directories(quicr PUBLIC ${CMAKE_BINARY_DIR}/include ${PROJECT_SOURCE_DIR}/include)

if(LINT)
    include(Lint)
    lint(quicr)
endif()
