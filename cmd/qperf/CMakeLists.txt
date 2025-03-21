# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

add_executable(qperf qperf.cpp)
target_link_libraries(qperf PRIVATE quicr)
target_include_directories(qperf PRIVATE ../dependencies )

target_compile_options(qperf
        PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
        $<$<CXX_COMPILER_ID:MSVC>: >)

set_target_properties(qperf
        PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)

target_compile_definitions(qperf PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

if(LINT)
        include(Lint)
        Lint(qperf)
endif()
