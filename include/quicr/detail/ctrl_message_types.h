#pragma once
#include "quicr/common.h"
#include "quicr/detail/uintvar.h"
#include "quicr/track_name.h"

namespace quicr::messages {
    quicr::Bytes& operator<<(quicr::Bytes& buffer, const quicr::Bytes& bytes);

    quicr::Bytes& operator<<(quicr::Bytes& buffer, const quicr::BytesSpan& bytes);
    quicr::BytesSpan operator>>(quicr::BytesSpan buffer, quicr::Bytes& value);

    quicr::Bytes& operator<<(quicr::Bytes& buffer, std::uint64_t value);
    quicr::BytesSpan operator>>(quicr::BytesSpan buffer, std::uint64_t& value);

    quicr::Bytes& operator<<(quicr::Bytes& buffer, std::uint8_t value);
    quicr::BytesSpan operator>>(quicr::BytesSpan buffer, uint8_t& value);

    Bytes& operator<<(Bytes& buffer, std::uint16_t value);
    BytesSpan operator>>(BytesSpan buffer, std::uint16_t& value);

    quicr::Bytes& operator<<(quicr::Bytes& buffer, const quicr::UintVar& value);

    using GroupId = uint64_t;
    using ObjectId = uint64_t;
    // TODO(RichLogan): Remove when ErrorReason -> ReasonPhrase.
    using ReasonPhrase = Bytes;

    struct ControlMessage
    {
        std::uint64_t type{ 0 };
        Bytes payload{};
    };
    Bytes& operator<<(Bytes& buffer, const ControlMessage& message);
    BytesSpan operator>>(BytesSpan buffer, ControlMessage& message);

    struct Location
    {
        GroupId group{ 0 };
        ObjectId object{ 0 };

        auto operator<=>(const Location& other) const
        {
            if (const auto cmp = group <=> other.group; cmp != 0) {
                return cmp;
            }
            return object <=> other.object;
        }

        bool operator==(const Location& other) const = default;
    };
    Bytes& operator<<(Bytes& buffer, const Location& location);
    BytesSpan operator>>(BytesSpan buffer, Location& location);

    /// MoQ Key Value Pair.
    template<typename T>
    concept KeyType =
      std::same_as<T, std::uint64_t> || (std::is_enum_v<T> && std::same_as<std::underlying_type_t<T>, std::uint64_t>);
    template<KeyType T>
    struct KeyValuePair
    {
        T type;
        Bytes value;
    };
    template<KeyType T>
    Bytes& operator<<(Bytes& buffer, const KeyValuePair<T>& param)
    {
        const auto type = static_cast<std::uint64_t>(param.type);
        buffer << UintVar(type);
        if (type % 2 == 0) {
            // Even, single varint of value.
            assert(param.value.size() <= 8);
            std::uint64_t val = 0;
            std::memcpy(&val, param.value.data(), std::min(param.value.size(), sizeof(std::uint64_t)));
            buffer << UintVar(val);
        } else {
            // Odd, encode bytes.
            buffer << UintVar(param.value.size());
            buffer.insert(buffer.end(), param.value.begin(), param.value.end());
        }
        return buffer;
    }
    template<KeyType T>
    BytesSpan operator>>(BytesSpan buffer, KeyValuePair<T>& param)
    {
        std::uint64_t type;
        buffer = buffer >> type;
        param.type = static_cast<T>(type);
        if (type % 2 == 0) {
            // Even, single varint of value.
            UintVar uvar(buffer);
            buffer = buffer.subspan(uvar.size());
            std::uint64_t val(uvar);
            param.value.resize(uvar.size());
            std::memcpy(param.value.data(), &val, uvar.size());
        } else {
            // Odd, decode bytes.
            uint64_t size = 0;
            buffer = buffer >> size;
            param.value.assign(buffer.begin(), std::next(buffer.begin(), size));
            buffer = buffer.subspan(size);
        }
        return buffer;
    }

    // Serialization for all uint64_t/enum(uint64_t to varint).
    template<KeyType T>
    Bytes& operator<<(Bytes& buffer, const T value)
    {
        buffer << UintVar(static_cast<std::uint64_t>(value));
        return buffer;
    }
    template<KeyType T>
    BytesSpan operator>>(BytesSpan buffer, T& value)
    {
        std::uint64_t uvalue;
        buffer = buffer >> uvalue;
        value = static_cast<T>(uvalue);
        return buffer;
    }

    enum struct SetupParameterType : uint64_t
    {
        kPath = 0x01,
        kMaxRequestId = 0x02, // version specific, unused
        kAuthorizationToken = 0x03,
        kEndpointId = 0xF1, // Endpoint ID, using temp value for now
        kInvalid = 0xFF,    // used internally.
    };

    enum struct ParameterType : uint64_t
    {
        kDeliveryTimeout = 0x02,
        kAuthorizationToken = 0x03,
        kMaxCacheDuration = 0x04,
        kInvalid = 0xFF, // used internally.
    };

    using Parameter = KeyValuePair<ParameterType>;
    using SetupParameter = KeyValuePair<SetupParameterType>;

    enum struct GroupOrder : uint8_t
    {
        kOriginalPublisherOrder = 0x0,
        kAscending,
        kDescending
    };

    Bytes& operator<<(Bytes& buffer, GroupOrder value);
    BytesSpan operator>>(BytesSpan buffer, GroupOrder& value);

    enum struct FilterType : uint64_t
    {
        kNone = 0x0,
        kLatestGroup,
        kLatestObject,
        kAbsoluteStart,
        kAbsoluteRange
    };

    enum class TrackStatusCode : uint64_t
    {
        kInProgress = 0x00,
        kDoesNotExist,
        kNotStarted,
        kFinished,
        kUnknown
    };

    enum class SubscribeDoneStatusCode : uint64_t
    {
        kInternalError = 0x00,
        kUnauthorized,
        kTrackEnded,
        kSubscribtionEnded,
        kGoingAway,
        kExpired,
        kTooFarBehind,
    };

    Bytes& operator<<(Bytes& buffer, SubscribeDoneStatusCode value);
    BytesSpan operator>>(BytesSpan buffer, SubscribeDoneStatusCode& value);

    enum class FetchType : uint8_t
    {
        kStandalone = 0x1,
        kJoiningFetch,
    };

    Bytes& operator<<(Bytes& buffer, FetchType value);
    BytesSpan operator>>(BytesSpan buffer, FetchType& value);

    enum class TerminationReason : uint64_t
    {
        kNoError = 0x0,
        kInternalError,
        kUnauthorized,
        kProtocolViolation,
        kDupTrackAlias,
        kParamLengthMismatch,
        kGoAwayTimeout = 0x10,
    };

    enum class FetchErrorCode : uint8_t
    {
        kInternalError = 0x0,
        kUnauthorized = 0x1,
        kTimeout = 0x2,
        kNotSupported = 0x3,
        kTrackDoesNotExist = 0x4,
        kInvalidRange = 0x5,
    };

    Bytes& operator<<(Bytes& buffer, FetchErrorCode value);
    BytesSpan operator>>(BytesSpan buffer, FetchErrorCode& value);

    enum class AnnounceErrorCode : uint64_t
    {
        kInternalError = 0x0,
        kUnauthorized,
        kTimeout,
        kNotSupported,
        kUninterested
    };

    // TODO (Suhas): rename it to StreamMapping
    enum ForwardingPreference : uint8_t
    {
        kStreamPerGroup = 0,
        kStreamPerObject,
        kStreamPerPriority,
        kStreamPerTrack,
        kDatagram
    };

    Bytes& operator<<(Bytes& buffer, ForwardingPreference value);
    BytesSpan operator>>(BytesSpan buffer, ForwardingPreference& value);

    enum class SubscribeErrorCode : uint64_t
    {
        kInternalError = 0x0,
        kUnauthorized,
        kTimeout,
        kNotSupported,
        kTrackDoesNotExist,
        kInvalidRange,
        kRetryTrackAlias,

        kTrackNotExist = 0xF0 // Missing in draft
    };

    Bytes& operator<<(Bytes& buffer, SubscribeErrorCode value);
    BytesSpan operator>>(BytesSpan buffer, SubscribeErrorCode& value);

    enum class SubscribeAnnouncesErrorCode : uint64_t
    {
        kInternalError = 0x0,
        kUnauthorized,
        kTimeout,
        kNotSupported,
        kNamespacePrefixUnknown,
    };

    BytesSpan operator>>(BytesSpan buffer, TrackNamespace& msg);
    Bytes& operator<<(Bytes& buffer, const TrackNamespace& msg);
} // namespace
