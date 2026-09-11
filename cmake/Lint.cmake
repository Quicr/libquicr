function(lint target)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy-15 clang-tidy REQUIRED)

    set(CLANG_TIDY_COMMAND "${CLANG_TIDY_EXE}")
    if(MSVC)
        # clang-tidy runs in cl driver mode here, and drops the /EHsc that is
        # already on the compile command. Every throw then reads as
        # "cannot use 'throw' with exceptions disabled", so pass it again.
        list(APPEND CLANG_TIDY_COMMAND "--extra-arg=/EHsc")
    endif()

    set_target_properties(${target}
        PROPERTIES
            CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND}"
            CXX_CLANG_TIDY_EXPORT_FIXES_DIR "clang-tidy-fixes")
endfunction()
