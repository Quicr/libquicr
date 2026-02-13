// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/detail/messages.h"

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
        field = std::move(val.value());
        return true;
    }

    template<class StreamBufferType>
    static bool ParseExtensions(StreamBufferType& buffer,
                                std::optional<std::size_t>& extension_headers_length,
                                std::optional<Extensions>& extensions,
                                std::size_t& extension_bytes_remaining,
                                std::optional<std::uint64_t>& current_header)
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
                tag = tag_field.Get();
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
        std::size_t total_length = 0;
        std::vector<KeyValuePair<std::uint64_t>> kvps;
        for (const auto& [key, values] : extensions) {
            for (const auto& value : values) {
                const auto kvp = KeyValuePair<std::uint64_t>{ key, value };
                const auto size = kvp.Size();
                total_length += size;
                kvps.push_back(kvp);
            }
        }

        // Total length of all extension headers (varint).
        buffer << static_cast<std::uint64_t>(total_length);

        // Write the KVP extensions.
        for (const auto& kvp : kvps) {
            buffer << kvp;
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

        constexpr auto immutable_key = static_cast<std::uint64_t>(ExtensionHeaderType::kImmutable);

        // Serialize immutable extensions in MoQ form, and insert into combined extensions key.
        if (immutable_extensions.has_value() && !immutable_extensions->empty()) {
            // Immutable extensions MUST NOT contain an immutable extension entry.
            if (immutable_extensions->contains(immutable_key)) {
                throw ProtocolViolationException(
                  "An immutable extension header must not contain another immutable extension header");
            }

            // Serialize immutable extensions.
            Bytes immutable_bytes;
            for (const auto& [key, values] : *immutable_extensions) {
                for (const auto& value : values) {
                    immutable_bytes << KeyValuePair<std::uint64_t>{ key, value };
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
                         std::optional<std::uint64_t>& current_header)
    {
        // First, parse all extensions.
        if (!ParseExtensions(buffer, extension_headers_length, extensions, extension_bytes_remaining, current_header)) {
            return false;
        }

        constexpr auto immutable_key = static_cast<std::uint64_t>(ExtensionHeaderType::kImmutable);

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
                if (!ParseExtensions(stream_buffer,
                                     immutable_length,
                                     immutable_extensions,
                                     immutable_bytes_remaining,
                                     immutable_current_header)) {
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
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, FetchHeader&);

    Bytes& operator<<(Bytes& buffer, const FetchHeader& msg)
    {
        buffer << UintVar(static_cast<uint64_t>(msg.type));
        buffer << UintVar(msg.request_id);
        return buffer;
    }

    FetchSerializationProperties::FetchSerializationProperties(const std::uint64_t wire)
      : end_of_range(ParseEndOfRange(wire))
      , subgroup_id_mode(end_of_range.has_value()
                           ? std::nullopt
                           : std::make_optional(static_cast<FetchSubgroupIdType>(wire & kSubgroupBitmask)))
      , object_id_present(end_of_range.has_value() || wire & kObjectIdBitmask)
      , group_id_present(end_of_range.has_value() || wire & kGroupIdBitmask)
      , priority_present(!end_of_range.has_value() && wire & kPriorityBitmask)
      , extensions_present(!end_of_range.has_value() && wire & kExtensionsBitmask)
      , datagram(wire & kDatagramBitmask)
    {
        if (!end_of_range.has_value() && wire >= 128) {
            throw ProtocolViolationException("Bad serialization flags");
        }
    }

    FetchSerializationProperties::FetchSerializationProperties(const EndOfRange end_of_range) noexcept
      : end_of_range(end_of_range)
      , subgroup_id_mode(std::nullopt)
      , object_id_present(true)
      , group_id_present(true)
      , priority_present(false)
      , extensions_present(false)
      , datagram(false)
    {
    }

    // Stream forwarding preference creation.
    FetchSerializationProperties::FetchSerializationProperties(const FetchSubgroupIdType subgroup_id_mode,
                                                               const bool object_id_present,
                                                               const bool group_id_present,
                                                               const bool priority_present,
                                                               const bool extensions_present) noexcept
      : subgroup_id_mode(subgroup_id_mode)
      , object_id_present(object_id_present)
      , group_id_present(group_id_present)
      , priority_present(priority_present)
      , extensions_present(extensions_present)
      , datagram(false)
    {
    }

    // Datagram forwarding preference.
    FetchSerializationProperties::FetchSerializationProperties(const bool object_id_present,
                                                               const bool group_id_present,
                                                               const bool priority_present,
                                                               const bool extensions_present) noexcept
      : subgroup_id_mode(std::nullopt)
      , object_id_present(object_id_present)
      , group_id_present(group_id_present)
      , priority_present(priority_present)
      , extensions_present(extensions_present)
      , datagram(true)
    {
    }

    std::uint64_t FetchSerializationProperties::GetType() const noexcept
    {
        if (end_of_range.has_value()) {
            switch (*end_of_range) {
                case EndOfRange::kEndOfNonExistentRange:
                    return kEndOfNonExistentRange;
                case EndOfRange::kEndOfUnknownRange:
                    return kEndOfUnknownRange;
            }
        }

        std::uint64_t type = 0;
        if (subgroup_id_mode.has_value()) {
            type |= static_cast<std::uint8_t>(*subgroup_id_mode) & kSubgroupBitmask;
        }
        if (object_id_present) {
            type |= kObjectIdBitmask;
        }
        if (group_id_present) {
            type |= kGroupIdBitmask;
        }
        if (priority_present) {
            type |= kPriorityBitmask;
        }
        if (extensions_present) {
            type |= kExtensionsBitmask;
        }
        if (datagram) {
            type |= kDatagramBitmask;
        }
        return type;
    }

    std::optional<FetchSerializationProperties::EndOfRange> FetchSerializationProperties::ParseEndOfRange(
      const std::uint64_t value) noexcept
    {
        switch (value) {
            case kEndOfNonExistentRange:
                return EndOfRange::kEndOfNonExistentRange;
            case kEndOfUnknownRange:
                return EndOfRange::kEndOfUnknownRange;
            default:
                return std::nullopt;
        }
    }

    FetchSerializationProperties FetchObjectSerializationState::MakeProperties(
      const ObjectHeaders& object_headers,
      const ObjectPriority priority) const noexcept
    {
        // Subgroups.
        std::optional<FetchSerializationProperties::FetchSubgroupIdType> subgroup_type;
        if (!object_headers.track_mode || object_headers.track_mode == TrackMode::kStream) {
            subgroup_type = FetchSerializationProperties::FetchSubgroupIdType::kSubgroupExplicit;
            if (object_headers.subgroup_id == 0) {
                // Zero has a code point.
                subgroup_type = FetchSerializationProperties::FetchSubgroupIdType::kSubgroupZero;
            } else if (prior_subgroup_id.has_value()) {
                if (object_headers.subgroup_id == *prior_subgroup_id) {
                    // Same as last.
                    subgroup_type = FetchSerializationProperties::FetchSubgroupIdType::kSubgroupPrior;
                } else if (object_headers.subgroup_id == *prior_subgroup_id + 1) {
                    // +1 from last.
                    subgroup_type = FetchSerializationProperties::FetchSubgroupIdType::kSubgroupNext;
                }
            }
        }

        // Only serialize when it's not the next one.
        const bool serialize_object_id =
          !(prior_object_id.has_value() && object_headers.object_id == *prior_object_id + 1);
        // Only serialize when it's not the same as the last one.
        const bool serialize_group_id = !(prior_group_id.has_value() && object_headers.group_id == *prior_group_id);
        // Only serialize when priority is different.
        const bool serialize_priority = !(prior_priority.has_value() && *prior_priority == priority);
        // Only serialize extensions when there are extensions.
        const bool extensions =
          object_headers.extensions.has_value() || object_headers.immutable_extensions.has_value();

        // Build.
        if (object_headers.track_mode == TrackMode::kDatagram) {
            return { serialize_object_id, serialize_group_id, serialize_priority, extensions };
        }
        return { *subgroup_type, serialize_object_id, serialize_group_id, serialize_priority, extensions };
    }

    void FetchObjectSerializationState::Update(const ObjectHeaders& object_headers) noexcept
    {
        prior_group_id = object_headers.group_id;
        prior_object_id = object_headers.object_id;
        prior_subgroup_id = object_headers.subgroup_id;
        prior_priority = object_headers.priority;
    }

    Bytes& operator<<(Bytes& buffer, const FetchObject& msg)
    {
        assert(msg.properties.has_value());
        buffer << UintVar(msg.properties->GetType());

        if (msg.properties->group_id_present) {
            assert(msg.group_id.has_value());
            buffer << UintVar(*msg.group_id);
        }
        if (!msg.properties->datagram) {
            if (msg.properties->subgroup_id_mode ==
                FetchSerializationProperties::FetchSubgroupIdType::kSubgroupExplicit) {
                assert(msg.subgroup_id.has_value());
                buffer << UintVar(*msg.subgroup_id);
            }
        }
        if (msg.properties->object_id_present) {
            assert(msg.object_id.has_value());
            buffer << UintVar(*msg.object_id);
        }
        if (msg.properties->priority_present) {
            assert(msg.publisher_priority.has_value());
            buffer.push_back(*msg.publisher_priority);
        }
        if (msg.properties->extensions_present) {
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
    bool operator>>(StreamBufferType& buffer, FetchObject& msg)
    {
        switch (msg.current_pos) {
            case 0: {
                std::uint64_t serialized_flags;
                if (!ParseUintVField(buffer, serialized_flags)) {
                    return false;
                }
                msg.properties.emplace(FetchSerializationProperties(serialized_flags));
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 1: {
                if (msg.properties->group_id_present) {
                    GroupId group_id;
                    if (!ParseUintVField(buffer, group_id)) {
                        return false;
                    }
                    msg.group_id = group_id;
                } else {
                    msg.group_id = std::nullopt;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 2: {
                const auto& properties = *msg.properties;
                if (!properties.datagram) {
                    if (properties.end_of_range.has_value()) {
                        msg.subgroup_id = std::nullopt;
                    } else {
                        if (!properties.subgroup_id_mode.has_value()) {
                            throw ProtocolViolationException("Unexpectedly missing subgroup mode");
                        }
                        switch (*properties.subgroup_id_mode) {
                            case FetchSerializationProperties::FetchSubgroupIdType::kSubgroupZero:
                                msg.subgroup_id = 0;
                                break;
                            case FetchSerializationProperties::FetchSubgroupIdType::kSubgroupExplicit:
                                std::uint64_t subgroup_id;
                                if (!ParseUintVField(buffer, subgroup_id)) {
                                    return false;
                                }
                                msg.subgroup_id = subgroup_id;
                                break;
                            default:
                                msg.subgroup_id = std::nullopt;
                                break;
                        }
                    }
                } else {
                    msg.subgroup_id = std::nullopt;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 3: {
                if (msg.properties->object_id_present) {
                    ObjectId object_id;
                    if (!ParseUintVField(buffer, object_id)) {
                        return false;
                    }
                    msg.object_id = object_id;
                } else {
                    msg.object_id = std::nullopt;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 4: {
                if (msg.properties->priority_present) {
                    auto val = buffer.Front();
                    if (!val) {
                        return false;
                    }
                    buffer.Pop();
                    msg.publisher_priority = val.value();
                } else {
                    msg.publisher_priority = std::nullopt;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 5: {
                if (msg.properties->extensions_present) {
                    if (!ParseExtensions(buffer,
                                         msg.extension_headers_length,
                                         msg.extensions,
                                         msg.immutable_extensions,
                                         msg.extension_bytes_remaining,
                                         msg.current_tag)) {
                        return false;
                    }
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }

            case 6: {
                if (!ParseUintVField(buffer, msg.payload_len)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 7: {
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

            case 8: {
                if (!buffer.Available(msg.payload_len)) {
                    return false;
                }
                auto val = buffer.Front(msg.payload_len);
                if (val.size() == 0) {
                    return false;
                }

                msg.payload = std::move(val);
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
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, FetchObject&);

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
                    if (!val) {
                        return false;
                    }
                    buffer.Pop();
                    msg.priority = val.value();
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
                                         msg.current_tag)) {
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

                msg.payload = std::move(buffer.Front(msg.payload_len));
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
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, ObjectDatagram&);

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
                    if (!val) {
                        return false;
                    }
                    buffer.Pop();
                    msg.priority = val.value();
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
                                         msg.current_tag)) {
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
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, ObjectDatagramStatus&);

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
                [[fallthrough]];
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
                        SubGroupId subgroup_id;
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
                    if (!val) {
                        return false;
                    }
                    buffer.Pop();
                    msg.priority = val.value();
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
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, StreamHeaderSubGroup&);

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
                                         msg.current_tag)) {
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
                if (val.size() == 0) {
                    return false;
                }

                msg.payload = std::move(val);
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
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, StreamSubGroupObject&);

}
