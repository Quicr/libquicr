add_library(quicr
        quicr_client_old.cpp
    quicr_quic_transport.cpp)
target_compile_options(quicr
    PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
        $<$<CXX_COMPILER_ID:MSVC>: >)
set_target_properties(quicr
    PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_definitions(quicr _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(quicr
    PRIVATE
        picoquic-core
    PUBLIC
        quicrq-core
        picoquic-log)
target_include_directories(quicr PUBLIC ${PROJECT_SOURCE_DIR}/include)

