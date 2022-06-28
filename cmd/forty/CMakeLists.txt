find_package(OpenSSL 1.1 REQUIRED)

add_executable(forty forty-bytes.cpp)
target_link_libraries( forty PUBLIC quicr OpenSSL::Crypto)