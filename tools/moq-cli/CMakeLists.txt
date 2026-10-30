# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

add_executable(moq_cli client.cpp)
target_link_libraries(moq_cli PRIVATE
    quicr::quicr
    spdlog::spdlog
    cxxopts::cxxopts
)

target_compile_options(moq_cli PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
    $<$<CXX_COMPILER_ID:MSVC>:>
)

target_include_directories(moq_cli PRIVATE "../shared/include")

set_target_properties(moq_cli PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME "moq-cli"
)

if(QUICR_LINT)
    include(Lint)
    lint(moq_cli)
endif()
