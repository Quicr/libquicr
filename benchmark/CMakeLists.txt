add_executable(quicr_benchmark
                main.cpp
                name.cpp
                message_buffer.cpp
                hex_endec.cpp)

target_link_libraries(quicr_benchmark PRIVATE quicr benchmark::benchmark)
target_include_directories(quicr_benchmark PRIVATE ${PROJECT_SOURCE_DIR}/src)

target_compile_options(quicr_benchmark
    PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
        $<$<CXX_COMPILER_ID:MSVC>: >)

set_target_properties(quicr_benchmark
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS ON)
if(MSVC)
    target_compile_definitions(quicr_benchmark _CRT_SECURE_NO_WARNINGS)
endif()
