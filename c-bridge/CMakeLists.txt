# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

add_library(quicr-bridge STATIC src/quicr_bridge.cpp)
add_library(quicr::bridge ALIAS quicr-bridge)

target_link_libraries(quicr-bridge PRIVATE quicr::quicr)

target_include_directories(quicr-bridge
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(quicr-bridge PROPERTIES
    EXPORT_NAME bridge
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/include/quicr/quicr_bridge.h"
)

target_compile_options(quicr-bridge PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
        -Wall -Wextra -Wpedantic -Wno-unused-parameter>
)

if(WIN32)
    target_compile_definitions(quicr-bridge PRIVATE NOMINMAX)
endif()

if(QUICR_BUILD_EXAMPLES)
    set(QUICR_BRIDGE_EXAMPLES
        simple_publisher
        simple_subscriber
        chat
        fetch
        file_transfer
    )

    foreach(example IN LISTS QUICR_BRIDGE_EXAMPLES)
        add_executable(${example} "examples/${example}.c")
        target_link_libraries(${example} PRIVATE quicr-bridge)
        target_compile_features(${example} PRIVATE c_std_11)
    endforeach()
endif()

