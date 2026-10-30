# SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

include_guard(GLOBAL)

function(quicr_enable_sanitizers target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "Cannot enable sanitizers for unknown target: ${target}")
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "Sanitizers require Clang or GCC")
    endif()

    target_compile_options(${target} PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -fno-sanitize-recover=undefined
    )
    target_link_options(${target} PRIVATE
        -fsanitize=address,undefined
    )
endfunction()
