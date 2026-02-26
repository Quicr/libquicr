#pragma once
#include "quicr/common.h"
#include "quicr/detail/uintvar.h"
#include "quicr/track_name.h"

#include <algorithm>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <string>
#include <tuple>
#include <variant>

namespace quicr::messages {

    struct ProtocolViolationException : std::runtime_error
    {
        const std::string reason;
        ProtocolViolationException(const std::string& reason,
                                   const std::source_location location = std::source_location::current())
          : std::runtime_error("Protocol violation: " + reason + " (line " + std::to_string(location.line()) +
                               ", file " + location.file_name() + ")")
          , reason(reason)
        {
        }
    };
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
         * @param prev_type The previous type value.
         * @return Encoded size, in bytes.
         */
        std::size_t Size(const T prev_type) const
        {
            std::size_t size = 0;
            const auto type_value = static_cast<std::uint64_t>(type);
            const auto prev_type_value = static_cast<std::uint64_t>(prev_type);
            const auto delta = type_value - prev_type_value;
            size += UintVar(delta).size();

            if (type_value % 2 == 0) {
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

    /**
     * Serialize KVP.
     * @param buffer Buffer to write into.
     * @param kvp The KeyValuePair to write.
     * @param prev_type The previous type value so we can do the delta.
     */
    template<KeyType T>
    void SerializeKvp(Bytes& buffer, const KeyValuePair<T>& kvp, const T prev_type)
    {
        // Delta encode the type.
        const auto type_value = static_cast<std::uint64_t>(kvp.type);
        const auto prev_type_value = static_cast<std::uint64_t>(prev_type);
        const auto delta = type_value - prev_type_value;
        buffer << UintVar(delta);

        if (type_value % 2 != 0) {
            // Odd, encode bytes.
            buffer << kvp.value;
        } else {
            // Even, single varint of value.
            if (kvp.value.size() > sizeof(std::uint64_t)) {
                throw std::invalid_argument("Value too large to encode as uint64_t.");
            }
            std::uint64_t val = 0;
            std::memcpy(&val, kvp.value.data(), kvp.value.size());
            buffer << UintVar(val);
        }
    }

    /**
     * Deserialize KVP.
     * @param buffer Buffer to read from.
     * @param kvp The KeyValuePair to read into.
     * @param prev_type The previous type value to unwrap the delta.
     * @throws ProtocolViolationException if delta causes overflow past 2^64-1.
     */
    template<KeyType T>
    void ParseKvp(BytesSpan& buffer, KeyValuePair<T>& kvp, const T prev_type)
    {
        // Delta to absolute.
        const auto prev_type_value = static_cast<std::uint64_t>(prev_type);
        std::uint64_t delta;
        buffer = buffer >> delta;
        if (delta > std::numeric_limits<std::uint64_t>::max() - prev_type_value) {
            throw ProtocolViolationException("Delta encoding overflow: prev_type + delta exceeds 2^64-1");
        }
        const auto type_value = prev_type_value + delta;
        kvp.type = static_cast<T>(type_value);

        if (type_value % 2 != 0) {
            // Odd, decode bytes.
            buffer = buffer >> kvp.value;
        } else {
            // Even, decode single varint of value.
            UintVar uvar(buffer);
            buffer = buffer.subspan(uvar.size());
            std::uint64_t val(uvar);
            const auto* p = reinterpret_cast<const std::uint8_t*>(&val);
            kvp.value.assign(p, p + uvar.size());
        }
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
        kLocationFilter = 0x21,
        kGroupFilter = 0x23,
        kSubgroupFilter = 0x25,
        kObjectFilter = 0x27,
        kPriorityFilter = 0x29,
        kPropertyFilter = 0x2B,
        kTrackFilter = 0x2D,
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
        kNone,
        kLocationFilter,
        kGroupFilter,
        kSubgroupFilter,
        kObjectFilter,
        kPriorityFilter,
        kPropertyFilter,
        kTrackFilter,
    };

    constexpr ParameterType ToParameterFilterType(FilterType type)
    {
        switch (type) {
            case FilterType::kLocationFilter:
                return ParameterType::kLocationFilter;
            case FilterType::kGroupFilter:
                return ParameterType::kGroupFilter;
            case FilterType::kSubgroupFilter:
                return ParameterType::kSubgroupFilter;
            case FilterType::kObjectFilter:
                return ParameterType::kObjectFilter;
            case FilterType::kPriorityFilter:
                return ParameterType::kPriorityFilter;
            case FilterType::kPropertyFilter:
                return ParameterType::kPropertyFilter;
            case FilterType::kTrackFilter:
                return ParameterType::kTrackFilter;
            case FilterType::kNone:
                return ParameterType::kInvalid;
        }
    }

    constexpr FilterType ToFilterType(ParameterType type)
    {
        switch (type) {
            case ParameterType::kLocationFilter:
                return FilterType::kLocationFilter;
            case ParameterType::kGroupFilter:
                return FilterType::kGroupFilter;
            case ParameterType::kSubgroupFilter:
                return FilterType::kSubgroupFilter;
            case ParameterType::kObjectFilter:
                return FilterType::kObjectFilter;
            case ParameterType::kPriorityFilter:
                return FilterType::kPriorityFilter;
            case ParameterType::kPropertyFilter:
                return FilterType::kPropertyFilter;
            case ParameterType::kTrackFilter:
                return FilterType::kTrackFilter;
            default:
                throw std::invalid_argument("parameter type is not a valid filter type");
        }
    }

    enum class LocationFilterType
    {
        kNextGroupStart,
        kLargestObject,
        kAbsoluteStart,
        kAbsoluteRange,
    };

    /**
     * @brief
     *
     * @notes: - End Object ID MAY be omitted to indicate no end within End Group.
     *         - End Group ID and End Object ID MAY be omitted to indicate no end, for an open ended subscription or
     *           a Joining Fetch which implicitly ends at the associated subscription start Location.
     *         - All but Start Group ID MAY be omitted to indicate
     */
    struct LocationFilter
    {
        std::uint64_t start_group;
        std::optional<std::uint64_t> start_object;

        std::optional<std::uint64_t> end_group;
        std::optional<std::uint64_t> end_object;

        constexpr auto operator<=>(const LocationFilter&) const noexcept = default;
    };

    inline Bytes& operator<<(Bytes& bytes, const LocationFilter& filter)
    {
        AppendBytes(bytes, UintVar(filter.start_group));

        if (filter.start_object.has_value()) {
            AppendBytes(bytes, UintVar(filter.start_object.value()));

            if (filter.end_group.has_value()) {
                AppendBytes(bytes, UintVar(filter.end_group.value()));

                if (filter.end_object.has_value()) {
                    AppendBytes(bytes, UintVar(filter.end_object.value()));
                }
            }
        }

        return bytes;
    }

    inline BytesSpan operator>>(BytesSpan bytes, [[maybe_unused]] LocationFilter& filter)
    {
        filter = LocationFilter{};

        bytes = bytes >> filter.start_group;

        if (!bytes.empty()) {
            std::uint64_t start_object = 0;
            bytes = bytes >> start_object;
            filter.start_object = start_object;

            if (!bytes.empty()) {
                std::uint64_t end_group = 0;
                bytes = bytes >> end_group;
                filter.end_group = end_group;

                if (!bytes.empty()) {
                    std::uint64_t end_object = 0;
                    bytes = bytes >> end_object;
                    filter.end_object = end_object;
                }
            }
        }

        return bytes;
    }

    struct RangeFilter
    {
        std::uint64_t start;
        std::optional<std::uint64_t> end;

        constexpr auto operator<=>(const RangeFilter&) const noexcept = default;
    };

    inline Bytes& operator<<(Bytes& bytes, const RangeFilter& filter)
    {
        AppendBytes(bytes, UintVar(filter.start));

        if (filter.end.has_value()) {
            AppendBytes(bytes, UintVar(filter.end.value()));
        }

        return bytes;
    }

    inline Bytes& operator<<(Bytes& bytes, const std::vector<RangeFilter>& filters)
    {
        for (const auto& filter : filters) {
            bytes << filter;
        }

        return bytes;
    }

    inline BytesSpan operator>>(BytesSpan bytes, [[maybe_unused]] std::vector<RangeFilter>& filter)
    {
        return bytes;
    }

    struct PropertyFilter
    {
        std::uint64_t property_type;
        std::uint64_t start;
        std::optional<std::uint64_t> end;

        constexpr auto operator<=>(const PropertyFilter&) const noexcept = default;
    };

    inline Bytes& operator<<(Bytes& bytes, const PropertyFilter& filter)
    {
        AppendBytes(bytes, UintVar(filter.property_type));
        AppendBytes(bytes, UintVar(filter.start));

        if (filter.end.has_value()) {
            AppendBytes(bytes, UintVar(filter.end.value()));
        }

        return bytes;
    }

    inline BytesSpan operator>>(BytesSpan bytes, [[maybe_unused]] PropertyFilter& filter)
    {
        return bytes;
    }

    inline Bytes& operator<<(Bytes& bytes, const std::vector<PropertyFilter>& filters)
    {
        for (const auto& filter : filters) {
            bytes << filter;
        }

        return bytes;
    }

    inline BytesSpan operator>>(BytesSpan bytes, [[maybe_unused]] std::vector<PropertyFilter>& filter)
    {
        return bytes;
    }

    struct TrackFilter
    {
        std::uint64_t property_type;
        std::uint64_t max_tracks_selected;
        std::uint64_t max_tracks_deselected;
        std::uint64_t max_time_selected;

        constexpr auto operator<=>(const TrackFilter&) const noexcept = default;
    };

    inline Bytes& operator<<(Bytes& bytes, const TrackFilter& filter)
    {
        AppendBytes(bytes, UintVar(filter.property_type));
        AppendBytes(bytes, UintVar(filter.max_tracks_selected));
        AppendBytes(bytes, UintVar(filter.max_tracks_deselected));
        AppendBytes(bytes, UintVar(filter.max_time_selected));

        return bytes;
    }

    inline BytesSpan operator>>(BytesSpan bytes, TrackFilter& filter)
    {
        bytes = bytes >> filter.property_type;
        bytes = bytes >> filter.max_tracks_selected;
        bytes = bytes >> filter.max_tracks_deselected;
        bytes = bytes >> filter.max_time_selected;

        return bytes;
    }

    struct GroupFilter
    {
        GroupFilter(std::initializer_list<RangeFilter> fs)
          : ranges(fs)
        {
        }

        std::vector<RangeFilter> ranges;
        auto operator<=>(const GroupFilter&) const noexcept = default;
    };

    inline Bytes& operator<<(Bytes& bytes, const GroupFilter& filter)
    {
        return bytes << filter.ranges;
    }

    inline BytesSpan operator>>(BytesSpan bytes, GroupFilter& filter)
    {
        return bytes >> filter.ranges;
    }

    struct SubgroupFilter
    {
        SubgroupFilter(std::initializer_list<RangeFilter> fs)
          : ranges(fs)
        {
        }

        std::vector<RangeFilter> ranges;
        auto operator<=>(const SubgroupFilter&) const noexcept = default;
    };

    inline Bytes& operator<<(Bytes& bytes, const SubgroupFilter& filter)
    {
        return bytes << filter.ranges;
    }

    inline BytesSpan operator>>(BytesSpan bytes, SubgroupFilter& filter)
    {
        return bytes >> filter.ranges;
    }

    struct ObjectFilter
    {
        ObjectFilter(std::initializer_list<RangeFilter> fs)
          : ranges(fs)
        {
        }

        std::vector<RangeFilter> ranges;
        auto operator<=>(const ObjectFilter&) const noexcept = default;
    };

    inline Bytes& operator<<(Bytes& bytes, const ObjectFilter& filter)
    {
        return bytes << filter.ranges;
    }

    inline BytesSpan operator>>(BytesSpan bytes, ObjectFilter& filter)
    {
        return bytes >> filter.ranges;
    }

    struct PriorityFilter
    {
        PriorityFilter(std::initializer_list<RangeFilter> fs)
          : ranges(fs)
        {
        }

        std::vector<RangeFilter> ranges;
        auto operator<=>(const PriorityFilter&) const noexcept = default;
    };

    inline Bytes& operator<<(Bytes& bytes, const PriorityFilter& filter)
    {
        return bytes << filter.ranges;
    }

    inline BytesSpan operator>>(BytesSpan bytes, PriorityFilter& filter)
    {
        return bytes >> filter.ranges;
    }

    using Filter = std::variant<std::monostate,
                                LocationFilter,
                                GroupFilter,
                                SubgroupFilter,
                                ObjectFilter,
                                PriorityFilter,
                                std::vector<PropertyFilter>,
                                TrackFilter>;

    inline FilterType GetFilterType(const Filter& filter)
    {
        return std::visit(
          [](auto&& f) {
              using T = std::decay_t<decltype(f)>;
              if constexpr (std::is_same_v<std::monostate, T>) {
                  return FilterType::kNone;
              } else if constexpr (std::is_same_v<LocationFilter, T>) {
                  return FilterType::kLocationFilter;
              } else if constexpr (std::is_same_v<GroupFilter, T>) {
                  return FilterType::kGroupFilter;
              } else if constexpr (std::is_same_v<SubgroupFilter, T>) {
                  return FilterType::kSubgroupFilter;
              } else if constexpr (std::is_same_v<ObjectFilter, T>) {
                  return FilterType::kObjectFilter;
              } else if constexpr (std::is_same_v<PriorityFilter, T>) {
                  return FilterType::kPriorityFilter;
              } else if constexpr (std::is_same_v<std::vector<PropertyFilter>, T>) {
                  return FilterType::kPropertyFilter;
              } else if constexpr (std::is_same_v<TrackFilter, T>) {
                  return FilterType::kTrackFilter;
              }
          },
          filter);
    }

    inline ParameterType GetFilterParameterType(const Filter& filter)
    {
        return ToParameterFilterType(GetFilterType(filter));
    }

    inline Bytes& operator<<(Bytes& bytes, const Filter& filter)
    {
        std::visit(
          [&](auto&& f) {
              using T = std::decay_t<decltype(f)>;
              if constexpr (!std::is_same_v<std::monostate, T>) {
                  bytes << f;
              }
          },
          filter);
        return bytes;
    }

    inline BytesSpan operator>>(BytesSpan, Filter&)
    {
        throw std::runtime_error("parsing a non specific filter is impossible, stream to a more specific filter type");
    }

    inline Parameter SerializeFilter(FilterType filter_type, const Filter& filter)
    {
        auto param = std::visit(
          [&](auto&& f) {
              using T = std::decay_t<decltype(f)>;

              if constexpr (std::is_same_v<std::monostate, T>) {
                  return Parameter{ ToParameterFilterType(filter_type), Bytes{} };
              } else {
                  Bytes bytes;
                  return Parameter{ ToParameterFilterType(filter_type), bytes << f };
              }
          },
          filter);

        return param;
    }

    inline Filter DeserializeFilter(FilterType filter_type, BytesSpan bytes)
    {
        // TODO: Figure out how to parse the vector filters.
        switch (filter_type) {
            case FilterType::kLocationFilter: {
                LocationFilter filter{};
                bytes = bytes >> filter;
                return filter;
            }
            case FilterType::kGroupFilter: {
                return std::monostate{};
            }
            case FilterType::kSubgroupFilter: {
                return std::monostate{};
            }
            case FilterType::kObjectFilter: {
                return std::monostate{};
            }
            case FilterType::kPriorityFilter: {
                return std::monostate{};
            }
            case FilterType::kPropertyFilter: {
                return std::monostate{};
            }
            case FilterType::kTrackFilter: {
                TrackFilter filter{};
                bytes = bytes >> filter;
                return filter;
            }
            default:
                return std::monostate{};
        }

        return std::monostate{};
    }

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

    enum class ExtensionType : uint64_t
    {
        // Track Scope Extensions
        kDeliveryTimeout = 0x02,
        kMaxCacheDuration = 0x04,
        kDefaultPublisherPriority = 0x0E,
        kDefaultPublisherGroupOrder = 0x22,
        kDynamicGroups = 0x30,

        // Object Scope Extensions
        kPriorGroupIdGap = 0x3C,
        kPriorObjectIdGap = 0x3E,

        // Dual Scope (Track and Object) Extensions
        kImmutable = 0x0B,
    };

    template<class T>
    concept HasByteStreamOperators = requires(T& value, Bytes& bytes) {
        { bytes << value } -> std::same_as<Bytes&>;
        { BytesSpan{} >> value } -> std::same_as<BytesSpan>;
    };

    class TrackExtensions
    {
      public:
        TrackExtensions() = default;

        TrackExtensions(const std::map<std::uint64_t, Bytes>& ext)
          : extensions(ext)
        {
        }

      protected:
        template<typename T>
        Bytes ToBytes(ExtensionType type, const T& value)
        {
            if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
                if (static_cast<uint64_t>(type) % 2 == 0) {
                    const std::uint64_t val = static_cast<std::uint64_t>(value);
                    auto* val_bytes = reinterpret_cast<const std::uint8_t*>(&val);
                    return Bytes{ val_bytes, val_bytes + sizeof(val) };
                }
            }

            if constexpr (HasByteStreamOperators<T>) {
                Bytes bytes;
                bytes << value;
                return bytes;
            }

            return AsOwnedBytes(value);
        }

      public:
        template<typename T>
        TrackExtensions& Add(ExtensionType type, const T& value)
        {
            if (type == ExtensionType::kImmutable) {
                throw std::invalid_argument("ExtensionType::kImmutable should not be used directly, use AddImmutable "
                                            "with the keytype you want to use");
            }

            extensions[static_cast<std::uint64_t>(type)] = ToBytes<T>(type, value);

            return *this;
        }

        template<typename T>
        TrackExtensions& AddImmutable(ExtensionType type, const T& value)
        {
            immutable_extensions[static_cast<std::uint64_t>(type)] = ToBytes<T>(type, value);
            return *this;
        }

        template<typename T>
        T Get(ExtensionType type) const
        {
            if constexpr (std::is_arithmetic_v<T>) {
                if (static_cast<std::uint64_t>(type) % 2 == 0) {
                    std::uint64_t val = 0;
                    const auto& bytes = extensions.at(static_cast<std::uint64_t>(type));
                    std::memcpy(&val, bytes.data(), std::min(bytes.size(), sizeof(val)));
                    return static_cast<T>(val);
                }
            }

            if constexpr (HasByteStreamOperators<T>) {
                T result;
                extensions.at(static_cast<std::uint64_t>(type)) >> result;
                return result;
            }

            return FromBytes<T>(extensions.at(static_cast<std::uint64_t>(type)));
        }

        template<typename T>
        T GetImmutable(ExtensionType type) const
        {
            if constexpr (std::is_arithmetic_v<T>) {
                if (static_cast<std::uint64_t>(type) % 2 == 0) {
                    std::uint64_t val = 0;
                    const auto& bytes = immutable_extensions.at(static_cast<std::uint64_t>(type));
                    std::memcpy(&val, bytes.data(), std::min(bytes.size(), sizeof(val)));
                    return static_cast<T>(val);
                }
            }

            if constexpr (HasByteStreamOperators<T>) {
                T result;
                immutable_extensions.at(static_cast<std::uint64_t>(type)) >> result;
                return result;
            }

            return FromBytes<T>(immutable_extensions.at(static_cast<std::uint64_t>(type)));
        }

        auto begin() const noexcept { return extensions.begin(); }
        auto end() const noexcept { return extensions.end(); }

        std::map<std::uint64_t, Bytes> extensions;
        std::map<std::uint64_t, Bytes> immutable_extensions;
    };

    BytesSpan operator>>(BytesSpan buffer, TrackExtensions& msg);
    Bytes& operator<<(Bytes& buffer, const TrackExtensions& msg);

    BytesSpan operator>>(BytesSpan buffer, TrackNamespace& msg);
    Bytes& operator<<(Bytes& buffer, const TrackNamespace& msg);

    template<typename Type = ParameterType>
    class ParameterList
    {
      public:
        ParameterList() = default;
        ParameterList(const ParameterList&) = default;
        ParameterList(ParameterList&&) = default;
        ParameterList& operator=(const ParameterList&) = default;
        ParameterList& operator=(ParameterList&&) = default;

        ParameterList(std::initializer_list<KeyValuePair<Type>> values)
          : parameters(values)
        {
        }

        template<typename T>
        ParameterList& Add(Type type, const T& value)
        {
            if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
                if (static_cast<uint64_t>(type) % 2 == 0) {
                    const std::uint64_t val = static_cast<std::uint64_t>(value);
                    auto* val_bytes = reinterpret_cast<const std::uint8_t*>(&val);
                    parameters.push_back({ type, Bytes{ val_bytes, val_bytes + sizeof(val) } });
                    return *this;
                }
            }

            if constexpr (HasByteStreamOperators<T>) {
                Bytes bytes;
                bytes << value;
                parameters.push_back({ type, std::move(bytes) });
                return *this;
            }

            parameters.push_back({ type, AsOwnedBytes(value) });
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

        bool Contains(Type type) const
        {
            auto it =
              std::find_if(parameters.begin(), parameters.end(), [type](const auto& kv) { return kv.type == type; });
            return it != parameters.end();
        }

        BytesSpan Find(Type type) const
        {
            auto it =
              std::find_if(parameters.begin(), parameters.end(), [type](const auto& kv) { return kv.type == type; });
            if (it == parameters.end()) {
                return {};
            }

            return it->value;
        }

        template<typename T>
        T Get(Type type) const
        {
            auto bytes = Find(type);
            if (bytes.empty()) {
                return {};
            }

            if constexpr (std::is_arithmetic_v<T>) {
                if (static_cast<std::uint64_t>(type) % 2 == 0) {
                    std::uint64_t val = 0;
                    std::memcpy(&val, bytes.data(), std::min(bytes.size(), sizeof(val)));
                    return static_cast<T>(val);
                }
            }

            if constexpr (HasByteStreamOperators<T>) {
                T result;
                bytes >> result;
                return result;
            }

            return FromBytes<T>(bytes);
        }

        Filter GetFilter(FilterType type)
        {
            if constexpr (!std::is_same_v<ParameterType, Type>) {
                return std::monostate{};
            }

            auto bytes = Find(ToParameterFilterType(type));
            if (bytes.empty()) {
                return {};
            }

            return DeserializeFilter(type, bytes);
        }

        template<typename T>
        std::optional<T> GetOptional(Type type) const
        {
            return Contains(type) ? std::make_optional(Get<T>(type)) : std::nullopt;
        }

        auto operator<=>(const ParameterList&) const = default;

        std::vector<KeyValuePair<Type>> parameters;
    };

    template<typename Type = ParameterType>
    BytesSpan operator>>(BytesSpan buffer, ParameterList<Type>& msg)
    {
        uint64_t size = 0;
        buffer = buffer >> size;

        Type prev_type{};
        for (uint64_t i = 0; i < size; ++i) {
            KeyValuePair<Type> param;
            ParseKvp(buffer, param, prev_type);
            prev_type = param.type;
            msg.parameters.push_back(std::move(param));
        }

        return buffer;
    }

    template<typename Type = ParameterType>
    Bytes& operator<<(Bytes& buffer, const ParameterList<Type>& msg)
    {
        buffer << UintVar(msg.parameters.size());

        // Sort parameters by type for delta encoding
        auto sorted = msg.parameters;
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
            return static_cast<std::uint64_t>(a.type) < static_cast<std::uint64_t>(b.type);
        });

        Type prev_type{};
        for (const auto& param : sorted) {
            SerializeKvp(buffer, param, prev_type);
            prev_type = param.type;
        }

        return buffer;
    }

} // namespace
