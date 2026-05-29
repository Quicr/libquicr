# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

CPMAddPackage("gh:doctest/doctest@2.5.2")

add_executable(quicr_test
    cache.cpp
    client.cpp
    data_storage.cpp
    main.cpp
    moq_ctrl_messages.cpp
    moq_data_messages.cpp
    moq_test.cpp
    priority_queue.cpp
    tick_service.cpp
    time_queue.cpp
    track_handlers.cpp
    track_namespace.cpp
    uintvar.cpp
)

target_include_directories(quicr_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(quicr_test PRIVATE quicr doctest::doctest)

set_target_properties(quicr_test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS ON
)

target_compile_options(quicr_test PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
)

if (doctest_ADDED)
    list(APPEND CMAKE_MODULE_PATH "${doctest_SOURCE_DIR}/scripts/cmake")
    include(doctest)
endif()

doctest_discover_tests(quicr_test)

add_subdirectory(integration_test)

if (LINT)
    include(Lint)
    Lint(quicr_test)
endif()
