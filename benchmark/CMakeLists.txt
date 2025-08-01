# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

find_package(benchmark)
if (NOT benchmark_FOUND)
    set(BENCHMARK_USE_BUNDLED_GTEST OFF)
    set(BENCHMARK_ENABLE_TESTING OFF)

    include(FetchContent)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG main
    )
    FetchContent_MakeAvailable(benchmark)
endif()

add_executable(quicr_benchmark
    stream_buffer.cpp
    time_queue.cpp
    uintvar.cpp
    hash.cpp
    data_storage.cpp
    tick_service.cpp
)

target_link_libraries(quicr_benchmark PRIVATE quicr benchmark::benchmark_main)
target_include_directories(quicr_benchmark PRIVATE ${PROJECT_SOURCE_DIR}/src)

target_compile_options(quicr_benchmark
    PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall -Wno-error=unused-function>
        $<$<CXX_COMPILER_ID:MSVC>: >)

set_target_properties(quicr_benchmark
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS ON)

if(LINT)
    include(Lint)
    Lint(quicr_benchmark)
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(quicr_benchmark PRIVATE -Wall -pedantic -Wextra -Werror -Wmissing-declarations)
endif()

if(MSVC)
    target_compile_options(quicr_benchmark PRIVATE /W4 /WX)
    target_compile_definitions(quicr_benchmark _CRT_SECURE_NO_WARNINGS)
endif()