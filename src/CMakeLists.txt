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
    fetch_track_handler.cpp
    subscribe_track_handler.cpp
    server.cpp
    quic_transport.cpp
    transport.cpp
    transport_picoquic.cpp
    joining_fetch_handler.cpp
    $<$<BOOL:${USE_MVFST}>:transport_mvfst.cpp>
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

# Link mvfst libraries if enabled
if(USE_MVFST)
    # Re-find mvfst to ensure targets are available in this scope
    find_package(mvfst CONFIG REQUIRED)
    find_package(folly CONFIG REQUIRED)
    find_package(gflags CONFIG REQUIRED)

    # Fix folly's broken gflags target reference on macOS
    # Folly references "gflags_shared" but gflags exports "gflags::gflags_shared"
    if(NOT TARGET gflags_shared AND TARGET gflags::gflags_shared)
        add_library(gflags_shared ALIAS gflags::gflags_shared)
    endif()

    # Fix folly's broken include paths on macOS
    # Folly's cmake config hardcodes the SDK path it was built with, which may differ
    # from the current SDK. This breaks C++ header include order.
    # We need to filter the problematic path from ALL folly/mvfst related targets.
    if(APPLE)
        # Function to fix include paths for a target
        function(fix_target_includes target_name)
            if(TARGET ${target_name})
                get_target_property(_includes ${target_name} INTERFACE_INCLUDE_DIRECTORIES)
                if(_includes)
                    set(_new_includes "")
                    foreach(_inc IN LISTS _includes)
                        # Skip CommandLineTools SDK include paths
                        if(NOT _inc MATCHES ".*/CommandLineTools/SDKs/.*/usr/include")
                            list(APPEND _new_includes "${_inc}")
                        endif()
                    endforeach()
                    set_target_properties(${target_name} PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${_new_includes}")
                endif()
            endif()
        endfunction()

        # Fix all known problematic targets
        fix_target_includes(Folly::folly)
        fix_target_includes(Folly::folly_deps)
        fix_target_includes(fizz::fizz)
    endif()

    find_package(fizz CONFIG REQUIRED)

    target_link_libraries(quicr
        PUBLIC
            mvfst::mvfst_transport
            mvfst::mvfst_client
            mvfst::mvfst_server
            mvfst::mvfst_fizz_client
            fizz::fizz
            Folly::folly
    )

    # Add proxygen for WebTransport support if available
    # NOTE: Disabled because homebrew's proxygen doesn't include QuicWebTransport implementation
    # find_package(proxygen CONFIG QUIET)
    # if(proxygen_FOUND)
    #     message(STATUS "Linking with proxygen for WebTransport support")
    #     target_compile_definitions(quicr PRIVATE HAVE_PROXYGEN_WEBTRANSPORT)
    #     target_link_libraries(quicr
    #         PUBLIC
    #             proxygen::proxygen
    #             proxygen::proxygenhttpserver
    #     )
    #     if(APPLE)
    #         fix_target_includes(proxygen::proxygen)
    #     endif()
    # endif()

endif()

add_dependencies(quicr ${ctrl_messages_TARGET})

if (PLATFORM_ESP_IDF)
    add_compile_definitions(${LIB_NAME} PLATFORM_ESP)
endif()

target_include_directories(quicr PUBLIC ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/dependencies/picoquic/picohttp)
