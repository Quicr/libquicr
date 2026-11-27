// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/messages/ctrl_message_types.h"

#include <algorithm>
#include <initializer_list>
#include <limits>
#include <map>
#include <vector>

namespace quicr::messages {

    /// Message-parameter type codes used as delta-encoded map keys on the wire.
    enum struct ParameterType : uint64_t
    {
        // Delivery and Timeouts
        kDeliveryTimeout = 0x02,         ///< Delivery timeout, encoded as an unsigned varint.
        kAuthorizationToken = 0x03,      ///< Sensitive opaque token bytes; never log the value.
        kRendezvousTimeout = 0x04,       ///< Rendezvous timeout, encoded as an unsigned varint.
        kSubgroupDeliveryTimeout = 0x06, ///< Subgroup delivery timeout, encoded as an unsigned varint.
        kFillTimeout = 0x0A,             ///< Fill timeout, encoded as an unsigned varint.

        // Subscription and Track State
        kExpires = 0x08,       ///< Expiry value; zero is resolved as absent.
        kLargestObject = 0x09, ///< Group/object location encoded as two unsigned varints.
        kForward = 0x10,       ///< One-byte Boolean forwarding flag.

        // Priority and Filtering
        kSubscriberPriority = 0x20, ///< One-byte subscriber priority.
        kSubscriptionFilter = 0x21, ///< Length-prefixed location-filter bytes.
        kGroupOrder = 0x22,         ///< One-byte GroupOrder value.

        // Dynamic Group Management
        kNewGroupRequest = 0x32,      ///< New-group request encoded as an unsigned varint.
        kTrackNamespacePrefix = 0x34, ///< Length-prefixed opaque namespace-prefix bytes.

        // Filters
        kTrackFilter = 0x29, ///< Length-prefixed track filter bytes.

        /*===================================================================*/
        // Internal Use
        /*===================================================================*/

        kInvalid = 0xFF, ///< Internal sentinel; not a valid wire parameter.
    };

    /// A message parameter key/value pair.
    using Parameter = KeyValuePair<ParameterType>;

    /// A setup-option key/value pair.
    using SetupParameter = KeyValuePair<SetupOptionType>;

    /// @param type Filter type to convert.
    /// @return Parameter type corresponding to @p type, or kInvalid for kNone.
    constexpr ParameterType ToParameterFilterType(FilterType type)
    {
        switch (type) {
            case FilterType::kSubscriptionFilter:
                return ParameterType::kSubscriptionFilter;
            case FilterType::kTrackFilter:
                return ParameterType::kTrackFilter;
            default:
                return ParameterType::kInvalid;
        }
    }

    /**
     * Converts a filter parameter key to its filter discriminator.
     * @param type Parameter key to convert.
     * @return Corresponding filter type.
     * @throws std::invalid_argument if @p type is not a filter parameter.
     */
    constexpr FilterType ToFilterType(ParameterType type)
    {
        switch (type) {
            case ParameterType::kSubscriptionFilter:
                return FilterType::kSubscriptionFilter;
            case ParameterType::kTrackFilter:
                return FilterType::kTrackFilter;

            default:
                throw std::invalid_argument("parameter type is not a valid filter type");
        }
    }

    /// @param filter Filter to inspect.
    /// @return Parameter key corresponding to the active alternative in @p filter.
    inline ParameterType GetFilterParameterType(const Filter& filter)
    {
        return ToParameterFilterType(GetFilterType(filter));
    }

    /**
     * Serializes a filter into a parameter value.
     * @param filter_type Wire discriminator used for the parameter key.
     * @param filter Active filter value. No validation ensures it matches @p filter_type.
     * @return Parameter containing the serialized filter, or an empty value for monostate.
     */
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

    /// Physical value encoding selected by a parameter type.
    enum class ParameterEncoding
    {
        kByte,     ///< Exactly one raw byte.
        kVarint,   ///< One unsigned variable-length integer.
        kLocation, ///< Two unsigned variable-length integers.
        kBytes     ///< Unsigned-varint length followed by bytes.
    };

    /// Wire properties of a parameter type.
    struct ParameterSpec
    {
        ParameterEncoding encoding; ///< Value encoding.
        bool repeatable;            ///< True if there can be multiple instances of this type.
    };

    /**
     * Look up the wire encoding for a Message Parameter type.
     * @param type The parameter type.
     * @return The encoding it uses.
     * @throws ProtocolViolationException if the type is unknown.
     */
    inline ParameterSpec GetParameterSpec(ParameterType type)
    {
        switch (type) {
            case ParameterType::kDeliveryTimeout:
            case ParameterType::kRendezvousTimeout:
            case ParameterType::kSubgroupDeliveryTimeout:
            case ParameterType::kFillTimeout:
            case ParameterType::kExpires:
            case ParameterType::kNewGroupRequest:
                return { ParameterEncoding::kVarint, false };
            case ParameterType::kAuthorizationToken:
                return { ParameterEncoding::kBytes, true };
            case ParameterType::kLargestObject:
                return { ParameterEncoding::kLocation, false };
            case ParameterType::kForward:
            case ParameterType::kSubscriberPriority:
            case ParameterType::kGroupOrder:
                return { ParameterEncoding::kByte, false };
            case ParameterType::kSubscriptionFilter:
            case ParameterType::kTrackNamespacePrefix:
            case ParameterType::kTrackFilter:
                return { ParameterEncoding::kBytes, false };
            case ParameterType::kInvalid:
                break;
        }
        throw ProtocolViolationException(
          "Unknown Message Parameter type (type=" + std::to_string(static_cast<std::uint64_t>(type)) + ")");
    }

    /**
     * Types accepted by typed parameter insertion and retrieval.
     * @tparam T Candidate value type.
     */
    template<typename T>
    concept ParameterValueType =
      HasByteStreamOperators<std::decay_t<T>> || ByteParameter<std::decay_t<T>> ||
      std::is_same_v<std::decay_t<T>, Location> || std::is_same_v<std::decay_t<T>, Bytes> || requires(T v) {
          { UintVar(v) };
      };

    /**
     * Sorted parameter collection with type-directed wire encoding.
     * @tparam Type uint64-compatible parameter-key enum or integer type.
     */
    template<typename Type = ParameterType>
        requires std::is_convertible_v<Type, std::uint64_t> ||
                 std::is_same_v<std::underlying_type_t<Type>, std::uint64_t>
    class ParameterList
    {
      public:
        /// Creates an empty list.
        ParameterList() = default;

        /// Copies a list.
        /// @param other List to copy.
        ParameterList(const ParameterList& other) = default;

        /// Moves a list.
        /// @param other List to move.
        ParameterList(ParameterList&& other) = default;

        /// Copy-assigns a list.
        /// @param other List to copy.
        /// @return This list.
        ParameterList& operator=(const ParameterList& other) = default;

        /// Move-assigns a list.
        /// @param other List to move.
        /// @return This list.
        ParameterList& operator=(ParameterList&& other) = default;

        /**
         * Encode a parameter into the list.
         * @tparam T Value type accepted by the selected encoding.
         * @param type Parameter key that determines the wire encoding.
         * @param value Value to encode.
         * @return This list.
         * @throws std::invalid_argument if @p type is not repeatable and already has a value.
         * @throws ProtocolViolationException if the key is unknown or the value type is incompatible.
         */
        template<typename T>
            requires HasByteStreamOperators<T> || std::is_convertible_v<T, std::uint8_t> ||
                     std::is_same_v<T, Location> || requires(T v) {
                         { UintVar(v) };
                     }
        ParameterList& Add(Type type, const T& value)
        {
            const auto key = static_cast<std::uint64_t>(type);
            const auto spec = GetParameterSpec(type);
            if (!spec.repeatable && parameters.contains(key)) {
                throw std::invalid_argument("ParameterList already has a value for this non-repeating type");
            }
            parameters.emplace(key, Encode(spec.encoding, key, value));

            return *this;
        }

        /**
         * Adds an optional value when present.
         * @tparam T Parameter value type.
         * @param type Parameter key.
         * @param value Optional value.
         * @return This list.
         */
        template<ParameterValueType T>
        ParameterList& AddOptional(Type type, const std::optional<T>& value)
        {
            if (value.has_value()) {
                Add<T>(type, value.value());
            }

            return *this;
        }

        /// @return Const iterator to the first numeric-key/value entry.
        auto begin() const noexcept { return parameters.begin(); }

        /// @return Const iterator past the last entry.
        auto end() const noexcept { return parameters.end(); }

        /// @param type Parameter key to find.
        /// @return Whether @p type has a stored map entry.
        bool Contains(Type type) const { return parameters.contains(static_cast<std::uint64_t>(type)); }

        /**
         * Decodes the value of a non-repeatable parameter key.
         * @tparam T Requested value type.
         * @param type Parameter key controlling decoding.
         * @return Decoded value, or std::nullopt when absent.
         * @throws std::logic_error if @p type is repeatable; use GetAll().
         * @throws ProtocolViolationException for empty values, unknown keys, or incompatible requested types.
         * @throws UintVarInvalidArgException for malformed varints.
         */
        template<ParameterValueType T>
        std::optional<T> Get(Type type) const
        {
            const auto spec = GetParameterSpec(type);
            if (spec.repeatable) {
                throw std::logic_error("Get() is only for non-repeatable types");
            }
            const auto key = static_cast<std::uint64_t>(type);
            const auto it = parameters.find(key);
            if (it == parameters.end()) {
                return std::nullopt;
            }
            return Decode<T>(spec.encoding, key, it->second);
        }

        /**
         * Decodes every value of a repeatable parameter key, in insertion order.
         * @tparam T Requested value type.
         * @param type Parameter key controlling decoding.
         * @return Decoded values; empty when absent.
         * @throws std::logic_error if @p type is not repeatable; use Get().
         * @throws ProtocolViolationException for empty values, unknown keys, or incompatible requested types.
         * @throws UintVarInvalidArgException for malformed varints.
         */
        template<ParameterValueType T>
        std::vector<T> GetAll(Type type) const
        {
            const auto spec = GetParameterSpec(type);
            if (!spec.repeatable) {
                throw std::logic_error("GetAll() is only for repeatable types");
            }

            const auto key = static_cast<std::uint64_t>(type);
            const auto [first, last] = parameters.equal_range(key);
            if (first == last) {
                return {};
            }

            std::vector<T> values;
            for (auto it = first; it != last; ++it) {
                values.push_back(Decode<T>(spec.encoding, key, it->second));
            }
            return values;
        }

        /**
         * Decodes a filter parameter.
         * @param type Filter discriminator.
         * @return Decoded filter, or monostate when absent, unsupported, or this is not a ParameterType list.
         * @note Subgroup, object, priority, and property filter decoding is currently unsupported.
         */
        Filter GetFilter(FilterType type) const
        {
            if constexpr (!std::is_same_v<ParameterType, Type>) {
                return std::monostate{};
            }

            const auto bytes = Get<Bytes>(ToParameterFilterType(type));
            if (!bytes.has_value()) {
                return std::monostate{};
            }
            return DeserializeFilter(type, *bytes);
        }

        /// Compares entries in key order.
        /// @param other List to compare.
        /// @return Lexicographic comparison of the entries.
        auto operator<=>(const ParameterList& other) const = default;

        /**
         * Parses a counted, delta-keyed parameter list.
         * @param buffer Input span passed by value.
         * @param msg Destination list.
         * @return Unconsumed suffix of @p buffer.
         * @throws ProtocolViolationException for unknown types, a type delta past 2^64-1, a repeat
         * of a non-repeatable type, or a length-prefixed value longer than the remaining input.
         * @throws std::invalid_argument (including UintVarInvalidArgException) for other truncated input.
         */
        friend BytesSpan operator>>(BytesSpan buffer, ParameterList& msg)
        {
            std::uint64_t count = 0;
            buffer = buffer >> count;

            std::multimap<std::uint64_t, Bytes> parameters;
            std::uint64_t type = 0;
            for (std::uint64_t i = 0; i < count; ++i) {
                std::uint64_t delta = 0;
                buffer = buffer >> delta;
                if (delta > std::numeric_limits<std::uint64_t>::max() - type) {
                    throw ProtocolViolationException("Parameter type delta overflows");
                }
                type += delta;

                const auto spec = GetParameterSpec(static_cast<Type>(type));
                if (delta == 0 && !parameters.empty() && !spec.repeatable) {
                    throw ProtocolViolationException("Unexpected duplicate parameter");
                }

                Bytes value;
                switch (spec.encoding) {
                    case ParameterEncoding::kByte: {
                        std::uint8_t byte = 0;
                        buffer = buffer >> byte;
                        value.push_back(byte);
                        break;
                    }
                    case ParameterEncoding::kVarint: {
                        UintVar uval(buffer);
                        value << uval;
                        buffer = buffer.subspan(uval.size());
                        break;
                    }
                    case ParameterEncoding::kLocation: {
                        Location loc{};
                        buffer = buffer >> loc;
                        value << loc;
                        break;
                    }
                    case ParameterEncoding::kBytes: {
                        buffer = buffer >> value;
                        break;
                    }
                }
                parameters.emplace_hint(parameters.end(), type, std::move(value));
            }

            msg.parameters = std::move(parameters);
            return buffer;
        }

        /**
         * Appends a counted, sorted, delta-keyed parameter list.
         * @param buffer Destination byte vector.
         * @param msg List to serialize.
         * @return @p buffer.
         */
        friend Bytes& operator<<(Bytes& buffer, const ParameterList& msg)
        {
            buffer << UintVar(msg.parameters.size());

            std::uint64_t prev_type = 0;
            for (const auto& [type, value] : msg.parameters) {
                buffer << UintVar(type - prev_type);
                prev_type = type;

                switch (GetParameterSpec(static_cast<Type>(type)).encoding) {
                    case ParameterEncoding::kByte:
                        buffer << value.front();
                        break;
                    case ParameterEncoding::kVarint:
                        [[fallthrough]];
                    case ParameterEncoding::kLocation:
                        buffer.insert(buffer.end(), value.begin(), value.end());
                        break;
                    case ParameterEncoding::kBytes:
                        buffer << value;
                        break;
                }
            }

            return buffer;
        }

      private:
        template<typename T>
        static Bytes Encode(ParameterEncoding encoding, std::uint64_t key, const T& value)
        {
            Bytes bytes;
            switch (encoding) {
                case ParameterEncoding::kByte:
                    if constexpr (ByteParameter<T>) {
                        bytes.push_back(static_cast<std::uint8_t>(value));
                        return bytes;
                    } else {
                        throw ProtocolViolationException(
                          "Given parameter type must be u8 (type=" + std::to_string(key) + ")");
                    }
                case ParameterEncoding::kVarint:
                    if constexpr (requires { UintVar(value); }) {
                        bytes << UintVar(value);
                        return bytes;
                    } else {
                        throw ProtocolViolationException(
                          "Given parameter type must be uvarint(u64) (type=" + std::to_string(key) + ")");
                    }
                default:
                    if constexpr (std::is_same_v<T, Bytes> || std::is_same_v<T, BytesSpan>) {
                        bytes.assign(value.begin(), value.end());
                    } else if constexpr (HasByteStreamOperators<T>) {
                        bytes << value;
                    } else {
                        throw ProtocolViolationException(
                          "Given parameter type must be Location or Bytes (type=" + std::to_string(key) + ")");
                    }
                    return bytes;
            }
        }

        template<typename T>
        static T Decode(ParameterEncoding encoding, std::uint64_t key, BytesSpan bytes)
        {
            if (bytes.empty()) {
                throw ProtocolViolationException("Parameter value must not be empty");
            }

            switch (encoding) {
                case ParameterEncoding::kByte:
                    if constexpr (ByteParameter<T>) {
                        return static_cast<T>(bytes[0]);
                    } else {
                        throw ProtocolViolationException(
                          "Given parameter type must be u8 (type=" + std::to_string(key) + ")");
                    }
                case ParameterEncoding::kVarint:
                    if constexpr (requires(T v) { UintVar(v); }) {
                        return static_cast<T>(UintVar(bytes).Get());
                    } else {
                        throw ProtocolViolationException(
                          "Given parameter type must be uvarint(u64) (type=" + std::to_string(key) + ")");
                    }
                default:
                    if constexpr (std::is_same_v<T, Bytes> || std::is_same_v<T, BytesSpan>) {
                        return T(bytes.begin(), bytes.end());
                    } else if constexpr (HasByteStreamOperators<T>) {
                        T result;
                        bytes >> result;
                        return result;
                    } else {
                        throw ProtocolViolationException(
                          "Given parameter type must be Location or Bytes (type=" + std::to_string(key) + ")");
                    }
            }
        }

        std::multimap<std::uint64_t, Bytes> parameters;
    };

    /// Standard message-parameter list.
    using Parameters = quicr::messages::ParameterList<quicr::messages::ParameterType>;

    /**
     * Ensures every stored key is allowed.
     * @param params Parameters to inspect.
     * @param allowed Allowed parameter types.
     * @throws ProtocolViolationException for a disallowed type.
     */
    inline void ValidateParameters(const Parameters& params, std::initializer_list<ParameterType> allowed)
    {
        for (const auto& [type_value, value] : params) {
            const auto type = static_cast<ParameterType>(type_value);
            if (std::ranges::find(allowed, type) == allowed.end()) {
                throw ProtocolViolationException("Parameter not valid for this message type");
            }
        }
    }

    /**
     * Resolves the optional FORWARD byte.
     * @param params Parameters to inspect.
     * @param default_value Value to return when FORWARD is absent.
     * @return @p default_value when absent; otherwise the encoded Boolean.
     * @throws ProtocolViolationException unless the encoded value is zero or one.
     */
    inline bool ResolveForward(const Parameters& params, bool default_value)
    {
        if (!params.Contains(ParameterType::kForward)) {
            return default_value;
        }
        const auto value = params.Get<std::uint8_t>(ParameterType::kForward);
        if (value > 1) {
            throw ProtocolViolationException("FORWARD parameter must be 0 or 1");
        }
        return value == 1;
    }

    /**
     * Resolves and validates GROUP_ORDER.
     * @param params Parameters to inspect.
     * @return Valid order, or std::nullopt when absent.
     * @throws ProtocolViolationException for any other byte value.
     */
    inline std::optional<GroupOrder> ResolveGroupOrder(const Parameters& params)
    {
        const auto value = params.Get<std::uint8_t>(ParameterType::kGroupOrder);
        if (!value.has_value()) {
            return std::nullopt;
        }
        if (value != static_cast<std::uint8_t>(GroupOrder::kAscending) &&
            value != static_cast<std::uint8_t>(GroupOrder::kDescending)) {
            throw ProtocolViolationException("GROUP_ORDER parameter must be Ascending or Descending");
        }
        return static_cast<GroupOrder>(*value);
    }

    /// @param params Parameters to inspect.
    /// @return EXPIRES when present and nonzero; otherwise std::nullopt.
    inline std::optional<std::uint64_t> ResolveExpires(const Parameters& params)
    {
        const auto expires = params.Get<std::uint64_t>(ParameterType::kExpires);
        return (expires.has_value() && expires.value() != 0) ? expires : std::nullopt;
    }

    // Filters.
    /// Filter kinds searched by ContainsAnyFilter() and ResolveFilter(), in precedence order.
    inline constexpr FilterType kFilterTypes[] = {
        FilterType::kSubscriptionFilter,
        FilterType::kTrackFilter,
    };

    /// @param params Parameters to inspect.
    /// @return Whether any known filter parameter key is present.
    inline bool ContainsAnyFilter(const Parameters& params)
    {
        return std::ranges::any_of(
          kFilterTypes, [&](const auto filter_type) { return params.Contains(ToParameterFilterType(filter_type)); });
    }

    /**
     * Resolves the first present filter according to kFilterTypes order.
     * @param params Parameters to inspect.
     * @return Decoded filter, or monostate when none is present or its decoder is unsupported.
     */
    inline Filter ResolveFilter(const Parameters& params)
    {
        for (const auto filter_type : kFilterTypes) {
            if (params.Contains(ToParameterFilterType(filter_type))) {
                return params.GetFilter(filter_type);
            }
        }
        return std::monostate{};
    }

} // namespace quicr::messages
