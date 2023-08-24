add_library(quicr
            message_buffer.cpp
            encode.cpp)

target_compile_options(quicr
    PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
        $<$<CXX_COMPILER_ID:MSVC>: >)
set_target_properties(quicr
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_definitions(quicr _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(quicr PUBLIC qname)
target_include_directories(quicr PUBLIC ${PROJECT_SOURCE_DIR}/include)

