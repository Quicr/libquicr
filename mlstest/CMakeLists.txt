
find_package(OpenSSL 1.1 REQUIRED)

set(CMAKE_EXPORT_PACKAGE_REGISTRY ON)
find_package(MLSPP REQUIRED)

add_executable( quicr_mlstest main.cpp MlsUserSession.cpp testLogger.cpp)

target_link_libraries( quicr_mlstest LINK_PUBLIC
                            quicr picotls-core picotls-openssl MLSPP::mlspp  MLSPP::tls_syntax)

target_compile_options(quicr_mlstest
        PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
        $<$<CXX_COMPILER_ID:MSVC>: >)

set_target_properties(quicr_mlstest
        PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)