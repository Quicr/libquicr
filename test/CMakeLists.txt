set(TEST_APP_NAME "${LIB_NAME}_test")

# Test Binary
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(${TEST_APP_NAME} ${TEST_SOURCES})
target_include_directories(${TEST_APP_NAME} PRIVATE ${doctest_SOURCE_DIR})
add_dependencies(${TEST_APP_NAME} ${LIB_NAME})
target_link_libraries(${TEST_APP_NAME}
  PRIVATE
    ${LIB_NAME} doctest::doctest OpenSSL::Crypto Threads::Threads)
target_compile_options(${TEST_APP_NAME} PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
    $<$<CXX_COMPILER_ID:MSVC>: >)
set_target_properties(${TEST_APP_NAME}
    PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS ON)
if(MSVC)
  target_compile_definitions(${TEST_APP_NAME} _CRT_SECURE_NO_WARNINGS)
endif()

# Enable doctest
doctest_discover_tests(${TEST_APP_NAME} ADD_LABELS 0)

add_test(NAME ${TEST_APP_NAME}
         COMMAND ${TEST_APP_NAME})
