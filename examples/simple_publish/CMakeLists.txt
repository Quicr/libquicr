# SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

add_executable(simple_publish main.cpp)
set_target_properties(simple_publish PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)

target_link_libraries(simple_publish PRIVATE quicr)

if(LINT)
    include(Lint)
    Lint(simple_publish)
endif()
