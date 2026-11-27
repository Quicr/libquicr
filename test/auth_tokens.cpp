// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "auth_tokens.h"

#include <doctest/doctest.h>

#include <initializer_list>

using namespace quicr;
using namespace quicr::messages;

namespace {
    Parameters AuthParameters(std::initializer_list<Bytes> bodies)
    {
        Parameters params;
        for (const auto& body : bodies) {
            params.Add(ParameterType::kAuthorizationToken, body);
        }
        return params;
    }

    void CheckAuthTokenError(const Parameters& parameters, AuthTokenError expected)
    {
        try {
            CollectAuthTokens(parameters);
            FAIL("Expected CollectAuthTokens to throw AuthTokenException");
        } catch (const AuthTokenException& e) {
            CHECK(e.code == expected);
        }
    }
}

TEST_CASE("AUTHORIZATION TOKEN resolves direct values in parameter order")
{
    const auto result = CollectAuthTokens(AuthParameters({ { 0x03, 0x04, 0xAB }, { 0x03, 0x02, 0xAB } }));
    CHECK(result == std::vector<AuthToken>{ { static_cast<AuthTokenType>(4), { 0xAB } },
                                            { static_cast<AuthTokenType>(2), { 0xAB } } });
}

TEST_CASE("AUTHORIZATION TOKEN rejects duplicate direct values")
{
    CheckAuthTokenError(AuthParameters({ { 0x03, 0x02, 0xAB }, { 0x03, 0x02, 0xAB } }),
                        AuthTokenError::kDuplicateToken);
}

TEST_CASE("AUTHORIZATION TOKEN handles aliases with a zero cache budget")
{
    CheckAuthTokenError(AuthParameters({ { 0x01, 0x07, 0x02 } }), AuthTokenError::kCacheOverflow);
    CheckAuthTokenError(AuthParameters({ { 0x02, 0x07 } }), AuthTokenError::kUnknownAlias);
}

TEST_CASE("AUTHORIZATION TOKEN requires a complete wire token")
{
    for (const auto& body : std::initializer_list<Bytes>{
           {}, { 0x04 }, { 0x01, 0x07 }, { 0x03, 0x80 }, { 0x02, 0x07, 0xAB }, { 0x00, 0x07, 0xAB } }) {
        CheckAuthTokenError(AuthParameters({ body }), AuthTokenError::kKeyValueFormatting);
    }

    CheckAuthTokenError(AuthParameters({ { 0x02, 0x07 }, { 0x01, 0x07, 0x02 }, { 0x04 } }),
                        AuthTokenError::kKeyValueFormatting);
}
