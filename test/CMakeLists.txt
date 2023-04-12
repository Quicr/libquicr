# Test Binary

add_executable(quicr_test
                main.cpp
                name.cpp
                namespace.cpp
                quicr_client.cpp
                quicr_server.cpp
                encode.cpp
                hex_endec.cpp)
target_include_directories(quicr_test PRIVATE ${PROJECT_SOURCE_DIR}/src)

target_link_libraries(quicr_test PRIVATE quicr doctest::doctest)

target_compile_options(quicr_test
    PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
        $<$<CXX_COMPILER_ID:MSVC>: >)

set_target_properties(quicr_test
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS ON)
if(MSVC)
    target_compile_definitions(quicr_test _CRT_SECURE_NO_WARNINGS)
endif()

include(${CMAKE_SOURCE_DIR}/dependencies/doctest/scripts/cmake/doctest.cmake)
doctest_discover_tests(quicr_test)
