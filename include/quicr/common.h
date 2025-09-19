// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "detail/quic_transport.h"

#include <span>
#include <string>

namespace quicr {

    constexpr uint64_t kMoqtVersion = 0xff00000E; ///< draft-ietf-moq-transport-14
    constexpr uint64_t kSubscribeExpires = 0;     ///< Never expires
    constexpr int kReadLoopMaxPerStream = 60; ///< Support packet/frame bursts, but do not allow starving other streams

    using namespace quicr;

    using Byte = uint8_t;
    using Bytes = std::vector<Byte>;
    using BytesSpan = std::span<const Byte>;
    using ConnectionHandle = uint64_t;
    /**
     * @brief Publish announce attributes
     *
     * @details Various attributes relative to the publish announce
     */
    struct PublishAnnounceAttributes
    {
        uint64_t request_id{ 0 };
    };

    /**
     * @brief Client Setup Attributes
     */
    struct ClientSetupAttributes
    {
        const std::string endpoint_id;
    };

    /**
     * @brief Server Setup Attributes
     */
    struct ServerSetupAttributes
    {
        const uint64_t moqt_version;
        const std::string server_id;
    };

    /**
     * @brief  Publish Announce Status
     */
    enum class PublishAnnounceStatus : uint8_t
    {
        kOK = 0,
        kNotConnected,
        kNotAnnounced,
        kPendingAnnounceResponse,
        kAnnounceNotAuthorized,
        kSendingUnannounce, ///< In this state, callbacks will not be called
    };
}
// namespace quicr
