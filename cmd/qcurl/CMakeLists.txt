project(qcurl
        VERSION ${PROJECT_VERSION}
        DESCRIPTION "Simple Quicr Client"
        LANGUAGES CXX)

add_executable(qcurl main.cpp subscriber.cpp publisher.cpp)

# Specify the libraries to link against
target_link_libraries(qcurl
    PRIVATE
        options_parser
        quicr)

# Specify the C++ standard to observe
set_target_properties(qcurl
    PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)

# Set compiler options
target_compile_options(qcurl
    PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra > #-Wall
        $<$<CXX_COMPILER_ID:MSVC>: >)
