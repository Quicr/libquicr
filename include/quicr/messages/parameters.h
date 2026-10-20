// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/messages/ctrl_message_types.h"

#include <algorithm>
#include <initializer_list>
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
        kSubgroupFilter = 0x25, ///< Length-prefixed subgroup-range filter bytes.
        kObjectFilter = 0x26,   ///< Length-prefixed object-range filter bytes.
        kPriorityFilter = 0x27, ///< Length-prefixed priority-range filter bytes.
        kPropertyFilter = 0x28, ///< Length-prefixed property filter bytes.
        kTrackFilter = 0x29,    ///< Length-prefixed track filter bytes.

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

    /**
     * Look up the wire encoding for a Message Parameter type.
     * @param type The parameter type.
     * @return The encoding it uses.
     * @throws ProtocolViolationException if the type is unknown.
     */
    inline ParameterEncoding GetParameterEncoding(ParameterType type)
    {
        static const std::map<ParameterType, ParameterEncoding> encodings = {
            { ParameterType::kDeliveryTimeout, ParameterEncoding::kVarint },
            { ParameterType::kAuthorizationToken, ParameterEncoding::kBytes },
            { ParameterType::kRendezvousTimeout, ParameterEncoding::kVarint },
            { ParameterType::kSubgroupDeliveryTimeout, ParameterEncoding::kVarint },
            { ParameterType::kExpires, ParameterEncoding::kVarint },
            { ParameterType::kLargestObject, ParameterEncoding::kLocation },
            { ParameterType::kFillTimeout, ParameterEncoding::kVarint },
            { ParameterType::kForward, ParameterEncoding::kByte },
            { ParameterType::kSubscriberPriority, ParameterEncoding::kByte },
            { ParameterType::kGroupOrder, ParameterEncoding::kByte },
            { ParameterType::kNewGroupRequest, ParameterEncoding::kVarint },
            { ParameterType::kTrackNamespacePrefix, ParameterEncoding::kBytes },
            { ParameterType::kSubscriptionFilter, ParameterEncoding::kBytes },
            { ParameterType::kSubgroupFilter, ParameterEncoding::kBytes },
            { ParameterType::kObjectFilter, ParameterEncoding::kBytes },
            { ParameterType::kPriorityFilter, ParameterEncoding::kBytes },
            { ParameterType::kPropertyFilter, ParameterEncoding::kBytes },
            { ParameterType::kTrackFilter, ParameterEncoding::kBytes },
        };

        const auto it = encodings.find(type);
        if (it == encodings.end()) {
            throw ProtocolViolationException(
              "Unknown Message Parameter type (type=" + std::to_string(static_cast<std::uint64_t>(type)) + ")");
        }
        return it->second;
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
     *
     * The map representation permits only one stored value per numeric key;
     * adding the same type appends to that value rather than creating a second
     * map entry. Consequently, duplicate authorization-token parameters are
     * not represented independently by this class.
     *
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
         * Encodes and appends a value under a parameter key.
         * @tparam T Value type accepted by the selected encoding.
         * @param type Parameter key that determines the wire encoding.
         * @param value Value to append to the key's stored bytes.
         * @return This list.
         * @throws ProtocolViolationException if the key is unknown or the value type is incompatible.
         */
        template<typename T>
            requires HasByteStreamOperators<T> || std::is_convertible_v<T, std::uint8_t> ||
                     std::is_same_v<T, Location> || requires(T v) {
                         { UintVar(v) };
                     }
        ParameterList& Add(Type type, const T& value)
        {
            const std::uint64_t key = static_cast<std::uint64_t>(type);
            switch (GetParameterEncoding(type)) {
                case ParameterEncoding::kByte:
                    if constexpr (ByteParameter<T>) {
                        parameters[key].push_back(static_cast<std::uint8_t>(value));
                        break;
                    } else {
                        throw ProtocolViolationException(
                          "Given parameter type must be u8 (type=" + std::to_string(key) + ")");
                    }
                case ParameterEncoding::kVarint:
                    if constexpr (requires { UintVar(value); }) {
                        parameters[key] << UintVar(value);
                        break;
                    } else {
                        throw ProtocolViolationException(
                          "Given parameter type must be uvarint(u64) (type=" + std::to_string(key) + ")");
                    }
                default:
                    if constexpr (std::is_same_v<T, Bytes> || std::is_same_v<T, BytesSpan>) {
                        parameters[key].insert(parameters[key].end(), value.begin(), value.end());
                    } else if constexpr (HasByteStreamOperators<T>) {
                        parameters[key] << value;
                    } else {
                        throw ProtocolViolationException(
                          "Given parameter type must be Location or Bytes (type=" + std::to_string(key) + ")");
                    }
                    break;
            }

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
         * Finds encoded value bytes without copying.
         * @param type Parameter key.
         * @return View into this list's storage, or an empty span when absent.
         * The view is invalidated by mutation or destruction of this list.
         */
        BytesSpan Find(Type type) const
        {
            auto it = parameters.find(static_cast<std::uint64_t>(type));
            if (it == parameters.end()) {
                return {};
            }

            return it->second;
        }

        /// @param type Parameter key to remove if present.
        void Remove(Type type) { parameters.erase(static_cast<std::uint64_t>(type)); }

        /**
         * Decodes a typed value.
         * @tparam T Requested value type.
         * @param type Parameter key controlling decoding.
         * @return Decoded value, or a value-initialized T when absent or stored as empty bytes.
         * @throws ProtocolViolationException for unknown keys or incompatible requested types.
         * @throws UintVarInvalidArgException for malformed varints.
         */
        template<ParameterValueType T>
        T Get(Type type) const
        {
            auto bytes = Find(type);
            if (bytes.empty()) {
                return {};
            }

            switch (GetParameterEncoding(type)) {
                case ParameterEncoding::kByte:
                    if constexpr (ByteParameter<T>) {
                        return static_cast<T>(bytes[0]);
                    } else {
                        throw ProtocolViolationException("Given parameter type must be u8 (type=" +
                                                         std::to_string(static_cast<std::uint64_t>(type)) + ")");
                    }
                case ParameterEncoding::kVarint:
                    if constexpr (requires(T v) { UintVar(v); }) {
                        return static_cast<T>(UintVar(bytes).Get());
                    } else {
                        throw ProtocolViolationException("Given parameter type must be uvarint(u64) (type=" +
                                                         std::to_string(static_cast<std::uint64_t>(type)) + ")");
                    }
                default:
                    if constexpr (std::is_same_v<T, Bytes> || std::is_same_v<T, BytesSpan>) {
                        return Bytes{ bytes.begin(), bytes.end() };
                    } else if constexpr (HasByteStreamOperators<T>) {
                        T result;
                        bytes >> result;
                        return result;
                    } else {
                        throw ProtocolViolationException("Given parameter type must be Location or Bytes (type=" +
                                                         std::to_string(static_cast<std::uint64_t>(type)) + ")");
                    }
            }
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

            auto bytes = Find(ToParameterFilterType(type));
            if (bytes.empty()) {
                return {};
            }

            return DeserializeFilter(type, bytes);
        }

        /// @tparam T Requested value type.
        /// @param type Parameter key to decode.
        /// @return Decoded @p type when present, otherwise std::nullopt.
        template<ParameterValueType T>
        std::optional<T> GetOptional(Type type) const
        {
            return Contains(type) ? std::make_optional(Get<T>(type)) : std::nullopt;
        }

        /// Lexicographically compares the underlying sorted maps.
        /// @param other List to compare.
        /// @return Lexicographic comparison of the maps.
        auto operator<=>(const ParameterList& other) const = default;

        std::map<std::uint64_t, Bytes> parameters; ///< Encoded values keyed by absolute numeric parameter type.
    };

    /**
     * Parses a counted, delta-keyed parameter list and advances the span.
     * Existing entries are not cleared; decoded bytes append to matching keys.
     * @tparam Type Parameter-key type.
     * @param buffer Input span passed by value.
     * @param msg Destination list.
     * @return Unconsumed suffix of @p buffer.
     * @throws ProtocolViolationException for unknown parameter types.
     * @throws UintVarInvalidArgException or std::invalid_argument for truncated input.
     */
    template<typename Type = ParameterType>
    BytesSpan operator>>(BytesSpan buffer, ParameterList<Type>& msg)
    {
        std::uint64_t num = 0;
        buffer = buffer >> num;

        std::uint64_t prev_type = 0;
        std::uint64_t delta = 0;
        for (std::uint64_t i = 0; i < num; ++i) {
            buffer = buffer >> delta;

            const std::uint64_t type = prev_type += delta;
            Bytes& value = msg.parameters[type];

            switch (GetParameterEncoding(static_cast<Type>(type))) {
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
        }

        return buffer;
    }

    /**
     * Appends a counted, sorted, delta-keyed parameter list.
     * @tparam Type Parameter-key type.
     * @param buffer Destination byte vector.
     * @param msg List to serialize.
     * @return @p buffer.
     * @throws ProtocolViolationException for unknown parameter types.
     * @warning A present byte-encoded parameter must contain at least one byte.
     */
    template<typename Type = ParameterType>
    Bytes& operator<<(Bytes& buffer, const ParameterList<Type>& msg)
    {
        buffer << UintVar(msg.parameters.size());

        std::uint64_t prev_type = 0;
        for (const auto& [type, value] : msg.parameters) {
            buffer << UintVar(type - prev_type);
            prev_type = type;

            switch (GetParameterEncoding(static_cast<Type>(type))) {
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

    /// Standard message-parameter list.
    using Parameters = quicr::messages::ParameterList<quicr::messages::ParameterType>;

    /**
     * Ensures every stored key is allowed.
     * @param params Parameters to inspect.
     * @param allowed Allowed parameter types.
     * @throws ProtocolViolationException for a disallowed key or duplicate non-token key.
     * @note ParameterList uses a map, so separately encoded duplicate entries have already
     * coalesced by the time this function runs; duplicate detection is therefore limited.
     */
    inline void ValidateParameters(const Parameters& params, std::initializer_list<ParameterType> allowed)
    {
        std::vector<ParameterType> seen;
        for (const auto& [type_value, value] : params) {
            const auto type = static_cast<ParameterType>(type_value);
            if (std::ranges::find(allowed, type) == allowed.end()) {
                throw ProtocolViolationException("Parameter not valid for this message type");
            }

            if (type != ParameterType::kAuthorizationToken) {
                if (std::ranges::find(seen, type) != seen.end()) {
                    throw ProtocolViolationException("Unexpected duplicate parameter");
                }
                seen.push_back(type);
            }
        }
    }

    /**
     * Decodes authorization-token values.
     * @param params Source parameters.
     * @return Decoded tokens.
     * @throws ProtocolViolationException for an invalid token alias discriminator.
     * @warning Token bytes are sensitive opaque data and must not be logged.
     */
    inline std::vector<Token> CollectAuthTokens(const Parameters& params)
    {
        std::vector<Token> tokens;
        for (const auto& [type_value, value] : params) {
            if (static_cast<ParameterType>(type_value) != ParameterType::kAuthorizationToken) {
                continue;
            }
            Token token{};
            const BytesSpan span{ value };
            span >> token;
            tokens.push_back(std::move(token));
        }
        return tokens;
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
        if (!params.Contains(ParameterType::kGroupOrder)) {
            return std::nullopt;
        }
        const auto value = params.Get<std::uint8_t>(ParameterType::kGroupOrder);
        if (value != static_cast<std::uint8_t>(GroupOrder::kAscending) &&
            value != static_cast<std::uint8_t>(GroupOrder::kDescending)) {
            throw ProtocolViolationException("GROUP_ORDER parameter must be Ascending or Descending");
        }
        return static_cast<GroupOrder>(value);
    }

    /// @param params Parameters to inspect.
    /// @return EXPIRES when present and nonzero; otherwise std::nullopt.
    inline std::optional<std::uint64_t> ResolveExpires(const Parameters& params)
    {
        const auto expires = params.GetOptional<std::uint64_t>(ParameterType::kExpires);
        return (expires.has_value() && expires.value() != 0) ? expires : std::nullopt;
    }

    // Filters.
    /// Filter kinds searched by ContainsAnyFilter() and ResolveFilter(), in precedence order.
    inline constexpr FilterType kFilterTypes[] = {
        FilterType::kSubscriptionFilter, FilterType::kSubgroupFilter, FilterType::kObjectFilter,
        FilterType::kPriorityFilter,     FilterType::kPropertyFilter, FilterType::kTrackFilter,
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
