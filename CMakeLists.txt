cmake_minimum_required(VERSION 3.18)

project(quicr
  VERSION 0.1
  LANGUAGES C CXX
)

# Building tests by default depends on whether this is a subproject
if (DEFINED PROJECT_NAME)
    # Option to control building tests
    option(quicr_BUILD_TESTS "Build Tests for quicr" OFF)
else()
    # Option to control building tests
    option(quicr_BUILD_TESTS "Build Tests for quicr" ON)
endif()

option(CLANG_TIDY "Perform linting with clang-tidy" OFF)

###
### Global Config
###
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(CheckCXXCompilerFlag)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  add_compile_options(-pedantic -Wmissing-declarations)
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
### Dependencies
###
# External libraries
find_package(OpenSSL 1.1 REQUIRED)


# contrib
option(quicr_GIT_SUBMODULE "Check submodules during build" OFF)
option(quicr_ADD_MODULES_RECURSIVELY "Check to add sub-modules recursively" OFF)

if (quicr_GIT_SUBMODULE)
  message("Using git submodules to bring in deps")
  if (quicr_ADD_MODULES_RECURSIVELY)
    include(cmake/AddGitSubmodule.cmake)
    add_git_submodule(${PROJECT_SOURCE_DIR}/contrib/picotls)
    add_git_submodule(${PROJECT_SOURCE_DIR}/contrib/picoquic)
    add_git_submodule(${PROJECT_SOURCE_DIR}/contrib/qproto)
   endif(quicr_ADD_MODULES_RECURSIVELY)
  
  set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
  find_package(PTLS)
  find_package(Picoquic)
  find_package(QProto REQUIRED)

  include_directories(include tests
        ${QPROTO_INCLUDE_DIRS}
        ${Picoquic_INCLUDE_DIRS} 
        ${PTLS_INCLUDE_DIRS} 
        ${OPENSSL_INCLUDE_DIR})

elseif(IS_DIRECTORY "/ws/picotls" AND IS_DIRECTORY "/ws/picoquic" AND IS_DIRECTORY "/ws/quicrq")
  message("Using /ws for picotls, picoquic, and quicrq")
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
endif(GIT_SUBMODULE)


if (OPENSSL_FOUND)
  include_directories(${OPENSSL_INCLUDE_DIR})
  set (SSL_LIBS ${OPENSSL_LIBRARIES})
else()
  set(SSL_LIBS )
endif()


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

target_link_libraries(${LIB_NAME} ${QPROTO_LIBRARIES} ${Picoquic_LIBRARIES} ${PTLS_LIBRARIES} ${SSL_LIBS})

###
### Enable testing and add tests if quicr_BUILD_TESTS is ON
###
if(quicr_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

###
### Applications
###

if(quicr_BUILD_cmd)
  add_subdirectory(cmd)
endif()
