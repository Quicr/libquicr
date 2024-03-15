if(BUILD_TESTING AND QUICR_BUILD_TESTS)
    add_subdirectory( doctest )
endif()

add_subdirectory( transport )
add_subdirectory( qname )

if(NOT LIBQUICR_WITHOUT_INFLUXDB)
    set (CPR_FORCE_MBEDTLS_BACKEND ON)
    add_subdirectory( cpr )

    option (INFLUXCXX_TESTING OFF)
    option (INFLUXCXX_WITH_BOOST OFF)
    set (cpr_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/cpr")
    add_subdirectory( influxdb-cxx )
endif()