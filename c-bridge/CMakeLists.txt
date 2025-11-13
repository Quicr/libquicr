# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.16)

project(quicr-bridge
    VERSION 1.0.0
    DESCRIPTION "QuicR C Bridge Library"
    LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find required dependencies
find_package(PkgConfig REQUIRED)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/dependencies/spdlog/include
)

# Collect source files
file(GLOB_RECURSE BRIDGE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

file(GLOB_RECURSE BRIDGE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
)

# Create the bridge library
add_library(quicr-bridge SHARED ${BRIDGE_SOURCES} ${BRIDGE_HEADERS})

# Link against the main QuicR library
target_link_libraries(quicr-bridge 
    PRIVATE 
    quicr
    spdlog::spdlog
)

# Set include directories for the library
target_include_directories(quicr-bridge
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(quicr-bridge PRIVATE 
        -Wall -Wextra -Wpedantic 
        -Wno-unused-parameter
        -fPIC
    )
endif()

# Platform-specific settings
if(WIN32)
    target_compile_definitions(quicr-bridge PRIVATE QUICR_BRIDGE_EXPORTS)
endif()

# Set library properties
set_target_properties(quicr-bridge PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER "${BRIDGE_HEADERS}"
)

# Examples
option(BUILD_BRIDGE_EXAMPLES "Build C bridge example applications" ON)

if(BUILD_BRIDGE_EXAMPLES)
    # Simple Publisher Example
    add_executable(simple_publisher examples/simple_publisher.c)
    target_link_libraries(simple_publisher quicr-bridge)
    target_include_directories(simple_publisher PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    # Simple Subscriber Example
    add_executable(simple_subscriber examples/simple_subscriber.c)
    target_link_libraries(simple_subscriber quicr-bridge)
    target_include_directories(simple_subscriber PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    # Chat Example
    add_executable(chat examples/chat.c)
    target_link_libraries(chat quicr-bridge pthread)
    target_include_directories(chat PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    # Fetch Example
    add_executable(fetch examples/fetch.c)
    target_link_libraries(fetch quicr-bridge)
    target_include_directories(fetch PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    # File Transfer Example
    add_executable(file_transfer examples/file_transfer.c)
    target_link_libraries(file_transfer quicr-bridge)
    target_include_directories(file_transfer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    # Add examples to a custom target for easy building
    add_custom_target(bridge_examples
        DEPENDS simple_publisher simple_subscriber
                chat fetch file_transfer
    )
endif()

# Installation
install(TARGETS quicr-bridge
    EXPORT quicr-bridge-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION include/quicr
)

install(EXPORT quicr-bridge-targets
    FILE quicr-bridge-config.cmake
    DESTINATION lib/cmake/quicr-bridge
)

# Install examples if built
if(BUILD_BRIDGE_EXAMPLES)
    install(TARGETS simple_publisher simple_subscriber
                    chat fetch file_transfer
        RUNTIME DESTINATION bin/bridge_examples
    )
endif()

# Create a pkg-config file
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/quicr-bridge.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/quicr-bridge.pc
    @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/quicr-bridge.pc
    DESTINATION lib/pkgconfig
)

# Print summary
message(STATUS "")
message(STATUS "QuicR C Bridge Library Configuration Summary:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  C Standard: ${CMAKE_C_STANDARD}")
message(STATUS "  Build Examples: ${BUILD_BRIDGE_EXAMPLES}")
message(STATUS "  Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
