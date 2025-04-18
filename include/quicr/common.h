// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "detail/quic_transport.h"

#include <span>
#include <string>

namespace quicr {
    namespace messages {
        enum struct GroupOrder : uint8_t;
    }

    constexpr uint64_t kMoqtVersion = 0xff000008; ///< draft-ietf-quicr-transport-08

    constexpr uint64_t kSubscribeExpires = 0; ///< Never expires
    constexpr int kReadLoopMaxPerStream = 60; ///< Support packet/frame bursts, but do not allow starving other streams

    using namespace quicr;

    using Byte = uint8_t;
    using Bytes = std::vector<Byte>;
    using BytesSpan = std::span<const Byte>;
    using ConnectionHandle = uint64_t;

    /**
     * @brief Subscribe attributes
     *
     * @details Various attributes relative to the subscribe
     */
    struct SubscribeAttributes
    {
        uint8_t priority;                 ///< Subscriber priority
        messages::GroupOrder group_order; ///< Subscriber group order
    };

    /**
     * @brief Publish announce attributes
     *
     * @details Various attributes relative to the publish announce
     */
    struct PublishAnnounceAttributes
    {};

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

    /**
     * @brief Fetch attributes
     */
    struct FetchAttributes
    {
        uint8_t priority;                        ///< Priority
        messages::GroupOrder group_order;        ///< Group order
        uint64_t start_group;                    ///< Starting group to fetch from, inclusive.
        uint64_t start_object;                   ///< Starting object in start_group to fetch from, inclusive.
        uint64_t end_group;                      ///< Final group to fetch from, inclusive.
        std::optional<std::uint64_t> end_object; ///< Final object in end_group to fetch from, inclusive. If unset, the
                                                 ///< entire end_group has been requested.
    };
}
// namespace quicr
