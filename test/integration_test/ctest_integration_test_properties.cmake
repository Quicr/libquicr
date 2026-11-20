# SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

if(DEFINED quicr_integration_test_TESTS)
    set(port 20000)
    foreach(test_name IN LISTS quicr_integration_test_TESTS)
        # Reserve this port and the following one so tests that need two
        # simultaneous relays cannot collide with another parallel test.
        math(EXPR port "${port} + 2")
        set_tests_properties("${test_name}" PROPERTIES ENVIRONMENT "LIBQUICR_TEST_PORT=${port}")
    endforeach()
endif()

if(DEFINED quicr_c_integration_test_TESTS)
    set(port 30000)
    foreach(test_name IN LISTS quicr_c_integration_test_TESTS)
        # Reserve the base port and the following two ports used by the C relay scenarios.
        math(EXPR port "${port} + 3")
        set_tests_properties("${test_name}" PROPERTIES
            ENVIRONMENT "LIBQUICR_TEST_PORT=${port}"
            RESOURCE_LOCK "quicr_c_integration_port_${port}"
        )
    endforeach()
endif()
