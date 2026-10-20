// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/messages/uintvar.h"
#include "quicr/track_name.h"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <tuple>
#include <variant>

namespace quicr {
    /**
     * Parses a counted TrackNamespace and advances the span.
     * @param buffer Input span.
     * @param msg Namespace receiving up to 32 copied entries.
     * @return Unconsumed suffix.
     * @throws std::invalid_argument if the entry count exceeds 32 or input is malformed.
     */
    BytesSpan operator>>(BytesSpan buffer, TrackNamespace& msg);

    /**
     * Appends a counted TrackNamespace.
     * @param buffer Destination bytes.
     * @param msg Namespace to encode.
     * @return @p buffer.
     */
    Bytes& operator<<(Bytes& buffer, const TrackNamespace& msg);
}

namespace quicr::messages {

    /// MoQ control-message wire type codes.
    enum class ControlMessageType : uint64_t
    {
        kRequestUpdate = 0x2,          ///< REQUEST_UPDATE.
        kSubscribe = 0x3,              ///< SUBSCRIBE.
        kSubscribeOk = 0x4,            ///< SUBSCRIBE_OK.
        kRequestError = 0x5,           ///< REQUEST_ERROR.
        kPublishNamespace = 0x6,       ///< PUBLISH_NAMESPACE.
        kRequestOk = 0x7,              ///< REQUEST_OK.
        kNamespace = 0x8,              ///< NAMESPACE.
        kPublishNamespaceDone = 0x9,   ///< PUBLISH_NAMESPACE_DONE.
        kUnsubscribe = 0xa,            ///< UNSUBSCRIBE.
        kPublishDone = 0xb,            ///< PUBLISH_DONE.
        kPublishNamespaceCancel = 0xc, ///< PUBLISH_NAMESPACE_CANCEL.
        kTrackStatus = 0xd,            ///< TRACK_STATUS.
        kNamespaceDone = 0xe,          ///< NAMESPACE_DONE.
        kPublishBlocked = 0xf,         ///< PUBLISH_BLOCKED.
        kGoaway = 0x10,                ///< GOAWAY.
        kMaxRequestId = 0x15,          ///< MAX_REQUEST_ID.
        kFetch = 0x16,                 ///< FETCH.
        kFetchCancel = 0x17,           ///< FETCH_CANCEL.
        kFetchOk = 0x18,               ///< FETCH_OK.
        kRequestsBlocked = 0x1a,       ///< REQUESTS_BLOCKED.
        kPublish = 0x1d,               ///< PUBLISH.
        kSubscribeNamespace = 0x50,    ///< SUBSCRIBE_NAMESPACE.
        kSubscribeTracks = 0x51,       ///< SUBSCRIBE_TRACKS.
        kSetup = 0x2F00,               ///< SETUP.
    };

    /// Exception indicating that received or constructed data violates the MoQ protocol.
    struct ProtocolViolationException : std::runtime_error
    {
        const std::string reason; ///< Unformatted protocol-violation reason.

        /**
         * @param reason Violation explanation.
         * @param location Source location attributed to the check.
         */
        ProtocolViolationException(const std::string& reason,
                                   const std::source_location location = std::source_location::current())
          : std::runtime_error("Protocol violation: " + reason + " (line " + std::to_string(location.line()) +
                               ", file " + location.file_name() + ")")
          , reason(reason)
        {
        }
    };

    /**
     * Appends a varint length and copied bytes.
     * @param buffer Destination bytes.
     * @param bytes Bytes to copy.
     * @return @p buffer.
     */
    Bytes& operator<<(Bytes& buffer, const Bytes& bytes);

    /**
     * Appends a varint length and copied span bytes.
     * @param buffer Destination bytes.
     * @param bytes Bytes to copy.
     * @return @p buffer.
     */
    Bytes& operator<<(Bytes& buffer, const BytesSpan& bytes);

    /**
     * Parses length-prefixed bytes.
     * @param buffer Input span.
     * @param value Destination for the parsed bytes.
     * @return Unconsumed suffix.
     */
    BytesSpan operator>>(BytesSpan buffer, Bytes& value);

    /**
     * Appends a value as UintVar.
     * @param buffer Destination bytes.
     * @param value Value to encode.
     * @return @p buffer.
     */
    Bytes& operator<<(Bytes& buffer, std::uint64_t value);

    /**
     * Parses one UintVar.
     * @param buffer Input span.
     * @param value Destination for the decoded value.
     * @return Unconsumed suffix.
     */
    BytesSpan operator>>(BytesSpan buffer, std::uint64_t& value);

    /**
     * Appends one raw byte.
     * @param buffer Destination bytes.
     * @param value Byte to append.
     * @return @p buffer.
     */
    Bytes& operator<<(Bytes& buffer, std::uint8_t value);

    /**
     * Parses one raw byte.
     * @param buffer Input span.
     * @param value Destination for the parsed byte.
     * @return Unconsumed suffix.
     * @throws std::invalid_argument on truncation.
     */
    BytesSpan operator>>(BytesSpan buffer, uint8_t& value);

    /**
     * Appends a two-byte big-endian integer.
     * @param buffer Destination bytes.
     * @param value Value to append.
     * @return @p buffer.
     */
    Bytes& operator<<(Bytes& buffer, std::uint16_t value);

    /**
     * Parses a two-byte big-endian integer.
     * @param buffer Input span.
     * @param value Destination for the parsed value.
     * @return Unconsumed suffix.
     * @throws std::invalid_argument on truncation.
     */
    BytesSpan operator>>(BytesSpan buffer, std::uint16_t& value);

    /**
     * Appends the stored UintVar encoding verbatim.
     * @param buffer Destination bytes.
     * @param value Encoding to append.
     * @return @p buffer.
     */
    Bytes& operator<<(Bytes& buffer, const UintVar& value);

    /// Absolute group/object location ordered lexicographically by group then object.
    struct Location
    {
        std::uint64_t group{ 0 };  ///< Group identifier.
        std::uint64_t object{ 0 }; ///< Object identifier within the group.

        /// @param other Location to compare.
        /// @return Lexicographic ordering by group, then object.
        auto operator<=>(const Location& other) const
        {
            if (const auto cmp = group <=> other.group; cmp != 0) {
                return cmp;
            }
            return object <=> other.object;
        }

        /// @param other Location to compare.
        /// @return Whether both identifiers equal those in @p other.
        bool operator==(const Location& other) const = default;
    };

    /**
     * Appends a location as two UintVars.
     * @param buffer Destination bytes.
     * @param location Location to encode.
     * @return @p buffer.
     */
    Bytes& operator<<(Bytes& buffer, const Location& location);

    /**
     * Parses two UintVars as a location.
     * @param buffer Input span.
     * @param location Destination for the parsed location.
     * @return Unconsumed suffix.
     */
    BytesSpan operator>>(BytesSpan buffer, Location& location);

    /// Inclusive terminal location for a fetch range.
    struct FetchEndLocation
    {
        std::uint64_t group{ 0 }; ///< The group ID of the fetch's end location (inclusive).
        std::optional<std::uint64_t>
          object; ///< The object ID of the fetch's end location (inclusive), or null for the whole group.
    };

    /**
     * Types usable as delta-encoded KVP keys.
     * @tparam T Either uint64_t or an enum with uint64_t underlying type.
     */
    template<typename T>
    concept KeyType =
      std::same_as<T, std::uint64_t> || (std::is_enum_v<T> && std::same_as<std::underlying_type_t<T>, std::uint64_t>);
    template<KeyType T>
    /**
     * MoQ key/value pair.
     *
     * Keys are delta-encoded relative to the preceding key. Odd keys carry
     * length-prefixed opaque bytes; even keys carry a UintVar represented in
     * `value` using host-memory byte order and up to eight bytes.
     * @tparam T Key type satisfying KeyType.
     */
    struct KeyValuePair
    {
        T type;      ///< Absolute key type.
        Bytes value; ///< Opaque odd-key bytes or host-order integer bytes for an even key.

        /**
         * Get the encoded size of this KeyValuePair, in bytes.
         * @param prev_type The previous type value.
         * @return Encoded size, in bytes.
         * @throws std::invalid_argument if an even-key value exceeds eight bytes.
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
         * @throws std::invalid_argument if either even-key value exceeds eight bytes.
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

    /**
     * Serializes a uint64-compatible key as UintVar.
     * @tparam T Key type.
     * @param buffer Destination bytes.
     * @param value Value to encode.
     * @return @p buffer.
     */
    template<KeyType T>
    Bytes& operator<<(Bytes& buffer, const T value)
    {
        return buffer << UintVar(static_cast<std::uint64_t>(value));
    }

    /**
     * Parses a UintVar key and advances the span.
     * @tparam T Key type.
     * @param buffer Input span.
     * @param value Decoded value.
     * @return Unconsumed suffix.
     */
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
     * @throws std::invalid_argument if an even-key value exceeds eight bytes.
     * @warning @p kvp.type must not be less than @p prev_type; this function does not validate underflow.
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
     * @note @p buffer is mutated to its unconsumed suffix.
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
            kvp.value.assign(p, p + sizeof(val));
        }
    }

    /// SETUP option type codes.
    enum struct SetupOptionType : uint64_t
    {
        kPath = 0x01,                  ///< Request path bytes.
        kAuthorizationToken = 0x03,    ///< Sensitive opaque authorization token; never log its bytes.
        kMaxAuthTokenCacheSize = 0x04, ///< Maximum authorization-token alias cache size.
        kAuthority = 0x05,             ///< Authority bytes.
        kMoqtImplementation = 0x07,    ///< Implementation identification bytes.

        /*===================================================================*/
        // Internal Use
        /*===================================================================*/

        kEndpointId = 0xF1, ///< Internal endpoint identifier.
        kInvalid = 0xFF,    ///< Internal invalid sentinel.
    };

    /**
     * Authorization token reference or value.
     *
     * Token payload bytes are sensitive and opaque. They must not be logged,
     * interpreted by this serialization layer, or embedded in diagnostics.
     */
    struct Token
    {
        /// Operation encoded at the beginning of an authorization-token value.
        enum class AliasType : std::uint64_t
        {
            kDelete = 0x0,   ///< Delete the specified cached alias.
            kRegister = 0x1, ///< Register alias, token type, and token value.
            kUseAlias = 0x2, ///< Refer to a previously registered alias.
            kUseValue = 0x3, ///< Carry a token type and token value directly.
        };

        AliasType alias_type{ AliasType::kUseValue }; ///< Alias operation.

        /// Alias required for delete, register, and use-alias operations.
        std::optional<std::uint64_t> token_alias;

        /// Application-defined token type required when a value is present.
        std::optional<std::uint64_t> token_type;

        /// Sensitive opaque token bytes; never log this field.
        Bytes token_value;

        /// Compares all token fields byte-for-byte.
        /// @param other Token to compare.
        /// @return Whether all fields are equal.
        bool operator==(const Token& other) const = default;
    };

    /**
     * Serializes a token body without an enclosing length.
     * @param buffer Destination bytes.
     * @param token Token to serialize.
     * @return @p buffer.
     * @throws std::bad_optional_access when fields required by alias_type are absent.
     */
    inline Bytes& operator<<(Bytes& buffer, const Token& token)
    {
        buffer << UintVar(static_cast<std::uint64_t>(token.alias_type));
        switch (token.alias_type) {
            case Token::AliasType::kDelete:
            case Token::AliasType::kUseAlias:
                buffer << UintVar(token.token_alias.value());
                break;
            case Token::AliasType::kRegister:
                buffer << UintVar(token.token_alias.value());
                buffer << UintVar(token.token_type.value());
                AppendBytes(buffer, token.token_value);
                break;
            case Token::AliasType::kUseValue:
                buffer << UintVar(token.token_type.value());
                AppendBytes(buffer, token.token_value);
                break;
        }
        return buffer;
    }

    /**
     * Parses a token body.
     *
     * Register and use-value consume every byte remaining in @p buffer as the
     * token value, so callers must pass a span bounded to one token parameter.
     * @param buffer Input span bounded to one token parameter.
     * @param token Destination token.
     * @return Empty suffix for value-carrying forms, otherwise the unconsumed suffix.
     * @throws ProtocolViolationException for an unknown alias operation.
     * @warning Parsed token bytes are sensitive opaque data and must not be logged.
     */
    inline BytesSpan operator>>(BytesSpan buffer, Token& token)
    {
        std::uint64_t alias_type = 0;
        buffer = buffer >> alias_type;
        token.alias_type = static_cast<Token::AliasType>(alias_type);

        switch (token.alias_type) {
            case Token::AliasType::kDelete:
            case Token::AliasType::kUseAlias: {
                std::uint64_t alias = 0;
                buffer = buffer >> alias;
                token.token_alias = alias;
                break;
            }
            case Token::AliasType::kRegister: {
                std::uint64_t alias = 0;
                std::uint64_t type = 0;
                buffer = buffer >> alias;
                buffer = buffer >> type;
                token.token_alias = alias;
                token.token_type = type;
                token.token_value.assign(buffer.begin(), buffer.end());
                buffer = buffer.subspan(buffer.size());
                break;
            }
            case Token::AliasType::kUseValue: {
                std::uint64_t type = 0;
                buffer = buffer >> type;
                token.token_type = type;
                token.token_value.assign(buffer.begin(), buffer.end());
                buffer = buffer.subspan(buffer.size());
                break;
            }
            default:
                throw ProtocolViolationException("Invalid AUTHORIZATION_TOKEN alias type");
        }
        return buffer;
    }

    /// Requested group traversal order.
    enum struct GroupOrder : uint8_t
    {
        kAscending = 0x1, ///< Increasing group identifiers.
        kDescending = 0x2 ///< Decreasing group identifiers.
    };

    /** Serializes a validated GroupOrder as UintVar. @throws ProtocolViolationException for another value. */
    Bytes& operator<<(Bytes& buffer, GroupOrder value);

    /** Parses and validates a GroupOrder. @return Unconsumed suffix. @throws ProtocolViolationException for another
     * value. */
    BytesSpan operator>>(BytesSpan buffer, GroupOrder& value);

    /// In-memory filter discriminator.
    enum struct FilterType : uint64_t
    {
        kNone,               ///< No filter.v
        kSubscriptionFilter, ///< Location/subscription filter.
        kTrackFilter,        ///< Track-selection constraints.
    };

    /// Subscription-filter form
    enum class SubscriptionFilterType : std::uint64_t
    {
        kNextGroupStart = 0x1, ///< Start at the next group.
        kLargestObject = 0x2,  ///< Start at the largest object.
        kAbsoluteStart = 0x3,  ///< Start at an absolute location.
        kAbsoluteRange = 0x4,  ///< Select an absolute range.
    };

    struct SubscriptionFilter
    {
        Range range;

        constexpr auto operator<=>(const SubscriptionFilter&) const noexcept = default;
    };

    /**
     * Serializes a location filter.
     * @param bytes Destination bytes.
     * @param filter Filter to serialize.
     * @return @p bytes.
     */
    inline Bytes& operator<<(Bytes& bytes, const SubscriptionFilter& filter)
    {
        bytes << UintVar(static_cast<std::uint64_t>(filter.type));
        switch (filter.type) {
            case SubscriptionFilterType::kNextGroupStart:
            case SubscriptionFilterType::kLargestObject:
                break;
            case SubscriptionFilterType::kAbsoluteStart:
                bytes << filter.start_location;
                break;
            case SubscriptionFilterType::kAbsoluteRange:
                if (filter.end_group_delta > std::numeric_limits<std::uint64_t>::max() - filter.start_location.group) {
                    throw ProtocolViolationException("Subscription filter end group overflow");
                }
                bytes << filter.start_location << UintVar(filter.end_group_delta);
                break;
            default:
                throw ProtocolViolationException("Invalid subscription filter type");
        }
        return bytes;
    }

    /**
     * @brief
     *
     * @notes: - End Object ID MAY be omitted to indicate no end within End Group.
     *         - End Group ID and End Object ID MAY be omitted to indicate no end, for an open ended subscription or
     *           a Joining Fetch which implicitly ends at the associated subscription start Location.
     *         - All but Start Group ID MAY be omitted to indicate
     */
    struct SubscriptionFilter
    {
        SubscriptionFilterType type{ SubscriptionFilterType::kNextGroupStart };
        Location start_location{};
        std::uint64_t end_group_delta{ 0 };

        auto operator<=>(const SubscriptionFilter&) const noexcept = default;
    };

    inline Bytes& operator<<(Bytes& bytes, const SubscriptionFilter& filter)
    {
        bytes << UintVar(static_cast<std::uint64_t>(filter.type));
        switch (filter.type) {
            case SubscriptionFilterType::kNextGroupStart:
            case SubscriptionFilterType::kLargestObject:
                break;
            case SubscriptionFilterType::kAbsoluteStart:
                bytes << filter.start_location;
                break;
            case SubscriptionFilterType::kAbsoluteRange:
                if (filter.end_group_delta > std::numeric_limits<std::uint64_t>::max() - filter.start_location.group) {
                    throw ProtocolViolationException("Subscription filter end group overflow");
                }
                bytes << filter.start_location << UintVar(filter.end_group_delta);
                break;
            default:
                throw ProtocolViolationException("Invalid subscription filter type");
        }
        return bytes;
    }

    /**
     * Resets and parses a location filter.
     * @param bytes Input span.
     * @param filter Destination filter.
     * @return Unconsumed suffix.
     */
    inline BytesSpan operator>>(BytesSpan bytes, SubscriptionFilter& filter)
    {
        filter = SubscriptionFilter{};
        std::uint64_t type = 0;
        bytes = bytes >> type;
        filter.type = static_cast<SubscriptionFilterType>(type);
        switch (filter.type) {
            case SubscriptionFilterType::kNextGroupStart:
            case SubscriptionFilterType::kLargestObject:
                break;
            case SubscriptionFilterType::kAbsoluteStart:
                bytes = bytes >> filter.start_location;
                break;
            case SubscriptionFilterType::kAbsoluteRange:
                bytes = bytes >> filter.start_location >> filter.end_group_delta;
                if (filter.end_group_delta > std::numeric_limits<std::uint64_t>::max() - filter.start_location.group) {
                    throw ProtocolViolationException("Subscription filter end group overflow");
                }
                break;
            default:
                throw ProtocolViolationException("Invalid subscription filter type");
        }
        return bytes;
    }

    struct TrackFilter
    {
        std::uint64_t property_type;       ///< Property type used for selection.
        std::uint64_t max_tracks_selected; ///< Maximum number of selected tracks.
        std::uint64_t timeout;             ///< Selection timeout.

        /// Compares all fields.
        /// @param other Filter to compare.
        /// @return Lexicographic comparison of all fields.
        constexpr auto operator<=>(const TrackFilter& other) const noexcept = default;
    };

    /**
     * Serializes three UintVars.
     * @param bytes Destination bytes.
     * @param filter Filter to serialize.
     * @return @p bytes.
     */
    inline Bytes& operator<<(Bytes& bytes, const TrackFilter& filter)
    {
        AppendBytes(bytes, UintVar(filter.property_type));
        AppendBytes(bytes, UintVar(filter.max_tracks_selected));
        AppendBytes(bytes, UintVar(filter.timeout));

        return bytes;
    }

    /**
     * Parses three UintVars.
     * @param bytes Input span.
     * @param filter Destination filter.
     * @return Unconsumed suffix.
     */
    inline BytesSpan operator>>(BytesSpan bytes, TrackFilter& filter)
    {
        bytes = bytes >> filter.property_type;
        bytes = bytes >> filter.max_tracks_selected;
        bytes = bytes >> filter.timeout;

        return bytes;
    }

    /// Any supported filter representation; monostate means no/unsupported filter.
    using Filter = std::variant<std::monostate, SubscriptionFilter, TrackFilter>;

    /// @param filter Filter to inspect.
    /// @return Discriminator corresponding to the active @p filter alternative.
    inline FilterType GetFilterType(const Filter& filter)
    {
        return std::visit(
          [](auto&& f) {
              using T = std::decay_t<decltype(f)>;
              if constexpr (std::is_same_v<std::monostate, T>) {
                  return FilterType::kNone;
              } else if constexpr (std::is_same_v<SubscriptionFilter, T>) {
                  return FilterType::kSubscriptionFilter;
              } else if constexpr (std::is_same_v<TrackFilter, T>) {
                  return FilterType::kTrackFilter;
              }
          },
          filter);
    }

    /**
     * Serializes the active filter body without a discriminator.
     * @param bytes Destination bytes.
     * @param filter Filter to serialize.
     * @return @p bytes.
     */
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

    /**
     * Rejects decoding without a concrete filter type.
     * @throws std::runtime_error unconditionally.
     */
    inline BytesSpan operator>>(BytesSpan, Filter&)
    {
        throw std::runtime_error("parsing a non specific filter is impossible, stream to a more specific filter type");
    }

    /**
     * Decodes a concrete filter body.
     * @param filter_type Expected filter type.
     * @param bytes Span bounded to the filter body.
     * @return LocationFilter or TrackFilter for supported types; monostate for
     * none, unknown, subgroup, object, priority, and property filters.
     * @note Vector-based and property-filter decoding is currently unsupported.
     */
    inline Filter DeserializeFilter(FilterType filter_type, BytesSpan bytes)
    {
        switch (filter_type) {
            case FilterType::kSubscriptionFilter: {
                SubscriptionFilter filter{};
                bytes = bytes >> filter;
                if (!bytes.empty()) {
                    throw ProtocolViolationException("Trailing bytes in subscription filter");
                }
                return filter;
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

    /// Legacy publish-done status codes.
    enum class PublishDoneStatusCode : uint64_t
    {
        kInternalError = 0x0,     ///< Internal error.
        kUnauthorized = 0x1,      ///< Authorization failed.
        kTrackEnded = 0x2,        ///< Track ended.
        kSubscribtionEnded = 0x3, ///< Subscription ended (legacy misspelled name).
        kGoingAway = 0x4,         ///< Endpoint is going away.
        kTooFarBehind 0x5,        ///< Consumer fell too far behind.
        kExpired = 0x6,
        kMalformedTrack = 0x12,
        kUpdateFailed = 0x8,
        kExcessiveLoad = 0x9
    };

    /**
     * Serializes a PublishDoneStatusCode as UintVar.
     * @param buffer Destination bytes.
     * @param value Status code to serialize.
     * @return @p buffer.
     */
    Bytes& operator<<(Bytes& buffer, PublishDoneStatusCode value);

    /**
     * Parses a PublishDoneStatusCode without range validation.
     * @param buffer Input span.
     * @param value Destination status code.
     * @return Unconsumed suffix.
     */
    BytesSpan operator>>(BytesSpan buffer, PublishDoneStatusCode& value);

    /// FETCH request form.
    enum class FetchType : uint8_t
    {
        kStandalone = 0x1,     ///< Independent fetch.
        kRelativeJoiningFetch, ///< Joining fetch relative to a subscription.
        kAbsoluteJoiningFetch  ///< Joining fetch with absolute bounds.
    };

    /**
     * Serializes a FetchType as UintVar.
     * @param buffer Destination bytes.
     * @param value Fetch type to serialize.
     * @return @p buffer.
     */
    Bytes& operator<<(Bytes& buffer, FetchType value);

    /**
     * Parses one raw byte as FetchType without range validation.
     * @param buffer Input span.
     * @param value Destination fetch type.
     * @return Unconsumed suffix.
     */
    BytesSpan operator>>(BytesSpan buffer, FetchType& value);

    /// Session termination reason codes.
    enum class TerminationReason : uint64_t
    {
        kNoError = 0x0,                   ///< Clean termination.
        kInternalError = 0x1,             ///< Internal endpoint failure.
        kUnauthorized = 0x2,              ///< Authorization failure.
        kProtocolViolation = 0x3,         ///< Peer violated the protocol.
        kInvalidRequestId = 0x4,          ///< Invalid request identifier.
        kDuplicateTrackAlias = 0x5,       ///< Track alias was reused.
        kKeyValueFormattingError = 0x6,   ///< Malformed key/value sequence.
        kTooManyRequests = 0x7,           ///< Request limit exceeded.
        kInvalidPath = 0x8,               ///< Path is invalid.
        kMalformedPath = 0x9,             ///< Path encoding is malformed.
        kGoawayTimeout = 0x10,            ///< GOAWAY timed out.
        kControlMessageTimeout = 0x11,    ///< Control-message timeout.
        kDataStreamTimeout = 0x12,        ///< Data-stream timeout.
        kAuthTokenCacheOverflow = 0x13,   ///< Authorization-token cache is full.
        kDuplicateAuthTokenAlias = 0x14,  ///< Authorization-token alias was reused.
        kVersionNegotiationFailed = 0x15, ///< No mutually supported version.
        kMalformedAuthToken = 0x16,       ///< Authorization token is malformed.
        kUnknownAuthTokenAlias = 0x17,    ///< Authorization-token alias is unknown.
        kExpiredAuthToken = 0x18,         ///< Authorization token expired.
        kInvalidAuthority = 0x19,         ///< Authority is invalid.
        kMalformedAuthority = 0x1A,       ///< Authority encoding is malformed.
    };

    /// Data-stream reset error codes.
    enum class StreamResetError : uint64_t
    {
        kInternalError = 0x0,       ///< Internal failure.
        kCancelled = 0x1,           ///< Delivery was cancelled.
        kDeliveryTimeout = 0x2,     ///< Delivery timed out.
        kSessionClosed = 0x3,       ///< Session closed.
        kUnknownObjectStatus = 0x4, ///< Unknown object status.
        kMalformedTrack = 0x12,     ///< Track data is malformed.
    };

    /// Known track/object extension type codes.
    enum class ExtensionType : uint64_t
    {
        // Track Scope Extensions
        kDeliveryTimeout = 0x02,            ///< Delivery timeout.
        kMaxCacheDuration = 0x04,           ///< Maximum cache duration.
        kDefaultPublisherPriority = 0x0E,   ///< Default publisher priority.
        kDefaultPublisherGroupOrder = 0x22, ///< Default publisher group order.
        kDynamicGroups = 0x30,              ///< Dynamic-groups flag.

        // Object Scope Extensions
        kPriorGroupIdGap = 0x3C,  ///< Gap from the prior group identifier.
        kPriorObjectIdGap = 0x3E, ///< Gap from the prior object identifier.

        // Dual Scope (Track and Object) Extensions
        kImmutable = 0x0B, ///< Container for immutable nested extension KVPs.
    };

    /**
     * Types with matching insertion into Bytes and extraction from BytesSpan.
     * @tparam T Candidate type.
     */
    template<class T>
    concept HasByteStreamOperators = requires(T& value, Bytes& bytes) {
        { bytes << value } -> std::same_as<Bytes&>;
        { BytesSpan{} >> value } -> std::same_as<BytesSpan>;
    };

    /**
     * Types representable by one raw parameter byte.
     * @tparam T Candidate integral or byte-backed enum type.
     */
    template<class T>
    concept ByteParameter = std::is_convertible_v<T, std::uint8_t> ||
                            (std::is_enum_v<T> && std::same_as<std::underlying_type_t<T>, std::uint8_t>);

    /// Draft 18 flat key-value pairs: a bounded, sorted KVP sequence without a leading count.
    class KeyValuePairs
    {
      public:
        /// Creates an empty sequence.
        KeyValuePairs() = default;

        /** @param pairs Absolute keys and their stored value bytes. */
        KeyValuePairs(const std::map<std::uint64_t, Bytes>& pairs)
          : pairs_(pairs)
        {
        }

        /**
         * Encodes and replaces a value by key.
         * @tparam T Value type.
         * @tparam K uint64-compatible key type.
         * @return This collection.
         */
        template<typename T, KeyType K>
        KeyValuePairs& Add(K type, const T& value)
        {
            const auto type_value = static_cast<std::uint64_t>(type);
            pairs_[type_value] = EncodeValue(type_value, value);
            return *this;
        }

        /**
         * Decodes a required value.
         * @tparam T Requested result type.
         * @tparam K Key type.
         * @param type Key to retrieve.
         * @return Decoded value.
         * @throws std::out_of_range if the key is absent.
         */
        template<typename T, KeyType K>
        T Get(K type) const
        {
            if constexpr (std::is_arithmetic_v<T>) {
                if (static_cast<std::uint64_t>(type) % 2 == 0) {
                    std::uint64_t val = 0;
                    const auto& bytes = pairs_.at(static_cast<std::uint64_t>(type));
                    std::memcpy(&val, bytes.data(), std::min(bytes.size(), sizeof(val)));
                    return static_cast<T>(val);
                }
            }

            if constexpr (HasByteStreamOperators<T>) {
                T result;
                pairs_.at(static_cast<std::uint64_t>(type)) >> result;
                return result;
            }

            return FromBytes<T>(pairs_.at(static_cast<std::uint64_t>(type)));
        }

        /// @tparam T Decoded value type.
        /// @tparam K Parameter-key type.
        /// @param type Key to decode.
        /// @return Decoded @p type, or std::nullopt when absent.
        template<typename T, KeyType K>
        std::optional<T> GetOptional(K type) const
        {
            return pairs_.contains(static_cast<std::uint64_t>(type)) ? std::make_optional(Get<T>(type)) : std::nullopt;
        }

        /// @param type Numeric key to find.
        /// @return Whether numeric key @p type exists.
        bool Contains(std::uint64_t type) const { return pairs_.contains(type); }

        /// @return Const iterator to the first pair.
        auto begin() const noexcept { return pairs_.begin(); }

        /// @return Const iterator past the final pair.
        auto end() const noexcept { return pairs_.end(); }

        /// @return Read-only reference to the sorted underlying map.
        const std::map<std::uint64_t, Bytes>& Pairs() const noexcept { return pairs_; }

        /**
         * Converts a value to the internal KVP byte representation.
         * @tparam T Value type.
         * @param type Absolute key; parity controls integer versus byte encoding.
         * @param value Value to encode.
         * @return Owned bytes.
         */
        template<typename T>
        static Bytes EncodeValue(std::uint64_t type, const T& value)
        {
            if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>) {
                if (type % 2 == 0) {
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

      private:
        std::map<std::uint64_t, Bytes> pairs_;
    };

    /**
     * Parses KVPs through the end of the input span.
     * @param buffer Input span.
     * @param msg Destination replaced with parsed KVPs.
     * @return An empty suffix.
     */
    BytesSpan operator>>(BytesSpan buffer, KeyValuePairs& msg);

    /**
     * Appends sorted, delta-keyed KVPs without a count or outer length.
     * @param buffer Destination bytes.
     * @param msg KVPs to serialize.
     * @return @p buffer.
     */
    Bytes& operator<<(Bytes& buffer, const KeyValuePairs& msg);

    /// Mutable and immutable track-scoped extension maps.
    class TrackExtensions
    {
      public:
        /// Creates empty extension maps.
        TrackExtensions() = default;

        /** @param ext Initial mutable extension values. */
        TrackExtensions(const std::map<std::uint64_t, Bytes>& ext)
          : extensions(ext)
        {
        }

      public:
        /**
         * Encodes and replaces a mutable extension.
         * @tparam T Value type.
         * @param type Extension key.
         * @param value Value to encode.
         * @throws std::invalid_argument if @p type is kImmutable.
         * @return This collection.
         */
        template<typename T>
        TrackExtensions& Add(ExtensionType type, const T& value)
        {
            if (type == ExtensionType::kImmutable) {
                throw std::invalid_argument("ExtensionType::kImmutable should not be used directly, use AddImmutable "
                                            "with the keytype you want to use");
            }

            extensions[static_cast<std::uint64_t>(type)] =
              KeyValuePairs::EncodeValue(static_cast<std::uint64_t>(type), value);

            return *this;
        }

        /**
         * Adds a mutable extension only when present.
         * @tparam T Value type.
         * @param type Extension key.
         * @param value Optional value.
         * @return This collection.
         */
        template<typename T>
        TrackExtensions& AddOptional(ExtensionType type, const std::optional<T>& value)
        {
            if (value.has_value()) {
                Add<T>(type, value.value());
            }
            return *this;
        }

        /**
         * Encodes and replaces an immutable extension.
         * @tparam T Value type.
         * @param type Nested extension key.
         * @param value Value to encode.
         * @return This collection.
         */
        template<typename T>
        TrackExtensions& AddImmutable(ExtensionType type, const T& value)
        {
            immutable_extensions[static_cast<std::uint64_t>(type)] =
              KeyValuePairs::EncodeValue(static_cast<std::uint64_t>(type), value);
            return *this;
        }

        /**
         * Decodes a required mutable extension.
         * @tparam T Requested result type.
         * @param type Extension key.
         * @return Decoded value.
         * @throws std::out_of_range if absent.
         */
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

        /**
         * @tparam T Requested result type.
         * @param type Extension key.
         * @return Decoded mutable extension, or std::nullopt when absent.
         */
        template<typename T>
        std::optional<T> GetOptional(ExtensionType type) const
        {
            return extensions.contains(static_cast<std::uint64_t>(type)) ? std::make_optional(Get<T>(type))
                                                                         : std::nullopt;
        }

        /**
         * Decodes a required immutable extension.
         * @tparam T Requested result type.
         * @param type Nested extension key.
         * @return Decoded value.
         * @throws std::out_of_range if absent.
         */
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

        /**
         * @tparam T Requested result type.
         * @param type Nested extension key.
         * @return Decoded immutable extension, or std::nullopt when absent.
         */
        template<typename T>
        std::optional<T> GetImmutableOptional(ExtensionType type) const
        {
            return immutable_extensions.contains(static_cast<std::uint64_t>(type))
                     ? std::make_optional(GetImmutable<T>(type))
                     : std::nullopt;
        }

        /// @return Const iterator to the first mutable extension.
        auto begin() const noexcept { return extensions.begin(); }

        /// @return Const iterator past the last mutable extension.
        auto end() const noexcept { return extensions.end(); }

        std::map<std::uint64_t, Bytes> extensions;           ///< Mutable extensions keyed by absolute type.
        std::map<std::uint64_t, Bytes> immutable_extensions; ///< Immutable nested extensions keyed by absolute type.
    };

    /**
     * Parses KVPs through the end of the input span.
     * @param buffer Input span.
     * @param msg Destination whose extension maps are replaced.
     * @return An empty suffix.
     */
    BytesSpan operator>>(BytesSpan buffer, TrackExtensions& msg);

    /**
     * Appends mutable KVPs plus an immutable container KVP.
     * @param buffer Destination bytes.
     * @param msg Extensions to serialize.
     * @return @p buffer.
     */
    Bytes& operator<<(Bytes& buffer, const TrackExtensions& msg);

} // namespace
