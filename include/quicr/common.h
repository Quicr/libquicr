// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace quicr {
    using Byte = uint8_t;
    using Bytes = std::vector<Byte>;
    using BytesSpan = std::span<const Byte>;

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

        if constexpr (std::is_same_v<std::string, std::decay_t<T>>) {
            return std::string(bytes.begin(), bytes.end());
        }

        T value{};
        std::copy(bytes.begin(), bytes.end(), reinterpret_cast<std::uint8_t*>(&value));
        return value;
    }

    inline void AppendBytes(Bytes& buffer, BytesSpan bytes)
    {
        buffer.reserve(buffer.size() + bytes.size());
        buffer.insert(buffer.end(), bytes.begin(), bytes.end());
    }

    constexpr const char* moqt_alpn = "moqt-18";
    constexpr uint64_t kMoqtVersion = 0xff00010; ///< draft-ietf-moq-transport-16
    constexpr uint64_t kSubscribeExpires = 0;    ///< Never expires
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
