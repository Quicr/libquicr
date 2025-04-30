#pragma once
#include "quicr/common.h"
#include "quicr/detail/uintvar.h"

namespace quicr::messages {
    quicr::Bytes& operator<<(quicr::Bytes& buffer, const quicr::Bytes& bytes);

    quicr::Bytes& operator<<(quicr::Bytes& buffer, const quicr::BytesSpan& bytes);
    quicr::BytesSpan operator>>(quicr::BytesSpan buffer, quicr::Bytes& value);

    quicr::Bytes& operator<<(quicr::Bytes& buffer, std::uint64_t value);
    quicr::BytesSpan operator>>(quicr::BytesSpan buffer, std::uint64_t& value);

    quicr::Bytes& operator<<(quicr::Bytes& buffer, std::uint8_t value);
    quicr::BytesSpan operator>>(quicr::BytesSpan buffer, uint8_t& value);

    quicr::Bytes& operator<<(quicr::Bytes& buffer, const quicr::UintVar& value);

    using GroupId = uint64_t;
    using ObjectId = uint64_t;

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
            std::uint64_t val;
            buffer = buffer >> val;
            param.value.resize(sizeof(std::uint64_t));
            std::memcpy(param.value.data(), &val, sizeof(std::uint64_t));
        } else {
            // Odd, decode bytes.
            uint64_t size = 0;
            buffer = buffer >> size;
            param.value.assign(buffer.begin(), std::next(buffer.begin(), size));
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

    enum struct ParameterType : uint64_t
    {
        kPath = 0x1,
        kMaxSubscribeId = 0x2, // version specific, unused
        kEndpointId = 0xF0,    // Endpoint ID, using temp value for now
        kInvalid = 0xFF,       // used internally.
    };

    using Parameter = KeyValuePair<ParameterType>;

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

    enum class SubscribeAnnouncesErrorCode : uint64_t
    {
        kInternalError = 0x0,
        kUnauthorized,
        kTimeout,
        kNotSupported,
        kNamespacePrefixUnknown,
    };
} // namespace
