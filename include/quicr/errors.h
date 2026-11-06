// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace quicr {

    /**
     * @brief A rejected result, holding why the request was rejected.
     *
     * @tparam E The reason code type.
     */
    template<typename E>
    struct Error
    {
        E reason_code; ///< Machine-readable reason the operation was rejected.

        std::optional<std::string> error_reason; ///< Optional human-readable explanation suitable for diagnostics.
    };

    /**
     * @brief MOQT error code, as carried by REQUEST_ERROR.
     */
    enum class ErrorCode : std::uint64_t
    {
        kInternalError = 0x0,            ///< The endpoint encountered an unspecified internal failure.
        kUnauthorized = 0x1,             ///< The requester is not authorized for the operation.
        kTimeout = 0x2,                  ///< The request could not be completed before its deadline.
        kNotSupported = 0x3,             ///< The requested operation or feature is unsupported.
        kMalformedAuthToken = 0x4,       ///< An authorization token is syntactically invalid.
        kExpiredAuthToken = 0x5,         ///< An authorization token has expired.
        kDoesNotExist = 0x10,            ///< The requested resource does not exist.
        kInvalidRange = 0x11,            ///< The requested object range is invalid.
        kMalformedTrack = 0x12,          ///< The supplied track name or properties are malformed.
        kDuplicateSubscription = 0x19,   ///< An equivalent subscription already exists.
        kUninterested = 0x20,            ///< The endpoint is no longer interested in the request.
        kPrefixOverlap = 0x30,           ///< A namespace prefix overlaps an existing request incompatibly.
        kInvalidJoiningRequestId = 0x32, ///< A joining fetch references an invalid request identifier.
    };
}
