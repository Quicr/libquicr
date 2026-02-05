#pragma once
#include "quicr/common.h"
#include "quicr/detail/uintvar.h"
#include "quicr/track_name.h"
#include <stdexcept>

namespace quicr::messages {
    Bytes& operator<<(Bytes& buffer, const Bytes& bytes);
    Bytes& operator<<(Bytes& buffer, const BytesSpan& bytes);
    BytesSpan operator>>(BytesSpan buffer, Bytes& value);

    Bytes& operator<<(Bytes& buffer, std::uint64_t value);
    BytesSpan operator>>(BytesSpan buffer, std::uint64_t& value);

    Bytes& operator<<(Bytes& buffer, std::uint8_t value);
    BytesSpan operator>>(BytesSpan buffer, uint8_t& value);

    Bytes& operator<<(Bytes& buffer, std::uint16_t value);
    BytesSpan operator>>(BytesSpan buffer, std::uint16_t& value);

    Bytes& operator<<(Bytes& buffer, const UintVar& value);

    using GroupId = uint64_t;
    using ObjectId = uint64_t;
    // TODO(RichLogan): Remove when ErrorReason -> ReasonPhrase.
    using ReasonPhrase = Bytes;
    using RequestID = uint64_t;
    using TrackName = Bytes;

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

        /**
         * Get the encoded size of this KeyValuePair, in bytes.
         * @return Encoded size, in bytes.
         */
        std::size_t Size() const
        {
            std::size_t size = 0;
            const auto type_val = static_cast<std::uint64_t>(type);
            size += UintVar(type_val).size();

            if (type_val % 2 == 0) {
                // Even types: single varint of value
                if (value.size() > sizeof(std::uint64_t)) {
                    throw std::invalid_argument("Value too large to encode as uint64_t.");
                }
                std::uint64_t val = 0;
                std::memcpy(&val, value.data(), value.size());
                size += UintVar(val).size();
            } else {
                // Odd types: length + bytes
                size += UintVar(value.size()).size();
                size += value.size();
            }
            return size;
        }

        /**
         * Equality comparison operator for KeyValuePair.
         * @param other The KeyValuePair to compare with.
         * @return True if both KeyValuePair objects are equal, false otherwise.
         */
        bool operator==(const KeyValuePair<T>& other) const
        {
            if (type != other.type) {
                return false;
            }

            if (static_cast<std::uint64_t>(type) % 2 != 0) {
                // Odd types are byte equality.
                return value == other.value;
            }

            // Even types are numeric equality.
            if (value.size() > sizeof(std::uint64_t) || other.value.size() > sizeof(std::uint64_t)) {
                throw std::invalid_argument("Even KVPs must be <= 8 bytes");
            }

            // Compare numeric values.
            const auto smaller = std::min(value.size(), other.value.size());
            if (memcmp(value.data(), other.value.data(), smaller) != 0) {
                return false;
            }

            // Are there left over bytes to check?
            const auto larger = std::max(value.size(), other.value.size());
            if (larger == smaller) {
                return true;
            }

            // Any remaining bytes could be 0, but nothing else.
            const auto& longer = (value.size() > other.value.size()) ? value : other.value;
            const auto remaining = larger - smaller;
            static constexpr std::uint8_t kZero[sizeof(std::uint64_t)] = { 0 };
            return memcmp(longer.data() + smaller, kZero, remaining) == 0;
        }
    };

    // Serialization for all uint64_t/enum(uint64_t to varint).
    template<KeyType T>
    Bytes& operator<<(Bytes& buffer, const T value)
    {
        return buffer << UintVar(static_cast<std::uint64_t>(value));
    }
    template<KeyType T>
    BytesSpan operator>>(BytesSpan buffer, T& value)
    {
        std::uint64_t uvalue;
        buffer = buffer >> uvalue;
        value = static_cast<T>(uvalue);
        return buffer;
    }

    template<KeyType T>
    Bytes& operator<<(Bytes& buffer, const KeyValuePair<T>& param)
    {
        buffer << param.type;
        if (static_cast<std::uint64_t>(param.type) % 2 != 0) {
            // Odd, encode bytes.
            return buffer << param.value;
        }

        // Even, single varint of value.
        if (param.value.size() > sizeof(std::uint64_t)) {
            throw std::invalid_argument("Value too large to encode as uint64_t.");
        }
        std::uint64_t val = 0;
        std::memcpy(&val, param.value.data(), param.value.size());
        return buffer << UintVar(val);
    }

    template<KeyType T>
    BytesSpan operator>>(BytesSpan buffer, KeyValuePair<T>& param)
    {
        buffer = buffer >> param.type;
        if (static_cast<std::uint64_t>(param.type) % 2 != 0) {
            // Odd, decode bytes.
            return buffer >> param.value;
        }

        // Even, decode single varint of value.
        UintVar uvar(buffer);
        buffer = buffer.subspan(uvar.size());
        std::uint64_t val(uvar);
        param.value.resize(uvar.size());
        std::memcpy(param.value.data(), &val, uvar.size());
        return buffer;
    }

    enum struct SetupParameterType : uint64_t
    {
        kPath = 0x01,
        kMaxRequestId = 0x02,
        kAuthorizationToken = 0x03,
        kMaxAuthTokenCacheSize = 0x04,
        kAuthority = 0x05,
        kMoqtImplementation = 0x07,

        /*===================================================================*/
        // Internal Use
        /*===================================================================*/

        kEndpointId = 0xF1,
        kInvalid = 0xFF,
    };

    enum struct ParameterType : uint64_t
    {
        kDeliveryTimeout = 0x02,
        kAuthorizationToken = 0x03,
        kExpires = 0x08,
        kLargestObject = 0x09,
        kForward = 0x10,
        kSubscriberPriority = 0x20,
        kSubscriptionFilter = 0x21,
        kGroupOrder = 0x22,
        kNewGroupRequest = 0x32,

        /*===================================================================*/
        // Internal Use
        /*===================================================================*/

        kInvalid = 0xFF,
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
        kLargestObject = 0x2,
        kNextGroupStart = 0x1,
        kAbsoluteStart = 0x3,
        kAbsoluteRange = 0x4
    };

    enum class PublishDoneStatusCode : uint64_t
    {
        kInternalError = 0x00,
        kUnauthorized,
        kTrackEnded,
        kSubscribtionEnded,
        kGoingAway,
        kExpired,
        kTooFarBehind,
    };

    Bytes& operator<<(Bytes& buffer, PublishDoneStatusCode value);
    BytesSpan operator>>(BytesSpan buffer, PublishDoneStatusCode& value);

    enum class FetchType : uint8_t
    {
        kStandalone = 0x1,
        kRelativeJoiningFetch,
        kAbsoluteJoiningFetch
    };

    Bytes& operator<<(Bytes& buffer, FetchType value);
    BytesSpan operator>>(BytesSpan buffer, FetchType& value);

    struct StandaloneFetch
    {
        TrackNamespace track_namespace;
        TrackName track_name;
        Location start;
        Location end;
    };

    Bytes& operator<<(Bytes& buffer, StandaloneFetch value);
    BytesSpan operator>>(BytesSpan buffer, StandaloneFetch& value);

    struct JoiningFetch
    {
        RequestID request_id;
        uint64_t joining_start;
    };

    Bytes& operator<<(Bytes& buffer, JoiningFetch value);
    BytesSpan operator>>(BytesSpan buffer, JoiningFetch& value);

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

    enum class TerminationReason : uint64_t
    {
        kNoError = 0x0,
        kInternalError = 0x1,
        kUnauthorized = 0x2,
        kProtocolViolation = 0x3,
        kInvalidRequestId = 0x4,
        kDuplicateTrackAlias = 0x5,
        kKeyValueFormattingError = 0x6,
        kTooManyRequests = 0x7,
        kInvalidPath = 0x8,
        kMalformedPath = 0x9,
        kGoawayTimeout = 0x10,
        kControlMessageTimeout = 0x11,
        kDataStreamTimeout = 0x12,
        kAuthTokenCacheOverflow = 0x13,
        kDuplicateAuthTokenAlias = 0x14,
        kVersionNegotiationFailed = 0x15,
        kMalformedAuthToken = 0x16,
        kUnknownAuthTokenAlias = 0x17,
        kExpiredAuthToken = 0x18,
        kInvalidAuthority = 0x19,
        kMalformedAuthority = 0x1A,
    };

    enum class ErrorCode : uint64_t
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

    enum class PublishDoneStatus : uint64_t
    {
        kInternalError = 0x0,
        kUnauthorized = 0x1,
        kTrackEnded = 0x2,
        kSubscriptionEnded = 0x3,
        kGoingAway = 0x4,
        kExpired = 0x5,
        kTooFarBehind = 0x6,
        kUpdateFailed = 0x8,
        kMalformedTrack = 0x12,
    };

    enum class StreamResetError : uint64_t
    {
        kInternalError = 0x0,
        kCancelled = 0x1,
        kDeliveryTimeout = 0x2,
        kSessionClosed = 0x3,
        kUnknownObjectStatus = 0x4,
        kMalformedTrack = 0x12,
    };

    enum class SubscribeOptions : uint64_t
    {
        kPublish = 0x00,
        kNamespace = 0x01,
        kBoth = 0x02,
    };

    BytesSpan operator>>(BytesSpan buffer, TrackNamespace& msg);
    Bytes& operator<<(Bytes& buffer, const TrackNamespace& msg);

    template<class T>
    concept HasStreamOperators = requires(T value) {
        { Bytes{} << value };
        { BytesSpan{} >> value };
    };

    template<typename Type = ParameterType>
    class ParameterList
    {
      public:
        ParameterList() = default;
        ParameterList(const ParameterList&) = default;
        ParameterList(ParameterList&&) = default;
        ParameterList& operator=(const ParameterList&) = default;
        ParameterList& operator=(ParameterList&&) = default;

        template<typename T>
            requires(std::is_trivially_copyable_v<T> || std::is_same_v<std::string, T>)
        ParameterList& Add(Type type, const T& value)
        {
            if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
                if (static_cast<uint64_t>(type) % 2 == 0) {
                    UintVar u_value(static_cast<uint64_t>(value));
                    parameters.emplace_back(type, Bytes{ u_value.begin(), u_value.end() });
                    return *this;
                }
            }

            parameters.emplace_back(type, AsOwnedBytes(value));
            return *this;
        }

        template<typename T>
            requires(!std::is_trivially_copyable_v<T> && !std::is_same_v<std::string, T>)
        ParameterList& Add(Type type, const T& value)
        {
            Bytes bytes;
            bytes << value;
            parameters.emplace_back(type, std::move(bytes));
            return *this;
        }

        template<typename T>
        ParameterList& AddOptional(Type type, const std::optional<T>& value)
        {
            if (value.has_value()) {
                Add<T>(type, value.value());
            }

            return *this;
        }

        operator std::vector<Parameter>&() noexcept { return parameters; }
        operator const std::vector<Parameter>&() const noexcept { return parameters; }

        auto begin() const noexcept { return parameters.begin(); }
        auto end() const noexcept { return parameters.end(); }
        auto at(std::size_t index) { return parameters.at(index); }

        bool Contains(Type type)
        {
            auto it = std::find(parameters.begin(), parameters.end(), type);
            return it == parameters.end();
        }

        BytesSpan Find(Type type)
        {
            auto it = std::find(parameters.begin(), parameters.end(), type);
            if (it == parameters.end()) {
                return {};
            }

            return it->value;
        }

        template<typename T>
        T Get(Type type) const noexcept
        {
            auto bytes = Find(type);
            if (bytes.empty()) {
                return {};
            }

            if (static_cast<std::uint64_t>(type) % 2 == 0) {
                return static_cast<T>(UintVar(bytes).Get());
            }

            if constexpr (HasStreamOperators<T>) {
                T result;
                bytes >> result;
                return result;
            }

            return FromBytes<T>(bytes);
        }

        std::vector<KeyValuePair<Type>> parameters;
    };

    template<typename Type = ParameterType>
    BytesSpan operator>>(BytesSpan buffer, ParameterList<Type>& msg)
    {
        uint64_t size = 0;
        buffer = buffer >> size;

        for (uint64_t i = 0; i < size; ++i) {
            KeyValuePair<Type> param;
            buffer = buffer >> param;
            msg.parameters.push_back(std::move(param));
        }

        return buffer;
    }

    template<typename Type = ParameterType>
    Bytes& operator<<(Bytes& buffer, const ParameterList<Type>& msg)
    {
        buffer << UintVar(msg.parameters.size());
        for (const auto& param : msg.parameters) {
            buffer << param;
        }

        return buffer;
    }

} // namespace
