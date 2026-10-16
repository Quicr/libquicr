# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

CPMAddPackage("gh:jarro2783/cxxopts@3.3.1")
CPMAddPackage("gh:nlohmann/json@3.11.3")

if(TARGET fmt::fmt)
    set(SPDLOG_FMT_EXTERNAL ON)
endif()

CPMAddPackage("gh:gabime/spdlog@1.17.0")

add_executable(qclient client.cpp)
target_link_libraries(qclient PRIVATE
    quicr
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    cxxopts::cxxopts
)

target_compile_options(qclient PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
    $<$<CXX_COMPILER_ID:MSVC>:>
)

set_target_properties(qclient PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)

if(LINT)
    include(Lint)
    Lint(qclient)
endif()
