# SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

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
endif()

# Include directories
target_include_directories(quicr_integration_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link against the quicr library and test framework
target_link_libraries(quicr_integration_test PRIVATE
    quicr
    doctest::doctest)

if(QUICR_BUILD_MSQUIC)
    target_compile_definitions(quicr_integration_test PRIVATE QUICR_HAS_MSQUIC=1)
endif()

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

set(TEST_KEY ${CMAKE_CURRENT_BINARY_DIR}/server-key.pem)
set(TEST_CERT ${CMAKE_CURRENT_BINARY_DIR}/server-cert.pem)
add_custom_command(
    OUTPUT ${TEST_KEY} ${TEST_CERT}
    COMMAND openssl req -nodes -x509 -newkey rsa:2048 -days 365
        -subj "/C=US/ST=CA/L=San Jose/O=Cisco/CN=test.m10x.org"
        -keyout ${TEST_KEY} -out ${TEST_CERT}
    COMMENT "Creating certificate in ${CMAKE_CURRENT_BINARY_DIR}"
)

add_custom_target(generate_test_certs ALL DEPENDS ${TEST_KEY} ${TEST_CERT})
add_dependencies(quicr_integration_test generate_test_certs)

# Enable test discovery with doctest - fixed-port integration tests can't run in parallel.
doctest_discover_tests(quicr_integration_test
    TEST_SPEC "Integration -*,Group ID Gap,Qlog Generation"
    TEST_SUFFIX " [picoquic]"
    TEST_LIST quicr_integration_picoquic_tests
    PROPERTIES
        RESOURCE_LOCK integration_lock
        ENVIRONMENT "LIBQUICR_TEST_BACKEND=picoquic"
        LABELS transport-picoquic
)

if(QUICR_BUILD_MSQUIC)
    doctest_discover_tests(quicr_integration_test
        TEST_SPEC "Integration -*,Group ID Gap,MsQuic -*"
        TEST_SUFFIX " [msquic]"
        TEST_LIST quicr_integration_msquic_tests
        PROPERTIES
            RESOURCE_LOCK integration_lock
            ENVIRONMENT "LIBQUICR_TEST_BACKEND=msquic"
            LABELS transport-msquic
    )
endif()

# Linting configuration
if(LINT)
    include(Lint)
    Lint(quicr_integration_test)
endif()
