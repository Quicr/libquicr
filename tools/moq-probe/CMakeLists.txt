# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

add_executable(qclient client.cpp)
target_link_libraries(qclient PRIVATE
    quicr
    spdlog::spdlog
    cxxopts::cxxopts
)

target_compile_options(qclient PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
    $<$<CXX_COMPILER_ID:MSVC>:>
)

target_include_directories(qclient PRIVATE "../shared/include")

set_target_properties(qclient PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME "moq-probe"
)

if(LINT)
    include(Lint)
    Lint(qclient)
endif()
