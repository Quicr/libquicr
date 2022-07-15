cmake_minimum_required(VERSION 3.17)

# Building tests by default depends on whether this is a subproject
if (DEFINED PROJECT_NAME)
    option(quicr_BUILD_TESTS "Build Tests for quicr" OFF)
else()
    option(quicr_BUILD_TESTS "Build Tests for quicr" ON)
endif()

project(quicr
        VERSION 1.0.0.0
        DESCRIPTION "quicr library"
        LANGUAGES CXX)

option(CLANG_TIDY "Perform linting with clang-tidy" OFF)

###
### Global Config
###
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(CheckCXXCompilerFlag)

if(CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
  if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY  ${CLANG_TIDY_EXE})
  else()
    message(WARNING "clang-tidy requested, but not found")
  endif()
endif()

###
### Dependencies
###
# External libraries
find_package(OpenSSL 1.1 REQUIRED)
find_package(Threads REQUIRED)


if(IS_DIRECTORY "/ws/picotls" AND IS_DIRECTORY "/ws/picoquic" AND IS_DIRECTORY "/ws/quicrq")

  message(STATUS "Using /ws for picotls, picoquic, and quicrq")
  set(Picotls_INCLUDE_DIR  /ws/picotls/include)
  set(Picoquic_INCLUDE_DIR /ws/picoquic/picoquic)
  set(Picoquic_loglib_INCLUDE_DIR /ws/picoquic/loglib)
  set(QuicrProto_INCLUDE_DIR /ws/quicrq/include/)

  set(Picotls_LIB_DIR /ws/picotls/)
  set(Picoquic_LIB_DIR /ws/picoquic/)
  set(QuicrProto_LIB_DIR /ws/quicrq/)

  set(PTLS_LIBRARIES picotls-core picotls-openssl)
  set(Picoquic_LIBRARIES picoquic-core picoquic-log)
  set(QPROTO_LIBRARIES quicrq-core)

else()

  message(STATUS "Fetching required libraries")

  # Fetch the content
  add_subdirectory(dependencies)

endif()


###
### Library Config
###

set(LIB_NAME "${PROJECT_NAME}")

file(GLOB_RECURSE LIB_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(${LIB_NAME} ${ILB_HEADERS} ${LIB_SOURCES})
target_compile_options(${LIB_NAME} PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
    $<$<CXX_COMPILER_ID:MSVC>: >)
set_target_properties(${LIB_NAME}
    PROPERTIES
      CXX_STANDARD 17
      CXX_STANDARD_REQUIRED YES
      CXX_EXTENSIONS OFF)
if(MSVC)
  target_compile_definitions(${LIB_NAME} _CRT_SECURE_NO_WARNINGS)
endif()
target_link_directories(${LIB_NAME}
  PRIVATE
    ${Picotls_LIB_DIR} ${Picoquic_LIB_DIR}
  PUBLIC
    ${QuicrProto_LIB_DIR})
target_link_libraries(${LIB_NAME} quicrq-core picoquic-core picoquic-log OpenSSL::Crypto)
target_include_directories(${LIB_NAME}
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/${PROJECT_NAME}-${PROJECT_VERSION}>
    ${Picotls_INCLUDE_DIR}
    ${Picoquic_INCLUDE_DIR}
    ${QuicrProto_INCLUDE_DIR}
    ${Picoquic_loglib_INCLUDE_DIR}
  PRIVATE
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(${LIB_NAME} ${QPROTO_LIBRARIES} ${Picoquic_LIBRARIES} ${PTLS_LIBRARIES} ${OPENSSL_LIBRARIES})

###
### Enable testing and add tests if quicr_BUILD_TESTS is ON
###
include(CTest)
if(BUILD_TESTING AND quicr_BUILD_TESTS)
  find_package(doctest QUIET)
  if(doctest_FOUND)
    # Include for doctest_discover_tests()
    include(doctest)
  else()
    # Include the CMake file for doctest_discover_tests() if content fetched
    include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
  endif()
  add_subdirectory(test)
endif()

###
### Applications
###
add_subdirectory(cmd)
