// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "test_cases.h"

#include <doctest/doctest.h>

namespace {
    void CheckResult(const quicr_c_test_result_t result)
    {
        INFO("C assertion at line " << result.line << ": " << (result.expression ? result.expression : ""));
        CHECK(result.line == 0);
    }
}

TEST_CASE("C API names")
{
    CheckResult(quicr_c_test_names());
}

TEST_CASE("C API extensions")
{
    CheckResult(quicr_c_test_extensions());
}

TEST_CASE("C API initializers")
{
    CheckResult(quicr_c_test_initializers());
}

TEST_CASE("C API responses")
{
    CheckResult(quicr_c_test_responses());
}

TEST_CASE("C API detached handlers")
{
    CheckResult(quicr_c_test_handlers());
}

TEST_CASE("C API generic handler operations")
{
    CheckResult(quicr_c_test_generic_handlers());
}
