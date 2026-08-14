// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/messages/ctrl_message_types.h"

#include <algorithm>
#include <initializer_list>
#include <vector>

namespace quicr::messages {

    enum struct ParameterType : uint64_t
    {
        // Delivery and Timeouts
        kDeliveryTimeout = 0x02,
        kAuthorizationToken = 0x03,
        kRendezvousTimeout = 0x04,
        kSubgroupDeliveryTimeout = 0x06,
        kFillTimeout = 0x0A,

        // Subscription and Track State
        kExpires = 0x08,
        kLargestObject = 0x09,
        kForward = 0x10,

        // Priority and Filtering
        kSubscriberPriority = 0x20,
        kSubscriptionFilter = 0x21,
        kGroupOrder = 0x22,

        // Dynamic Group Management
        kNewGroupRequest = 0x32,
        kTrackNamespacePrefix = 0x34,

        // Filters
        kSubgroupFilter = 0x25,
        kObjectFilter = 0x26,
        kPriorityFilter = 0x27,
        kPropertyFilter = 0x28,
        kTrackFilter = 0x29,

        /*===================================================================*/
        // Internal Use
        /*===================================================================*/

        kInvalid = 0xFF,
    };

    using Parameter = KeyValuePair<ParameterType>;
    using SetupParameter = KeyValuePair<SetupOptionType>;

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

    inline ParameterType GetFilterParameterType(const Filter& filter)
    {
        return ToParameterFilterType(GetFilterType(filter));
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

    enum class ParameterEncoding
    {
        kByte,
        kVarint,
        kLocation,
        kBytes
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

    template<typename T>
    concept ParameterValueType =
      HasByteStreamOperators<std::decay_t<T>> || ByteParameter<std::decay_t<T>> ||
      std::is_same_v<std::decay_t<T>, Location> || std::is_same_v<std::decay_t<T>, Bytes> || requires(T v) {
          { UintVar(v) };
      };

    template<typename Type = ParameterType>
        requires std::is_convertible_v<Type, std::uint64_t> ||
                 std::is_same_v<std::underlying_type_t<Type>, std::uint64_t>
    class ParameterList
    {
      public:
        ParameterList() = default;
        ParameterList(const ParameterList&) = default;
        ParameterList(ParameterList&&) = default;
        ParameterList& operator=(const ParameterList&) = default;
        ParameterList& operator=(ParameterList&&) = default;

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

        template<ParameterValueType T>
        ParameterList& AddOptional(Type type, const std::optional<T>& value)
        {
            if (value.has_value()) {
                Add<T>(type, value.value());
            }

            return *this;
        }

        auto begin() const noexcept { return parameters.begin(); }
        auto end() const noexcept { return parameters.end(); }

        bool Contains(Type type) const { return parameters.contains(static_cast<std::uint64_t>(type)); }

        BytesSpan Find(Type type) const
        {
            auto it = parameters.find(static_cast<std::uint64_t>(type));
            if (it == parameters.end()) {
                return {};
            }

            return it->second;
        }

        void Remove(Type type) { parameters.erase(static_cast<std::uint64_t>(type)); }

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

        template<ParameterValueType T>
        std::optional<T> GetOptional(Type type) const
        {
            return Contains(type) ? std::make_optional(Get<T>(type)) : std::nullopt;
        }

        auto operator<=>(const ParameterList&) const = default;

        std::map<std::uint64_t, Bytes> parameters;
    };

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

    using Parameters = quicr::messages::ParameterList<quicr::messages::ParameterType>;

    /// Must all be known, must not be duplicated (except auth).
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

    inline std::optional<std::uint64_t> ResolveExpires(const Parameters& params)
    {
        const auto expires = params.GetOptional<std::uint64_t>(ParameterType::kExpires);
        return (expires.has_value() && expires.value() != 0) ? expires : std::nullopt;
    }

    // Filters.
    inline constexpr FilterType kFilterTypes[] = {
        FilterType::kSubscriptionFilter, FilterType::kSubgroupFilter, FilterType::kObjectFilter,
        FilterType::kPriorityFilter,     FilterType::kPropertyFilter, FilterType::kTrackFilter,
    };
    inline bool ContainsAnyFilter(const Parameters& params)
    {
        return std::ranges::any_of(
          kFilterTypes, [&](const auto filter_type) { return params.Contains(ToParameterFilterType(filter_type)); });
    }
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
