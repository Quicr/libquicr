add_library(quicr
            message_buffer.cpp
            encode.cpp
            quicr_client.cpp
            quicr_client_raw_session.cpp
            quicr_server.cpp
            quicr_server_raw_session.cpp
            h3/fragment_assembler.cpp
            h3/pub_sub_registry.cpp
            h3/h3_connection_base.cpp
            h3/quicr_client_h3_session.cpp
            h3/quicr_client_h3_connection.cpp
            h3/quicr_server_h3_session.cpp
            h3/quicr_server_h3_connection.cpp
            h3/quiche_api_lock.cpp
            h3/transport_api_lock.cpp
            h3/quiche_types.cpp)

# Suppress external lib warnings
set_property(GLOBAL PROPERTY RULE_MESSAGES OFF)
target_compile_options(picotls-core PRIVATE -w)     # TODO Should move to qtransport
target_compile_options(picotls-openssl PRIVATE -w)  # TODO Should move to qtransport
target_compile_options(picoquic-core PRIVATE -w)    # TODO Should move to qtransport
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
if(MSVC)
    target_compile_definitions(quicr _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(quicr
    PUBLIC
        quicr-transport
        qname
        cantina::logger
        cantina::networking
        cantina::threading
        quiche_library)
if(APPLE)
    # For various security functions used by Quiche + BoringSSL
    target_link_libraries(quicr
        PUBLIC
            "-framework Security"
            "-framework Foundation")
endif()

# Disable Cantina logger macros below this level
target_compile_definitions(quicr PRIVATE LOGGER_LEVEL=LOGGER_LEVEL_INFO)

target_include_directories(quicr PUBLIC ${PROJECT_SOURCE_DIR}/include)
