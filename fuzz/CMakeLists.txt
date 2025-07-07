# SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    option(QUICR_BUILD_FUZZ "Build fuzzer targets" OFF)
    if(QUICR_BUILD_FUZZ)
        # Add fuzzing flags
        set(FUZZER_FLAGS "-fsanitize=fuzzer,address,undefined" "-fno-optimize-sibling-calls" "-fno-omit-frame-pointer" "-g" "-O1")
        add_executable(ctrl_messages_fuzzer ctrl_messages_fuzzer.cpp)
        target_link_libraries(ctrl_messages_fuzzer PRIVATE quicr)
        target_compile_options(ctrl_messages_fuzzer PRIVATE ${FUZZER_FLAGS})
        target_link_options(ctrl_messages_fuzzer PRIVATE ${FUZZER_FLAGS})
        set_target_properties(ctrl_messages_fuzzer PROPERTIES
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED ON
        )
        if(LINT)
            include(Lint)
            lint(quicr)
        endif()
    endif()
else()
    message(STATUS "Fuzzing requires Clang -- skipping")
endif()
