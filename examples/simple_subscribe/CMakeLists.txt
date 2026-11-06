# SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

add_executable(simple_subscribe main.cpp)
set_target_properties(simple_subscribe PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)

target_link_libraries(simple_subscribe PRIVATE quicr::quicr)

add_executable(simple_c_subscribe main.c)
set_target_properties(simple_c_subscribe PROPERTIES
    C_STANDARD 11
)
target_link_libraries(simple_c_subscribe PRIVATE quicr::c)

if(QUICR_LINT)
    include(Lint)
    lint(simple_subscribe)
    lint(simple_c_subscribe)
endif()
