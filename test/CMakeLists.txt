# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

CPMAddPackage("gh:doctest/doctest@2.5.2")

add_executable(quicr_test
    cache.cpp
    data_storage.cpp
    main.cpp
    moq_ctrl_messages.cpp
    moq_data_messages.cpp
    picoquic_connection.cpp
    priority_queue.cpp
    reply.cpp
    track_handlers.cpp
    track_namespace.cpp
    metrics.cpp
    uintvar.cpp
)

if(QUICR_ENABLE_SANITIZERS)
    target_sources(quicr_test PRIVATE sanitizer_options.cpp)
    quicr_enable_sanitizers(quicr_test)
endif()

# quicr links its implementation dependencies privately, so their include paths do not reach this
# target. They are needed here because unit tests include private headers directly.
target_include_directories(quicr_test PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    $<TARGET_PROPERTY:picoquic-core,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:picoquic-log,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:picohttp-core,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(quicr_test PRIVATE
    doctest::doctest
    quicr::quicr
    timeq::timeq
)

set_target_properties(quicr_test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS ON
)

target_compile_options(quicr_test PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
)

if (WIN32)
    target_compile_definitions(quicr_test PRIVATE NOMINMAX)
endif()

if (doctest_SOURCE_DIR)
    list(APPEND CMAKE_MODULE_PATH "${doctest_SOURCE_DIR}/scripts/cmake")
else()
    list(APPEND CMAKE_MODULE_PATH "${doctest_DIR}")
endif()

include(doctest)

doctest_discover_tests(quicr_test)

find_program(QUICR_OPENSSL_EXECUTABLE openssl)
if(QUICR_OPENSSL_EXECUTABLE)
    add_subdirectory(integration_test)
else()
    message(WARNING "Skipping certificate-backed integration tests because the openssl command was not found")
endif()

if(QUICR_LINT)
    include(Lint)
    lint(quicr_test)
endif()
