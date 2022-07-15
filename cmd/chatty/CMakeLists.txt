add_executable(chat-quicr
    src/main.cpp
    src/ChannelMsgDelegate.cpp
    src/ChatClient.cpp
    src/Message.cpp
)
target_include_directories(chat-quicr PRIVATE include)
target_link_libraries(chat-quicr PRIVATE quicr OpenSSL::Crypto Threads::Threads)
target_compile_options(chat-quicr PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
    $<$<CXX_COMPILER_ID:MSVC>: >)
set_target_properties(chat-quicr
    PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS ON)
if(MSVC)
  target_compile_definitions(chat-quicr _CRT_SECURE_NO_WARNINGS)
endif()
