# SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

CPMAddPackage("gh:nlohmann/json@3.11.3")

add_executable(moq_capture main.cpp)
set_target_properties(moq_capture PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)

target_link_libraries(moq_capture PRIVATE
    quicr
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    cxxopts::cxxopts
 )

target_include_directories(moq_capture PRIVATE "../shared/include")

if(LINT)
    include(Lint)
    Lint(moq_capture)
endif()
