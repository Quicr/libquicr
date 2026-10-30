# SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

add_executable(simple_subscribe main.cpp)
set_target_properties(simple_subscribe PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)

target_link_libraries(simple_subscribe PRIVATE quicr::quicr)

if(QUICR_LINT)
    include(Lint)
    lint(simple_subscribe)
endif()
