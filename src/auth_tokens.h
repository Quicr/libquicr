// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/messages/parameters.h"

#include <algorithm>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace quicr::messages {

    enum class AuthTokenError
    {
        kKeyValueFormatting,
        kCacheOverflow,
        kUnknownAlias,
        kDuplicateToken,
    };

    struct AuthTokenException : std::runtime_error
    {
        explicit AuthTokenException(AuthTokenError error)
          : std::runtime_error("Invalid AUTHORIZATION TOKEN parameter")
          , code(error)
        {
        }

        const AuthTokenError code;
    };

    inline bool AuthTokensAreUnique(const std::vector<AuthToken>& tokens)
    {
        std::vector<const AuthToken*> sorted;
        sorted.reserve(tokens.size());
        for (const auto& token : tokens) {
            sorted.push_back(&token);
        }
        const auto key = [](const AuthToken* token) { return std::tie(token->type, token->value); };
        std::ranges::sort(sorted, {}, key);
        return std::ranges::adjacent_find(sorted, {}, key) == sorted.end();
    }

    // Resolve the token values for a request when the advertised receive cache size is zero.
    inline std::vector<AuthToken> CollectAuthTokens(const Parameters& parameters)
    {
        std::vector<AuthToken> resolved;
        bool unknown_alias = false;
        bool registration = false;
        try {
            for (const auto& [type, bytes] : parameters) {
                if (type != static_cast<std::uint64_t>(ParameterType::kAuthorizationToken)) {
                    continue;
                }
                Token token;
                BytesSpan{ bytes } >> token;
                switch (token.alias_type) {
                    case Token::AliasType::kUseValue:
                        resolved.emplace_back(static_cast<AuthTokenType>(token.token_type.value()),
                                              std::move(token.token_value));
                        break;
                    case Token::AliasType::kRegister:
                        registration = true;
                        break;
                    case Token::AliasType::kUseAlias:
                    case Token::AliasType::kDelete:
                        unknown_alias = true;
                        break;
                }
            }
        } catch (const ProtocolViolationException&) {
            throw AuthTokenException{ AuthTokenError::kKeyValueFormatting };
        } catch (const UintVarInvalidArgException&) {
            throw AuthTokenException{ AuthTokenError::kKeyValueFormatting };
        }

        if (registration) {
            throw AuthTokenException{ AuthTokenError::kCacheOverflow };
        }
        if (unknown_alias) {
            throw AuthTokenException{ AuthTokenError::kUnknownAlias };
        }
        if (!AuthTokensAreUnique(resolved)) {
            throw AuthTokenException{ AuthTokenError::kDuplicateToken };
        }
        return resolved;
    }
}
