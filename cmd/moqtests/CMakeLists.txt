add_executable(moq_server
        moqTestServer.cpp
        subscription.cpp
        uri_converter.cpp)
target_link_libraries(moq_server PRIVATE quicr numero_uri_lib)

target_compile_options(moq_server
        PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
        $<$<CXX_COMPILER_ID:MSVC>: >)

set_target_properties(moq_server
        PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS ON)

if(CLANG_TIDY_EXE)
  set_target_properties(quicr PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()


# --------------------------------
# Really test
# --------------------------------
add_executable( moq_client moqTestClient.cpp
        uri_converter.cpp)

target_link_libraries( moq_client LINK_PUBLIC quicr numero_uri_lib)

target_compile_options(moq_client
        PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
        $<$<CXX_COMPILER_ID:MSVC>: >)

set_target_properties(moq_client
        PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
