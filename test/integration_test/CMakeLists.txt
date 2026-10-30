# SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

if (TARGET fmt::fmt)
    set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "Use external fmt with libquicr" FORCE)
endif()

CPMAddPackage("gh:gabime/spdlog@1.17.0")

# Add the integration test executable
add_executable(quicr_integration_test
    integration_test.cpp
    test_server.h
    test_server.cpp
    test_client.h
    test_client.cpp
)

if(QUICR_ENABLE_SANITIZERS)
    target_sources(quicr_integration_test PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../sanitizer_options.cpp"
    )
    quicr_enable_sanitizers(quicr_integration_test)
endif()

# quicr links its implementation dependencies privately, so their include paths do not reach this
# target. They are needed here because tests of private headers include picoquic headers.
target_include_directories(quicr_integration_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    $<TARGET_PROPERTY:picoquic-core,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:picoquic-log,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:picohttp-core,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(quicr_integration_test PRIVATE
    quicr::quicr
    doctest::doctest
    spdlog::spdlog
    timeq::timeq
)

# Compiler options
target_compile_options(quicr_integration_test
    PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>: -Wpedantic -Wextra -Wall>
        $<$<CXX_COMPILER_ID:MSVC>: >
)

# C++ standard settings
set_target_properties(quicr_integration_test
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF
)

if (WIN32)
    target_compile_definitions(quicr_integration_test PRIVATE NOMINMAX)
endif()

set(TEST_CERT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set(TEST_KEY "${TEST_CERT_DIRECTORY}/server-key.pem")
set(TEST_CERT "${TEST_CERT_DIRECTORY}/server-cert.pem")

add_custom_target(generate_test_certs
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TEST_CERT_DIRECTORY}"
    COMMAND ${QUICR_OPENSSL_EXECUTABLE} genpkey -algorithm EC
        -pkeyopt ec_paramgen_curve:P-256 -out "${TEST_KEY}"
    COMMAND ${QUICR_OPENSSL_EXECUTABLE} req -new -x509 -sha256 -days 1
        -subj "/C=US/ST=CA/L=San Jose/O=Cisco/CN=test.m10x.org"
        -key "${TEST_KEY}" -out "${TEST_CERT}"
    COMMENT "Creating integration test certificate"
    VERBATIM
)

add_dependencies(quicr_integration_test generate_test_certs)

doctest_discover_tests(quicr_integration_test
    WORKING_DIRECTORY "${TEST_CERT_DIRECTORY}"
)

# Each test process gets its own server port.
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/ctest_integration_test_properties.cmake"
)

# Linting configuration
if(QUICR_LINT)
    include(Lint)
    lint(quicr_integration_test)
endif()
