# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

set(BENCHMARK_USE_BUNDLED_GTEST OFF)
set(BENCHMARK_ENABLE_TESTING OFF)
CPMAddPackage("gh:google/benchmark@1.9.5")

add_executable(quicr_benchmark
    data_storage.cpp
    extensions.cpp
    hash.cpp
    priority_queue.cpp
    safe_time_queue.cpp
    stream_buffer.cpp
    uintvar.cpp
)

target_link_libraries(quicr_benchmark PRIVATE quicr benchmark::benchmark_main)
target_include_directories(quicr_benchmark PRIVATE ${PROJECT_SOURCE_DIR}/src)

target_compile_options(quicr_benchmark PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall -Wno-error=unused-function>
)

set_target_properties(quicr_benchmark PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS ON
)

if(LINT)
    include(Lint)
    Lint(quicr_benchmark)
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(quicr_benchmark PRIVATE -Wall -pedantic -Wextra -Werror -Wmissing-declarations)
endif()
