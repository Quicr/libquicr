# Test Binary

add_executable(quicr_test
                main.cpp
                quicr_client.cpp
                stream_buffer.cpp
                quicr_server.cpp
                moq_messages.cpp
                moq_instance.cpp
                encode.cpp
        moq_messages.cpp)
target_include_directories(quicr_test PRIVATE ${PROJECT_SOURCE_DIR}/src)

target_link_libraries(quicr_test PRIVATE quicr doctest::doctest)

if(NOT LIBQUICR_WITHOUT_INFLUXDB)
    target_link_libraries(quicr_test
            PRIVATE InfluxData::InfluxDB)
endif()

target_compile_options(quicr_test
    PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
        $<$<CXX_COMPILER_ID:MSVC>: >)

set_target_properties(quicr_test
    PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS ON)

if(CLANG_TIDY_EXE)
    set_target_properties(quicr_test PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(quicr_test PRIVATE  -Wall -pedantic -Wextra)
endif()

if(MSVC)
    target_compile_options(quicr_test PRIVATE /W4 /WX)
    target_compile_definitions(quicr_test _CRT_SECURE_NO_WARNINGS)
endif()

include(${CMAKE_SOURCE_DIR}/dependencies/doctest/scripts/cmake/doctest.cmake)
doctest_discover_tests(quicr_test)
