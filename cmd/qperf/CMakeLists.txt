FetchContent_Declare(cxxopts GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git GIT_TAG v3.2.1)
FetchContent_MakeAvailable(cxxopts)

if(NOT LIBQUICR_WITHOUT_INFLUXDB)
remove_definitions(REALTIME)
endif()

add_executable(qperf qperf.cpp)
target_link_libraries(qperf PRIVATE quicr cxxopts)

target_compile_options(qperf
        PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
        $<$<CXX_COMPILER_ID:MSVC>: >)

set_target_properties(qperf
        PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS ON)

add_executable(qperf_sub qperf_sub.cpp)
target_link_libraries(qperf_sub PRIVATE quicr cxxopts)

target_compile_options(qperf_sub
        PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
        $<$<CXX_COMPILER_ID:MSVC>: >)

set_target_properties(qperf_sub
        PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS ON)

configure_file(run_qperf.py run_qperf.py COPYONLY)
