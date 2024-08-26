
add_library(quicr)

target_sources(quicr PRIVATE
        message_buffer.cpp
        moqt/messages.cpp
        #moqt/transport.cpp
        #moqt/track_delegate_base.cpp
)

if(NOT LIBQUICR_WITHOUT_INFLUXDB)
    target_sources(quicr PRIVATE metrics_exporter.cpp)
else ()
    add_definitions("-DLIBQUICR_WITHOUT_INFLUXDB")
endif()

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

if(CLANG_TIDY_EXE)
    set_target_properties(quicr PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(quicr PRIVATE -Wall -pedantic -Wextra -Werror -Wmissing-declarations)
endif()

if(MSVC)
    target_compile_options(quicr PRIVATE /W4 /WX)
    target_compile_definitions(quicr _CRT_SECURE_NO_WARNINGS)
endif()

target_link_libraries(quicr
    PUBLIC
        quicr-transport
        qname)

if(NOT LIBQUICR_WITHOUT_INFLUXDB)
    target_link_libraries(quicr
            PRIVATE InfluxData::InfluxDB)
endif()
target_include_directories(quicr PUBLIC ${PROJECT_SOURCE_DIR}/include)

