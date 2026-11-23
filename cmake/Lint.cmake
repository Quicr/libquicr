# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

function(lint target)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy-15 clang-tidy REQUIRED)

    set(CLANG_TIDY_COMMAND "${CLANG_TIDY_EXE}")
    set(C_CLANG_TIDY_COMMAND "${CLANG_TIDY_EXE}" "--checks=-readability-identifier-naming")
    if(MSVC)
        # clang-tidy runs in cl driver mode here, and drops the /EHsc that is
        # already on the compile command. Every throw then reads as
        # "cannot use 'throw' with exceptions disabled", so pass it again.
        list(APPEND CLANG_TIDY_COMMAND "--extra-arg=/EHsc")
    endif()

    set_target_properties(${target} PROPERTIES
        C_CLANG_TIDY "${C_CLANG_TIDY_COMMAND}"
        C_CLANG_TIDY_EXPORT_FIXES_DIR "clang-tidy-fixes"
        CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}"
        CXX_CLANG_TIDY_EXPORT_FIXES_DIR "clang-tidy-fixes"
    )
endfunction()
