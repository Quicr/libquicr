add_library(quicr
            message_buffer.cpp
            encode.cpp
            quicr_client.cpp
            quicr_client_raw_session.cpp
            quicr_server.cpp
            quicr_server_raw_session.cpp)

# Suppress external lib warnings
set_property(GLOBAL PROPERTY RULE_MESSAGES OFF)
target_compile_options(picotls-core PRIVATE -w)
target_compile_options(picotls-openssl PRIVATE -w)
target_compile_options(picoquic-core PRIVATE -w)
target_compile_options(quicr-transport PRIVATE -w)

target_compile_options(quicr
    PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
        $<$<CXX_COMPILER_ID:MSVC>: >)
set_target_properties(quicr
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)

if(CLANG_TIDY_EXE)
  set_target_properties(quicr PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

if(MSVC)
    target_compile_definitions(quicr _CRT_SECURE_NO_WARNINGS)
endif()

target_link_libraries(quicr
    PUBLIC
        quicr-transport
        qname)
target_include_directories(quicr PUBLIC ${PROJECT_SOURCE_DIR}/include)

