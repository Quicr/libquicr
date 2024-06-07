
add_executable(qserver
        server.cpp
        subscription.cpp)
target_link_libraries(qserver PRIVATE quicr)

if(NOT LIBQUICR_WITHOUT_INFLUXDB)
  target_link_libraries(qserver
          PRIVATE InfluxData::InfluxDB)
endif()


target_compile_options(qserver
        PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
        $<$<CXX_COMPILER_ID:MSVC>: >)

set_target_properties(qserver
        PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS ON)

if(CLANG_TIDY_EXE)
  set_target_properties(quicr PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()


add_executable(qclient client.cpp)

target_link_libraries(qclient PRIVATE quicr)

if(NOT LIBQUICR_WITHOUT_INFLUXDB)
  target_link_libraries(qclient
          PRIVATE InfluxData::InfluxDB)
endif()

target_compile_options(qclient
        PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
        $<$<CXX_COMPILER_ID:MSVC>: >)


set_target_properties(qclient
        PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
