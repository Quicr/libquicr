// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "detail/quic_transport.h"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace quicr {
    using Byte = uint8_t;
    using Bytes = std::vector<Byte>;
    using BytesSpan = std::span<const Byte>;
    using ConnectionHandle = uint64_t;
    using Extensions = std::map<uint64_t, std::vector<std::vector<uint8_t>>>;

    template<class T>
        requires std::is_standard_layout_v<T>
    inline std::span<const uint8_t> AsBytes(const T& value)
    {
        return std::span{ reinterpret_cast<const std::uint8_t*>(&value), sizeof(T) };
    }

    template<>
    inline std::span<const uint8_t> AsBytes<std::string>(const std::string& value)
    {
        return std::span{ reinterpret_cast<const std::uint8_t*>(value.data()), value.size() };
    }

    template<class T>
        requires std::is_standard_layout_v<T>
    inline Bytes AsOwnedBytes(const T& value)
    {
        auto bytes = AsBytes<T>(value);
        return Bytes{ bytes.begin(), bytes.end() };
    }

    template<>
    inline Bytes AsOwnedBytes<std::string>(const std::string& value)
    {
        auto bytes = AsBytes<std::string>(value);
        return Bytes{ bytes.begin(), bytes.end() };
    }

    template<class T>
    T FromBytes(std::span<const uint8_t> bytes)
    {
        if (bytes.empty()) {
            return {};
        }

        if constexpr (std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>) {
            return *reinterpret_cast<const T*>(bytes.data());
        } else if constexpr (std::is_same_v<std::string, std::decay_t<T>>) {
            return std::string(bytes.begin(), bytes.end());
        } else {
            T value;
            std::copy(bytes.begin(), bytes.end(), reinterpret_cast<std::uint8_t*>(&value));
            return value;
        }
    }

    constexpr uint64_t kMoqtVersion = 0xff00000E; ///< draft-ietf-moq-transport-14
    constexpr uint64_t kSubscribeExpires = 0;     ///< Never expires
    constexpr int kReadLoopMaxPerStream = 100; ///< Support packet/frame bursts, but do not allow starving other streams

    /**
     * @brief Publish namespace attributes
     *
     * @details Various attributes relative to the publish namespace
     */
    struct PublishNamespaceAttributes
    {
        uint64_t request_id{ 0 };
    };

    struct SubscribeNamespaceAttributes
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
