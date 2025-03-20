# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

add_executable(quicr_test
    main.cpp
    moq_messages.cpp
    moq_ctrl_messages.cpp
    #moq_both_messages.cpp
    moq_test.cpp
    track_handlers.cpp
    uintvar.cpp
    client.cpp
    tick_service.cpp
    track_namespace.cpp
    data_storage.cpp
    cache.cpp
)
target_include_directories(quicr_test PRIVATE ${PROJECT_SOURCE_DIR}/src)

target_link_libraries(quicr_test PRIVATE quicr doctest::doctest)

target_compile_options(quicr_test
    PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
        $<$<CXX_COMPILER_ID:MSVC>: >)

set_target_properties(quicr_test
    PROPERTIES
        CXX_STANDARD 17 
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS ON)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(quicr_test PRIVATE  -Wall -pedantic -Wextra)
endif()

if(MSVC)
    target_compile_options(quicr_test PRIVATE /W4 /WX)
    target_compile_definitions(quicr_test _CRT_SECURE_NO_WARNINGS)
endif()

include(${CMAKE_SOURCE_DIR}/dependencies/doctest/scripts/cmake/doctest.cmake)
doctest_discover_tests(quicr_test)

if (LINT)
    include(Lint)
    Lint(quicr_test)
endif()
