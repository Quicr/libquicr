function(lint target)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    find_program(CLANG_TIDY_EXE clang-tidy REQUIRED)
    set_target_properties(${target}
        PROPERTIES
            CXX_CLANG_TIDY "${CLANG_TIDY_EXE}"
            CXX_CLANG_TIDY_EXPORT_FIXES_DIR "clang-tidy-fixes")
endfunction()
