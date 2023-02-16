if(BUILD_TESTING AND QUICR_BUILD_TESTS)
    add_subdirectory( doctest )
endif()

if(BUILD_BENCHMARK)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF)
    set(BENCHMARK_ENABLE_TESTING OFF)
    add_subdirectory(benchmark)
endif()

add_subdirectory( transport )
