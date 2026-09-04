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
        E reason_code;

        std::optional<std::string> error_reason;
    };

    /**
     * @brief MOQT error code, as carried by REQUEST_ERROR.
     */
    enum class ErrorCode : std::uint64_t
    {
        kInternalError = 0x0,
        kUnauthorized = 0x1,
        kTimeout = 0x2,
        kNotSupported = 0x3,
        kMalformedAuthToken = 0x4,
        kExpiredAuthToken = 0x5,
        kDoesNotExist = 0x10,
        kInvalidRange = 0x11,
        kMalformedTrack = 0x12,
        kDuplicateSubscription = 0x19,
        kUninterested = 0x20,
        kPrefixOverlap = 0x30,
        kInvalidJoiningRequestId = 0x32,
    };
}
