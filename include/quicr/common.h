// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/transport.h"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace quicr {

    constexpr const char* moqt_alpn = "moqt-18";
    constexpr uint64_t kSubscribeExpires = 0;  ///< Never expires
    constexpr int kReadLoopMaxPerStream = 100; ///< Support packet/frame bursts, but do not allow starving other streams

    /**
     * @brief  Publish Announce Status
     */
    enum class PublishNamespaceStatus : uint8_t
    {
        kOK = 0,
        kNotConnected,
        kNotPublished,
        kPendingResponse,
        kPublishNotAuthorized,
        kSendingDone, ///< In this state, callbacks will not be called
    };
}
// namespace quicr
