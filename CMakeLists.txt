cmake_minimum_required(VERSION 3.12)

project(quicr
  VERSION 0.1
  LANGUAGES CXX
)

option(CLANG_TIDY "Perform linting with clang-tidy" OFF)

###
### Global Config
###
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(CheckCXXCompilerFlag)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  add_compile_options(-Wall -pedantic -Wextra -Wmissing-declarations)
elseif(MSVC)
  add_compile_options(/W4 /WX)

  # MSVC helpfully recommends safer equivalents for things like
  # getenv, but they are not portable.
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)  
endif()

if(CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
  if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY  ${CLANG_TIDY_EXE})
  else()
    message(WARNING "clang-tidy requested, but not found")
  endif()
endif()

###
### Enable testing
###
enable_testing()

###
### Dependencies
###
# External libraries
find_package(OpenSSL 1.1 REQUIRED)

# contrib

if (IS_DIRECTORY "/ws/picotls" AND IS_DIRECTORY "/ws/picoquic" AND IS_DIRECTORY "/ws/quicrq")
  message("Using /ws for picotls, picoquic, and quicrq")
  set(Picotls_INCLUDE_DIR  /ws/picotls/include)
  set(Picoquic_INCLUDE_DIR /ws/picoquic/picoquic)
  set(Picoquic_loglib_INCLUDE_DIR /ws/picoquic/loglib)
  set(QuicrProto_INCLUDE_DIR /ws/quicrq/include/)

  set(Picotls_LIB_DIR /ws/picotls/)
  set(Picoquic_LIB_DIR /ws/picoquic/)
  set(QuicrProto_LIB_DIR /ws/quicrq/)

elseif (APPLE)
  message("Updating for Apple device")
  execute_process(
          COMMAND uname -m
          RESULT_VARIABLE result
          OUTPUT_VARIABLE OSX_NATIVE_ARCHITECTURE
          OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  set(Picotls_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/contrib/picotls/include)
  set(Picoquic_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/contrib/picoquic/include)
  set(QuicrProto_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/contrib/qproto/include)

  if(OSX_NATIVE_ARCHITECTURE STREQUAL "arm64")
    set(Picotls_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/contrib/picotls/lib/arm64)
    set(Picoquic_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/contrib/picoquic/lib/arm64)
    set(QuicrProto_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/contrib/qproto/lib/arm64)

  else()
    set(Picotls_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/contrib/picotls/lib/x86_64)
    set(Picoquic_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/contrib/picoquic/lib/x86_64)
    set(QuicrProto_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/contrib/qproto/lib/x86_64)
  endif()

endif(APPLE)

find_package( OpenSSL 1)
if (OPENSSL_FOUND)
  include_directories(${OPENSSL_INCLUDE_DIR})
  set (SSL_LIBS ${OPENSSL_LIBRARIES})
else()
  set(SSL_LIBS )
endif()

#set(PTLS_LIBRARIES picotls-core picotls-openssl picotls-fusion)
set(PTLS_LIBRARIES picotls-core picotls-openssl)
set(Picoquic_LIBRARIES picoquic-core picoquic-log)
set(QuicrProto_LIBRARIES quicrq-core)

###
### Library Config
###

set(LIB_NAME "${PROJECT_NAME}")

file(GLOB_RECURSE LIB_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(${LIB_NAME} ${ILB_HEADERS} ${LIB_SOURCES})
target_link_directories(${LIB_NAME} PUBLIC ${Picotls_LIB_DIR} ${Picoquic_LIB_DIR} ${QuicrProto_LIB_DIR})
target_link_libraries(${LIB_NAME} OpenSSL::Crypto)
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

target_link_libraries(${LIB_NAME} ${QuicrProto_LIBRARIES} ${Picoquic_LIBRARIES} ${PTLS_LIBRARIES} ${SSL_LIBS})

###
### Tests
###
add_subdirectory(test)

###
### Applications
###

add_subdirectory(cmd/forty)
add_subdirectory(cmd/chat-quicr)
