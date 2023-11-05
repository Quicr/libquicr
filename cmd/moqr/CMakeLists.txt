set(APP_NAME "moqr")

# Dependencies
set(CMAKE_EXPORT_PACKAGE_REGISTRY ON)

# Test Binary
file(GLOB SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(${APP_NAME} ${SOURCES})
add_dependencies(${APP_NAME} quicr)
target_link_libraries( ${APP_NAME}
        LINK_PUBLIC
        quicr
        MLSPP::mlspp
        numero_uri_lib
        doctest::doctest)

# Make the compiler stricter
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(${APP_NAME} PRIVATE -Wall -pedantic -Wextra -Werror -Wmissing-declarations)
elseif(MSVC)
    target_compile_options(${APP_NAME} PRIVATE /W4 /WX)
endif()

# Use C++20
set_target_properties(${APP_NAME}
        PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)

# Enable CTest
include(${CMAKE_SOURCE_DIR}/dependencies/doctest/scripts/cmake/doctest.cmake)
doctest_discover_tests(${APP_NAME} ADD_LABELS 0)