# SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

add_executable(quicr_c_link_test api_surface.c)
target_link_libraries(quicr_c_link_test PRIVATE quicr::c)
set_target_properties(quicr_c_link_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF
)

add_executable(quicr_c_test
    tests.cpp
    test_cases.c
)
target_link_libraries(quicr_c_test PRIVATE
    doctest::doctest_with_main
    quicr::c
)
set_target_properties(quicr_c_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
)

if(QUICR_ENABLE_SANITIZERS)
    target_sources(quicr_c_test PRIVATE ../sanitizer_options.cpp)
endif()

foreach(target IN ITEMS quicr_c_link_test quicr_c_test)
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANG_AND_ID:C,Clang,AppleClang,GNU>:-Wall;-Wextra;-Wpedantic>
        $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>:-Wall;-Wextra;-Wpedantic>
        $<$<AND:$<COMPILE_LANGUAGE:C>,$<C_COMPILER_ID:MSVC>>:/W4>
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/W4>
    )
endforeach()

add_test(NAME C-API-Link-Surface COMMAND quicr_c_link_test)
doctest_discover_tests(quicr_c_test)

if(QUICR_LINT)
    include(Lint)
    lint(quicr_c_link_test)
    lint(quicr_c_test)
endif()
