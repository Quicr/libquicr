add_executable(forty forty-bytes.cpp)
target_link_libraries(forty PRIVATE quicr)

target_compile_options(forty
    PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
        $<$<CXX_COMPILER_ID:MSVC>: >)
set_target_properties(forty
    PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS ON)
if(MSVC)
    target_compile_definitions(forty _CRT_SECURE_NO_WARNINGS)
endif()
