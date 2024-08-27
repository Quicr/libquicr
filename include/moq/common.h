/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <string>
#include <transport/transport.h>

namespace moq {

    constexpr uint64_t kMoqtVersion = 0xff000004; ///< draft-ietf-moq-transport-04
    constexpr uint64_t kSubscribeExpires = 0;     ///< Never expires
    constexpr int kReadLoopMaxPerStream = 60; ///< Support packet/frame bursts, but do not allow starving other streams

    using namespace qtransport;

    using Bytes = std::vector<uint8_t>;
    using BytesSpan = Span<uint8_t const>;
    using ConnectionHandle = uint64_t;

    /**
     * @brief Subscribe attributes
     *
     * @details Various attributes relative to the subscribe
     */
    struct SubscribeAttributes
    {};

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

}
// namespace moq
