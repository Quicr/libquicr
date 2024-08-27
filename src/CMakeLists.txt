
add_library(quicr)

target_sources(quicr PRIVATE
        message_buffer.cpp
        moq/messages.cpp
        moq/publish_track_handler_base.cpp
        moq/server_publish_track_handler_base.cpp
        moq/transport.cpp
        #moqt/transport.cpp
)

# Suppress external lib warnings
set_property(GLOBAL PROPERTY RULE_MESSAGES OFF)
target_compile_options(quicr-transport PRIVATE -w)

target_compile_options(quicr
    PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
        $<$<CXX_COMPILER_ID:MSVC>: >)
set_target_properties(quicr
    PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(quicr PRIVATE -Wall -pedantic -Wextra -Werror -Wmissing-declarations)
endif()

if(MSVC)
    target_compile_options(quicr PRIVATE /W4 /WX)
    target_compile_definitions(quicr _CRT_SECURE_NO_WARNINGS)
endif()

if(LINT)
    include(Lint)
    lint(quicr)
endif()

target_link_libraries(quicr
    PUBLIC
        quicr-transport)

target_include_directories(quicr PUBLIC ${PROJECT_SOURCE_DIR}/include)

