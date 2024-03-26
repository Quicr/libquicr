# ----------------------------------------------------
# Command overrides
# ----------------------------------------------------

# Override install command to add FRAMEWORK DESTINATION if missing. This is needed for framework (apple) builds
if(CMAKE_FRAMEWORK)
    macro(install)
        set (list_args "${ARGN}")
        if ("TARGETS" IN_LIST list_args AND NOT "FRAMEWORK" IN_LIST list_args)
            list(APPEND list_args "FRAMEWORK" "DESTINATION" "framework" "BUNDLE" "DESTINATION" "bundle")
            message(STATUS "Adding FRAMEWORK DESTINATION to install: ${list_args}")
            _install(${list_args})
        else ()
            message(STATUS "unchanged install: ${list_args}")
            _install(${ARGN})
        endif ()
    endmacro()
endif()

# Set OpenSSL for submodules
find_package(OpenSSL)

# --------------------------------------------------------
# Submodules
# --------------------------------------------------------
if(BUILD_TESTING AND QUICR_BUILD_TESTS)
    add_subdirectory( doctest )
endif()

add_subdirectory( transport )
add_subdirectory( qname )

if(NOT LIBQUICR_WITHOUT_INFLUXDB)
    set (_BUILD_TESTING ${BUILD_TESTING})
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    # zlib
    set (ZLIBNG_ENABLE_TESTS ${BUILD_TESTING})
    set (WITH_GTEST ${BUILD_TESTING})

    # libcurl
    set (CURL_USE_OPENSSL ON)
    set (CURL_USE_MBEDTLS OFF)
    set (CURL_USE_LIBPSL OFF)
    set (CURL_USE_LIBSSH2 OFF)
    set (CURL_USE_LIBSSH OFF)
    option (CURL_BROTLI OFF)
    set (HAVE_BORINGSSL ON)
    set (HAVE_OPENSSL_SRP OFF)
    set (HAVE_SSL_SET0_WBIO OFF)
    set (HAVE_GNUTLS_SRP OFF)
    set (HAVE_SSL_CTX_SET_QUIC_METHOD OFF)
    set (HAVE_QUICHE_CONN_SET_QLOG_FD OFF)
    option (USE_LIBIDN2 OFF)

    # CPR
    #    Fetches libcurl.
    set (CPR_LIBRARY ${CMAKE_BINARY_SOURCE_DIR}/dependencies/cpr)
    set (CPR_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/cpr)
    set (CPR_FORCE_OPENSSL_BACKEND ON)
    set (CPR_BUILD_TESTS OFF)
    set (CPR_ENABLE_CURL_HTTP_ONLY ON)

    # options below are needed for boringSSL support in libcurl
    add_compile_options(-Wno-gnu-anonymous-struct
                        -Wno-nested-anon-types)

    add_subdirectory( cpr )
    add_dependencies( cpr OpenSSL::SSL OpenSSL::Crypto)

#    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Restore original build testing if/when CPR disables it
    set(BUILD_TESTING ${_BUILD_TESTING} CACHE TRUE "" FORCE)
#    endif()

    # influxdb-cxx
    option (INFLUXCXX_TESTING ${BUILD_TESTING})
    option (INFLUXCXX_WITH_BOOST OFF)
    set (cpr_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/cpr")
    add_subdirectory( influxdb-cxx )
    add_dependencies( InfluxDB cpr)
endif()