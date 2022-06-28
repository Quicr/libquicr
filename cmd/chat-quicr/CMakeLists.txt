include_directories(./include)

add_executable(chat-quicr
    src/main.cpp
    src/ChannelMsgDelegate.cpp
    src/ChatClient.cpp
    src/Message.cpp
)
target_link_libraries( chat-quicr PUBLIC quicr OpenSSL::Crypto)
