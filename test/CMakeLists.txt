# Test Binary
add_executable(quicr_test
                main.cpp
                quicr_client.cpp
                encode.cpp)
target_include_directories(quicr_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(quicr_test PRIVATE quicr doctest::doctest)
target_compile_options(quicr_test
    PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
        $<$<CXX_COMPILER_ID:MSVC>: >)
set_target_properties(quicr_test
    PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS ON)
if(MSVC)
    target_compile_definitions(quicr_test _CRT_SECURE_NO_WARNINGS)
endif()

# Enable doctest
doctest_discover_tests(quicr_test ADD_LABELS 0)

add_test(NAME quicr_test
         COMMAND quicr_test)
