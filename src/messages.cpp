// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/messages/messages.h"

#include <cassert>
#include <limits>

using namespace quicr::messages;

namespace quicr::messages {
    //
    // Utility
    //
    template<class StreamBufferType>
    static bool ParseUintVField(StreamBufferType& buffer, uint64_t& field)
    {
        auto val = buffer.DecodeUintV();
        if (!val) {
            return false;
        }
        field = val.value();
        return true;
    }

    template<class StreamBufferType>
    static bool ReadUintVField(StreamBufferType& buffer, UintVar& field)
    {
        auto val = buffer.ReadUintV();
        if (!val) {
            return false;
        }
        field = val.value();
        return true;
    }

    template<class StreamBufferType>
    static bool ParseBytesField(StreamBufferType& buffer, Bytes& field)
    {
        auto val = buffer.DecodeBytes();
        if (!val) {
            return false;
        }
        field.assign(val->begin(), val->end());
        return true;
    }

    template<class StreamBufferType>
    static bool ParseExtensions(StreamBufferType& buffer,
                                std::optional<std::size_t>& extension_headers_length,
                                std::optional<Extensions>& extensions,
                                std::size_t& extension_bytes_remaining,
                                std::optional<std::uint64_t>& current_header,
                                std::uint64_t& prev_extension_type)
    {
        // Read the length of the extension block, if we haven't already.
        if (!extension_headers_length.has_value()) {
            UintVar length{ 0 };
            if (!ReadUintVField(buffer, length)) {
                return false;
            }
            extension_headers_length = extension_bytes_remaining = length.Get();
        }

        // There are no extensions, so we're done.
        if (*extension_headers_length == 0) {
            return true;
        }

        if (extensions == std::nullopt) {
            extensions = Extensions();
        }

        // Parse KVPs.
        while (extension_bytes_remaining > 0) {
            // Get this KVP's tag, if we can.
            std::uint64_t tag;
            if (current_header.has_value()) {
                // We already have this tag.
                tag = *current_header;
            } else {
                // We're at the start of a KVP.
                UintVar tag_field{ 0 };
                if (!ReadUintVField(buffer, tag_field)) {
                    return false;
                }
                extension_bytes_remaining -= tag_field.size();
                const std::uint64_t delta = tag_field.Get();

                // Check for overflow
                if (delta > std::numeric_limits<std::uint64_t>::max() - prev_extension_type) {
                    throw ProtocolViolationException("Delta encoding overflow: prev_type + delta exceeds 2^64-1");
                }

                tag = prev_extension_type + delta;
                current_header.emplace(tag);
            }

            // Now we're at the data.
            if (tag % 2 == 0) {
                // Even types: single varint value.
                auto val = buffer.ReadUintV();
                if (!val) {
                    return false;
                }
                // Decode the value and place into extensions.
                const UintVar value = *val;
                const std::uint64_t decoded_value = value.Get();
                extension_bytes_remaining -= value.size();
                std::vector<uint8_t> bytes(sizeof(std::uint64_t));
                memcpy(bytes.data(), &decoded_value, sizeof(std::uint64_t));
                (*extensions)[tag].push_back(std::move(bytes));
            } else {
                // Odd types: UIntVar length prefixed bytes.
                Bytes bytes;
                if (!ParseBytesField(buffer, bytes)) {
                    return false;
                }
                extension_bytes_remaining -= bytes.size() + UintVar(bytes.size()).size();
                (*extensions)[tag].push_back(std::move(bytes));
            }
            prev_extension_type = tag;
            current_header = std::nullopt;
        }
        return true;
    }

    static void PushBytes(Bytes& buffer, const Bytes& bytes)
    {
        buffer.insert(buffer.end(), bytes.begin(), bytes.end());
    }

    Bytes& operator<<(Bytes& buffer, const std::optional<Extensions>& extensions)
    {
        if (!extensions.has_value()) {
            // If there are no extensions, write a 0 length.
            // Note: Some MoQ objects (e.g. Datagram) MUST NOT write a 0 length. The caller
            // MUST NOT even attempt to serialize extensions in this case.
            buffer.push_back(0);
            return buffer;
        }
        buffer = buffer << *extensions;
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, const Extensions& extensions)
    {
        // Calculate total length of extension headers
        // std::map is already sorted by key, so deltas will be non-negative.
        // Duplicate keys get 0 deltas by design.
        // TODO: maybe static assert std::map because of ordering.
        std::size_t total_length = 0;
        std::vector<KeyValuePair<std::uint64_t>> kvps;
        std::uint64_t prev_type_for_size = 0;
        for (const auto& [key, values] : extensions) {
            for (const auto& value : values) {
                const auto kvp = KeyValuePair<std::uint64_t>{ static_cast<std::uint64_t>(key), value };
                const auto size = kvp.Size(prev_type_for_size);
                total_length += size;
                prev_type_for_size = key;
                kvps.push_back(kvp);
            }
        }

        // Total length of all extension headers (varint).
        buffer << static_cast<std::uint64_t>(total_length);

        // Write the KVP extensions.
        std::uint64_t prev_type = 0;
        for (const auto& kvp : kvps) {
            SerializeKvp(buffer, kvp, prev_type);
            prev_type = kvp.type;
        }

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, Extensions& extensions)
    {
        std::uint64_t length = 0;
        buffer = buffer >> length;

        std::uint64_t prev_type = 0;
        std::uint64_t bytes_consumed = 0;
        while (bytes_consumed < length) {
            const auto start_pos = buffer.size();
            KeyValuePair<std::uint64_t> kvp;
            ParseKvp(buffer, kvp, prev_type);
            prev_type = kvp.type;
            bytes_consumed += start_pos - buffer.size();

            extensions[kvp.type].emplace_back(std::move(kvp.value));
        }

        return buffer;
    }

    void SerializeExtensions(Bytes& buffer,
                             const std::optional<Extensions>& extensions,
                             const std::optional<Extensions>& immutable_extensions)
    {
        Extensions combined_extensions;

        // Add mutable extensions.
        if (extensions.has_value()) {
            for (const auto& [key, values] : *extensions) {
                combined_extensions[key] = values;
            }
        }

        constexpr auto immutable_key = static_cast<std::uint64_t>(ExtensionType::kImmutable);

        // Serialize immutable extensions in MoQ form, and insert into combined extensions key.
        if (immutable_extensions.has_value() && !immutable_extensions->empty()) {
            // Immutable extensions MUST NOT contain an immutable extension entry.
            if (immutable_extensions->contains(immutable_key)) {
                throw ProtocolViolationException(
                  "An immutable extension header must not contain another immutable extension header");
            }

            // Serialize immutable extensions.
            Bytes immutable_bytes;
            std::uint64_t imm_prev_type = 0;
            for (const auto& [key, values] : *immutable_extensions) {
                for (const auto& value : values) {
                    const auto kvp = KeyValuePair<std::uint64_t>{ key, value };
                    SerializeKvp(immutable_bytes, kvp, imm_prev_type);
                    imm_prev_type = kvp.type;
                }
            }
            combined_extensions[immutable_key].push_back(std::move(immutable_bytes));
        }

        // Serialize combined extensions.
        buffer << combined_extensions;
    }

    template<class StreamBufferType>
    bool ParseExtensions(StreamBufferType& buffer,
                         std::optional<std::size_t>& extension_headers_length,
                         std::optional<Extensions>& extensions,
                         std::optional<Extensions>& immutable_extensions,
                         std::size_t& extension_bytes_remaining,
                         std::optional<std::uint64_t>& current_header,
                         std::uint64_t& prev_extension_type)
    {
        // First, parse all extensions.
        if (!ParseExtensions(buffer,
                             extension_headers_length,
                             extensions,
                             extension_bytes_remaining,
                             current_header,
                             prev_extension_type)) {
            return false;
        }

        constexpr auto immutable_key = static_cast<std::uint64_t>(ExtensionType::kImmutable);

        // Extract immutable extensions if present and deserialize.
        if (extensions.has_value()) {
            const auto it = extensions->find(immutable_key);
            if (it != extensions->end() && !it->second.empty()) {
                // Deserialize the immutable extension map.
                auto stream_buffer = StreamBuffer<uint8_t>();
                stream_buffer.Push(std::span<const uint8_t>(it->second[0]));
                std::optional<std::size_t> immutable_length = it->second[0].size();
                std::size_t immutable_bytes_remaining = it->second[0].size();
                std::optional<std::uint64_t> immutable_current_header;
                std::uint64_t immutable_prev_type = 0;
                if (!ParseExtensions(stream_buffer,
                                     immutable_length,
                                     immutable_extensions,
                                     immutable_bytes_remaining,
                                     immutable_current_header,
                                     immutable_prev_type)) {
                    return false;
                }

                // Validate that immutable extensions don't nest.
                if (immutable_extensions.has_value() && immutable_extensions->contains(immutable_key)) {
                    throw ProtocolViolationException(
                      "Immutable Extensions header contains another Immutable Extensions key");
                }
            }
        }

        return true;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, FetchHeader& msg)
    {
        switch (msg.current_pos) {
            case 0: {
                uint64_t type;
                if (!ParseUintVField(buffer, type)) {
                    return false;
                }
                msg.type = static_cast<FetchHeaderType>(type);
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 1: {
                if (!ParseUintVField(buffer, msg.request_id)) {
                    return false;
                }
                msg.current_pos += 1;
                msg.parse_completed = true;
                [[fallthrough]];
            }
            default:
                break;
        }

        return msg.parse_completed;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, FetchHeader&);

    Bytes& operator<<(Bytes& buffer, const FetchHeader& msg)
    {
        buffer << UintVar(static_cast<uint64_t>(msg.type));
        buffer << UintVar(msg.request_id);
        return buffer;
    }

    FetchSerializationProperties::FetchSerializationProperties(const std::uint64_t value)
      : end_of_range(std::nullopt)
      , subgroup_id_mode(std::nullopt)
      , object_id_delta_present(false)
      , group_id_delta_present(false)
      , priority_present(false)
      , properties_present(false)
      , datagram(false)
    {
        if (value == kEndOfNonExistentRange) {
            end_of_range = EndOfRange::kNonExistent;
            group_id_delta_present = true;
            object_id_delta_present = true;
            return;
        }
        if (value == kEndOfUnknownRange) {
            end_of_range = EndOfRange::kUnknown;
            group_id_delta_present = true;
            object_id_delta_present = true;
            return;
        }
        if (value >= 128) {
            throw ProtocolViolationException("Invalid FETCH serialization flags");
        }

        subgroup_id_mode = static_cast<SubgroupIdMode>(value & kSubgroupMask);
        object_id_delta_present = value & kObjectIdDeltaBit;
        group_id_delta_present = value & kGroupIdDeltaBit;
        priority_present = value & kPriorityBit;
        properties_present = value & kPropertiesBit;
        datagram = value & kDatagramBit;
    }

    FetchSerializationProperties::FetchSerializationProperties(const SubgroupIdMode subgroup_id_mode,
                                                               const bool object_id_delta_present,
                                                               const bool group_id_delta_present,
                                                               const bool priority_present,
                                                               const bool properties_present,
                                                               const bool datagram) noexcept
      : end_of_range(std::nullopt)
      , subgroup_id_mode(datagram ? std::nullopt : std::optional(subgroup_id_mode))
      , object_id_delta_present(object_id_delta_present)
      , group_id_delta_present(group_id_delta_present)
      , priority_present(priority_present)
      , properties_present(properties_present)
      , datagram(datagram)
    {
    }

    std::uint64_t FetchSerializationProperties::GetType() const noexcept
    {
        if (end_of_range == EndOfRange::kNonExistent) {
            return kEndOfNonExistentRange;
        }
        if (end_of_range == EndOfRange::kUnknown) {
            return kEndOfUnknownRange;
        }

        std::uint64_t type = datagram ? kDatagramBit : static_cast<std::uint64_t>(*subgroup_id_mode);
        if (object_id_delta_present) {
            type |= kObjectIdDeltaBit;
        }
        if (group_id_delta_present) {
            type |= kGroupIdDeltaBit;
        }
        if (priority_present) {
            type |= kPriorityBit;
        }
        if (properties_present) {
            type |= kPropertiesBit;
        }
        return type;
    }

    FetchObject FetchObjectSerializationState::Encode(const ObjectHeaders& headers,
                                                      const std::uint8_t priority,
                                                      const BytesSpan payload)
    {
        const bool datagram = headers.track_mode == TrackMode::kDatagram;

        // Delta encoding for group ID.
        const bool first_object = !prior_group_id_.has_value();
        const bool group_id_delta_present = first_object || headers.group_id != *prior_group_id_;
        std::optional<std::uint64_t> group_id_delta;
        if (first_object) {
            // Absolute value.
            group_id_delta = headers.group_id;
        } else if (group_id_delta_present) {
            // Delta encode based on group order.
            if (group_order_ == GroupOrder::kAscending) {
                if (headers.group_id <= *prior_group_id_) {
                    throw std::invalid_argument("FETCH groups are not in ascending order");
                }
                group_id_delta = headers.group_id - *prior_group_id_ - 1;
            } else {
                if (headers.group_id >= *prior_group_id_) {
                    throw std::invalid_argument("FETCH groups are not in descending order");
                }
                group_id_delta = *prior_group_id_ - headers.group_id - 1;
            }
        }

        // Delta encoding for object ID.
        const bool object_id_delta_present = first_object || !prior_object_id_.has_value() ||
                                             *prior_object_id_ == std::numeric_limits<std::uint64_t>::max() ||
                                             headers.object_id != *prior_object_id_ + 1;
        if (!group_id_delta_present && object_id_delta_present && headers.object_id < *prior_object_id_) {
            throw std::invalid_argument("FETCH objects are not in Object ID order");
        }

        // Determine subgroup encoding.
        auto subgroup_mode = FetchSerializationProperties::SubgroupIdMode::kExplicit;
        if (datagram || headers.subgroup_id == 0) {
            subgroup_mode = FetchSerializationProperties::SubgroupIdMode::kZero;
        } else if (prior_subgroup_id_ == headers.subgroup_id) {
            subgroup_mode = FetchSerializationProperties::SubgroupIdMode::kPrior;
        }

        const bool priority_present = !prior_priority_.has_value() || priority != *prior_priority_;
        const bool properties_present = headers.extensions.has_value() || headers.immutable_extensions.has_value();

        // Build fetch object.
        FetchObject object{};
        object.properties.emplace(subgroup_mode,
                                  object_id_delta_present,
                                  group_id_delta_present,
                                  priority_present,
                                  properties_present,
                                  datagram);
        if (group_id_delta_present) {
            object.group_id_delta = group_id_delta;
        }
        if (!datagram && subgroup_mode == FetchSerializationProperties::SubgroupIdMode::kExplicit) {
            object.subgroup_id = headers.subgroup_id;
        }
        if (object_id_delta_present) {
            object.object_id_delta = group_id_delta_present ? headers.object_id : headers.object_id - *prior_object_id_;
        }
        if (priority_present) {
            object.publisher_priority = priority;
        }
        object.extensions = headers.extensions;
        object.immutable_extensions = headers.immutable_extensions;
        object.payload.assign(payload.begin(), payload.end());

        // Save delta encoding state.
        prior_group_id_ = headers.group_id;
        prior_object_id_ = headers.object_id;
        prior_subgroup_id_ = datagram ? std::nullopt : std::optional(headers.subgroup_id);
        prior_priority_ = priority;
        return object;
    }

    std::optional<ResolvedFetchObject> FetchObjectSerializationState::Decode(FetchObject&& object)
    {
        // TODO: This might be an assertion because we should have set properties prior to calling Decode.
        // TODO: The optional only deals with ensuring bad values don't get parsed.
        // TODO: Should properties even be optional?
        if (!object.properties.has_value()) {
            throw ProtocolViolationException("Missing FETCH serialization flags");
        }

        const auto& properties = *object.properties;
        if (properties.end_of_range.has_value()) {
            if (!object.group_id_delta.has_value() || !object.object_id_delta.has_value()) {
                throw ProtocolViolationException("FETCH End of Range omits its location");
            }
            if (!object.payload.empty()) {
                throw ProtocolViolationException("FETCH End of Range has a payload");
            }
            prior_group_id_ = *object.group_id_delta;
            prior_object_id_ = *object.object_id_delta;
            return std::nullopt;
        }

        // Unwrap group delta encoding.
        const bool first_object = !prior_group_id_.has_value();
        std::uint64_t group_id;
        if (object.group_id_delta.has_value()) {
            const std::uint64_t group_id_delta = *object.group_id_delta;
            if (first_object) {
                // First object encodes absolute value.
                group_id = group_id_delta;
            } else {
                // Delta decode based on group order.
                const std::uint64_t prior_group_id = *prior_group_id_;
                switch (group_order_) {
                    case GroupOrder::kAscending:
                        if (group_id_delta >= std::numeric_limits<std::uint64_t>::max() - prior_group_id) {
                            throw ProtocolViolationException("FETCH Group ID overflow");
                        }
                        group_id = prior_group_id + group_id_delta + 1;
                        break;
                    case GroupOrder::kDescending:
                        if (group_id_delta >= prior_group_id) {
                            throw ProtocolViolationException("FETCH Group ID underflow");
                        }
                        group_id = prior_group_id - group_id_delta - 1;
                        break;
                }
            }
        } else {
            // No group order, same group as prior.
            if (first_object) {
                throw ProtocolViolationException("First FETCH object omits Group ID");
            }
            group_id = *prior_group_id_;
        }

        // Unwrap object delta encoding.
        std::uint64_t object_id;
        if (object.object_id_delta.has_value()) {
            const std::uint64_t object_id_delta = *object.object_id_delta;
            if (first_object || object.group_id_delta.has_value()) {
                // First or first of group has absolute value.
                object_id = object_id_delta;
            } else {
                // Increment by given delta.
                if (object_id_delta > std::numeric_limits<std::uint64_t>::max() - *prior_object_id_) {
                    throw ProtocolViolationException("FETCH Object ID overflow");
                }
                object_id = *prior_object_id_ + object_id_delta;
            }
        } else {
            // Otherwise, increment prior by 1.
            if (!prior_object_id_.has_value() || *prior_object_id_ == std::numeric_limits<std::uint64_t>::max()) {
                throw ProtocolViolationException("FETCH Object ID overflow or missing prior Object ID");
            }
            object_id = *prior_object_id_ + 1;
        }

        std::uint64_t subgroup_id = 0;
        if (!properties.datagram) {
            switch (*properties.subgroup_id_mode) {
                case FetchSerializationProperties::SubgroupIdMode::kZero:
                    subgroup_id = 0;
                    break;
                case FetchSerializationProperties::SubgroupIdMode::kPrior:
                    if (!prior_subgroup_id_.has_value()) {
                        throw ProtocolViolationException("FETCH object references a missing prior Subgroup ID");
                    }
                    subgroup_id = *prior_subgroup_id_;
                    break;
                case FetchSerializationProperties::SubgroupIdMode::kNext:
                    if (!prior_subgroup_id_.has_value() ||
                        *prior_subgroup_id_ == std::numeric_limits<std::uint64_t>::max()) {
                        throw ProtocolViolationException("FETCH Subgroup ID overflow or missing prior Subgroup ID");
                    }
                    subgroup_id = *prior_subgroup_id_ + 1;
                    break;
                case FetchSerializationProperties::SubgroupIdMode::kExplicit:
                    if (!object.subgroup_id.has_value()) {
                        throw ProtocolViolationException("FETCH object omits explicit Subgroup ID");
                    }
                    subgroup_id = *object.subgroup_id;
                    break;
            }
        }

        std::uint8_t priority;
        if (object.publisher_priority.has_value()) {
            priority = *object.publisher_priority;
        } else {
            if (!prior_priority_.has_value()) {
                throw ProtocolViolationException("First FETCH object omits Publisher Priority");
            }
            priority = *prior_priority_;
        }

        // Construct resolved object.
        ResolvedFetchObject resolved{
            .headers = {
              .group_id = group_id,
              .object_id = object_id,
              .subgroup_id = subgroup_id,
              .payload_length = object.payload.size(),
              .status = ObjectStatus::kAvailable,
              .priority = priority,
              .ttl = std::nullopt, // TODO: Who sets this, and from what?
              .track_mode = properties.datagram ? TrackMode::kDatagram : TrackMode::kStream,
              .extensions = std::move(object.extensions),
              .immutable_extensions = std::move(object.immutable_extensions),
            },
            .payload = std::move(object.payload),
        };

        // Save parsing state.
        prior_group_id_ = group_id;
        prior_object_id_ = object_id;
        prior_subgroup_id_ = properties.datagram ? std::nullopt : std::optional(subgroup_id);
        prior_priority_ = priority;
        return resolved;
    }

    Bytes& operator<<(Bytes& buffer, const FetchObject& msg)
    {
        // These assertions guard our own internal logic of matching flags to optionality.
        assert(msg.properties.has_value());
        const auto& properties = *msg.properties;
        buffer << UintVar(properties.GetType());
        if (properties.group_id_delta_present) {
            assert(msg.group_id_delta.has_value());
            buffer << UintVar(*msg.group_id_delta);
        }
        if (!properties.end_of_range.has_value() && !properties.datagram &&
            properties.subgroup_id_mode == FetchSerializationProperties::SubgroupIdMode::kExplicit) {
            assert(msg.subgroup_id.has_value());
            buffer << UintVar(*msg.subgroup_id);
        }
        if (properties.object_id_delta_present) {
            assert(msg.object_id_delta.has_value());
            buffer << UintVar(*msg.object_id_delta);
        }
        if (!properties.end_of_range.has_value()) {
            if (properties.priority_present) {
                assert(msg.publisher_priority.has_value());
                buffer.push_back(*msg.publisher_priority);
            }
            if (properties.properties_present) {
                SerializeExtensions(buffer, msg.extensions, msg.immutable_extensions);
            }
        } else if (!msg.payload.empty()) {
            throw std::invalid_argument("FETCH End of Range cannot have a payload");
        }
        buffer << UintVar(msg.payload.size());
        PushBytes(buffer, msg.payload);
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, FetchObject& msg)
    {
        switch (msg.current_pos) {
            case 0: {
                std::uint64_t flags;
                if (!ParseUintVField(buffer, flags)) {
                    return false;
                }
                msg.properties.emplace(flags);
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 1: {
                if (msg.properties->group_id_delta_present) {
                    std::uint64_t group_id_delta;
                    if (!ParseUintVField(buffer, group_id_delta)) {
                        return false;
                    }
                    msg.group_id_delta = group_id_delta;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 2: {
                if (!msg.properties->end_of_range.has_value() && !msg.properties->datagram &&
                    msg.properties->subgroup_id_mode == FetchSerializationProperties::SubgroupIdMode::kExplicit) {
                    std::uint64_t subgroup_id;
                    if (!ParseUintVField(buffer, subgroup_id)) {
                        return false;
                    }
                    msg.subgroup_id = subgroup_id;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 3: {
                if (msg.properties->object_id_delta_present) {
                    std::uint64_t object_id_delta;
                    if (!ParseUintVField(buffer, object_id_delta)) {
                        return false;
                    }
                    msg.object_id_delta = object_id_delta;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 4: {
                if (!msg.properties->end_of_range.has_value() && msg.properties->priority_present) {
                    const auto val = buffer.Front();
                    if (val.empty()) {
                        return false;
                    }
                    buffer.Pop();
                    msg.publisher_priority = val[0];
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 5: {
                if (!msg.properties->end_of_range.has_value() && msg.properties->properties_present &&
                    !ParseExtensions(buffer,
                                     msg.extension_headers_length,
                                     msg.extensions,
                                     msg.immutable_extensions,
                                     msg.extension_bytes_remaining,
                                     msg.current_tag,
                                     msg.prev_extension_type)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 6: {
                if (!ParseUintVField(buffer, msg.payload_len)) {
                    return false;
                }
                if (msg.properties->end_of_range.has_value() && msg.payload_len != 0) {
                    throw ProtocolViolationException("FETCH End of Range has a payload");
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 7: {
                if (msg.payload_len == 0) {
                    msg.parse_completed = true;
                    return true;
                }
                if (!buffer.Available(msg.payload_len)) {
                    return false;
                }
                auto val = buffer.Front(msg.payload_len);
                if (val.size() == 0) {
                    return false;
                }

                msg.payload.assign(val.begin(), val.end());
                buffer.Pop(msg.payload_len);
                msg.parse_completed = true;
                [[fallthrough]];
            }
            default:
                break;
        }

        return msg.parse_completed;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, FetchObject&);

    //
    // Object
    //

    Bytes& operator<<(Bytes& buffer, const ObjectDatagram& msg)
    {
        const auto properties = msg.GetProperties();
        buffer << UintVar(properties.GetType());
        buffer << UintVar(msg.track_alias);
        buffer << UintVar(msg.group_id);
        if (!properties.zero_object_id) {
            buffer << UintVar(msg.object_id);
        }
        if (!properties.default_priority) {
            assert(msg.priority.has_value()); // Internal invariant.
            buffer.push_back(*msg.priority);
        }
        if (msg.extensions.has_value() || msg.immutable_extensions.has_value()) {
            SerializeExtensions(buffer, msg.extensions, msg.immutable_extensions);
        }

        if (msg.payload.empty()) {
            return buffer;
        }

        PushBytes(buffer, msg.payload);

        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, ObjectDatagram& msg)
    {
        switch (msg.current_pos) {
            case 0: {
                uint64_t type;
                if (!ParseUintVField(buffer, type)) {
                    return false;
                }
                msg.properties.emplace(type);
                assert(!msg.properties->status); // Internal invariant.
                msg.end_of_group = msg.properties->end_of_group;
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 1: {
                if (!ParseUintVField(buffer, msg.track_alias)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 2: {
                if (!ParseUintVField(buffer, msg.group_id)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 3: {
                if (!msg.properties->zero_object_id) {
                    if (!ParseUintVField(buffer, msg.object_id)) {
                        return false;
                    }
                } else {
                    msg.object_id = 0;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 4: {
                if (!msg.properties->default_priority) {
                    auto val = buffer.Front();
                    if (val.empty()) {
                        return false;
                    }
                    buffer.Pop();
                    msg.priority = val[0];
                } else {
                    msg.priority = std::nullopt;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 5: {
                if (msg.properties->extensions) {
                    if (!ParseExtensions(buffer,
                                         msg.extension_headers_length,
                                         msg.extensions,
                                         msg.immutable_extensions,
                                         msg.extension_bytes_left,
                                         msg.current_tag,
                                         msg.prev_extension_type)) {
                        return false;
                    }
                } else {
                    msg.extensions = std::nullopt;
                    msg.immutable_extensions = std::nullopt;
                }
                msg.current_pos += 1;
                msg.payload_len = buffer.Size();
                [[fallthrough]];
            }
            case 6: {
                if (msg.payload_len == 0) {
                    msg.parse_completed = true;
                    return true;
                }

                if (!buffer.Available(msg.payload_len)) {
                    return false;
                }

                auto val = buffer.Front(msg.payload_len);
                msg.payload.assign(val.begin(), val.end());
                buffer.Pop(msg.payload_len);
                msg.parse_completed = true;
                [[fallthrough]];
            }
            default:
                break;
        }

        return msg.parse_completed;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, ObjectDatagram&);

    Bytes& operator<<(Bytes& buffer, const ObjectDatagramStatus& msg)
    {
        const auto properties =
          DatagramHeaderProperties(msg.extensions.has_value() || msg.immutable_extensions.has_value(),
                                   false,
                                   msg.object_id == 0,
                                   !msg.priority.has_value(),
                                   true);
        buffer << UintVar(properties.GetType());
        buffer << UintVar(msg.track_alias);
        buffer << UintVar(msg.group_id);
        if (!properties.zero_object_id) {
            buffer << UintVar(msg.object_id);
        }
        if (!properties.default_priority) {
            assert(msg.priority.has_value()); // Internal invariant.
            buffer.push_back(*msg.priority);
        }
        if (properties.extensions) {
            SerializeExtensions(buffer, msg.extensions, msg.immutable_extensions);
        }
        buffer << UintVar(static_cast<uint8_t>(msg.status));

        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, ObjectDatagramStatus& msg)
    {
        switch (msg.current_pos) {
            case 0: {
                std::uint64_t type;
                if (!ParseUintVField(buffer, type)) {
                    return false;
                }
                msg.properties.emplace(type);
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 1: {
                if (!ParseUintVField(buffer, msg.track_alias)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 2: {
                if (!ParseUintVField(buffer, msg.group_id)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 3: {
                if (!msg.properties->zero_object_id) {
                    if (!ParseUintVField(buffer, msg.object_id)) {
                        return false;
                    }
                } else {
                    msg.object_id = 0;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 4: {
                if (!msg.properties->default_priority) {
                    auto val = buffer.Front();
                    if (val.empty()) {
                        return false;
                    }
                    buffer.Pop();
                    msg.priority = val[0];
                } else {
                    msg.priority = std::nullopt;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 5: {
                if (msg.properties->extensions) {
                    if (!ParseExtensions(buffer,
                                         msg.extension_headers_length,
                                         msg.extensions,
                                         msg.immutable_extensions,
                                         msg.extension_bytes_left,
                                         msg.current_tag,
                                         msg.prev_extension_type)) {
                        return false;
                    }
                } else {
                    msg.extensions = std::nullopt;
                    msg.immutable_extensions = std::nullopt;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 6: {
                uint64_t status = 0;
                if (!ParseUintVField(buffer, status)) {
                    return false;
                }
                msg.status = static_cast<ObjectStatus>(status);
                msg.current_pos += 1;
                msg.parse_completed = true;
                break;
            }
            default:
                break;
        }

        return msg.parse_completed;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, ObjectDatagramStatus&);

    Bytes& operator<<(Bytes& buffer, const StreamHeaderSubGroup& msg)
    {
        assert(msg.properties.has_value());
        buffer << UintVar(msg.properties->GetType());
        buffer << UintVar(msg.track_alias);
        buffer << UintVar(msg.group_id);
        switch (msg.properties->subgroup_id_mode) {
            case SubgroupIdType::kExplicit: {
                if (!msg.subgroup_id.has_value()) {
                    throw std::invalid_argument("Subgroup ID must be set when type is kExplicit");
                }
                buffer << UintVar(*msg.subgroup_id);
                break;
            }
            case SubgroupIdType::kIsZero: {
                // Subgroup id is not included, skip for serialization
                break;
            }
            case SubgroupIdType::kSetFromFirstObject: {
                if (msg.subgroup_id.has_value()) {
                    throw std::invalid_argument("Subgroup ID must be not set when type is not kExplicit");
                }
                break;
            }
            case SubgroupIdType::kReserved: {
                throw std::invalid_argument("Subgroup mode must not be kReserved");
            }
        }
        if (!msg.properties->default_priority) {
            assert(msg.priority.has_value());
            buffer.push_back(*msg.priority);
        }
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, StreamHeaderSubGroup& msg)
    {
        switch (msg.current_pos) {
            case 0: {
                std::uint64_t subgroup_type;
                if (!ParseUintVField(buffer, subgroup_type)) {
                    return false;
                }
                msg.properties.emplace(subgroup_type);
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 1: {
                if (!ParseUintVField(buffer, msg.track_alias)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 2: {
                if (!ParseUintVField(buffer, msg.group_id)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 3: {
                switch (msg.properties->subgroup_id_mode) {
                    case SubgroupIdType::kIsZero:
                        msg.subgroup_id = 0;
                        break;
                    case SubgroupIdType::kSetFromFirstObject:
                        msg.subgroup_id = std::nullopt; // Will be updated by first object.
                        break;
                    case SubgroupIdType::kExplicit:
                        std::uint64_t subgroup_id;
                        if (!ParseUintVField(buffer, subgroup_id)) {
                            return false;
                        }
                        msg.subgroup_id = subgroup_id;
                        break;
                    case SubgroupIdType::kReserved:
                        throw ProtocolViolationException("Subgroup mode must not be reserved");
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 4: {
                if (!msg.properties->default_priority) {
                    auto val = buffer.Front();
                    if (val.empty()) {
                        return false;
                    }
                    buffer.Pop();
                    msg.priority = val[0];
                } else {
                    msg.priority = std::nullopt;
                }
                msg.current_pos += 1;
                msg.parse_completed = true;
                [[fallthrough]];
            }
            default:
                break;
        }

        return msg.parse_completed;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, StreamHeaderSubGroup&);

    Bytes& operator<<(Bytes& buffer, const StreamSubGroupObject& msg)
    {
        buffer << UintVar(msg.object_delta);
        assert(msg.properties.has_value()); // Stream type must have been set before serialization.
        if (!msg.properties->extensions && (msg.extensions.has_value() || msg.immutable_extensions.has_value())) {
            // This is not allowed.
            assert(false);
        }
        if (msg.properties->extensions) {
            SerializeExtensions(buffer, msg.extensions, msg.immutable_extensions);
        }
        if (msg.payload.empty()) {
            // empty payload needs a object status to be set
            auto status = UintVar(static_cast<uint8_t>(msg.object_status));
            buffer.push_back(0);
            buffer << status;
        } else {
            buffer << UintVar(msg.payload.size());
            PushBytes(buffer, msg.payload);
        }
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, StreamSubGroupObject& msg)
    {
        switch (msg.current_pos) {
            case 0: {
                if (!ParseUintVField(buffer, msg.object_delta)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 1: {
                assert(msg.properties.has_value());
                if (msg.properties->extensions) {
                    if (!ParseExtensions(buffer,
                                         msg.extension_headers_length,
                                         msg.extensions,
                                         msg.immutable_extensions,
                                         msg.extension_bytes_left,
                                         msg.current_tag,
                                         msg.prev_extension_type)) {
                        return false;
                    }
                } else {
                    msg.extensions = std::nullopt;
                    msg.immutable_extensions = std::nullopt;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }

            case 2: {
                if (!ParseUintVField(buffer, msg.payload_len)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 3: {
                if (msg.payload_len == 0) {
                    uint64_t status = 0;
                    if (!ParseUintVField(buffer, status)) {
                        return false;
                    }
                    msg.object_status = static_cast<ObjectStatus>(status);
                    msg.parse_completed = true;
                    return true;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }

            case 4: {
                if (!buffer.Available(msg.payload_len)) {
                    return false;
                }
                auto val = buffer.Front(msg.payload_len);
                if (val.empty()) {
                    return false;
                }

                msg.payload.assign(val.begin(), val.end());
                buffer.Pop(msg.payload_len);
                msg.parse_completed = true;
                [[fallthrough]];
            }
            default:
                break;
        }

        return msg.parse_completed;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, StreamSubGroupObject&);

}
