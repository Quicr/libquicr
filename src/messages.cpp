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
            immutable_bytes << *immutable_extensions;
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
                std::optional<std::size_t> immutable_length;
                std::size_t immutable_bytes_remaining = 0;
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

    Bytes& operator<<(Bytes& buffer, const FetchObject& msg)
    {
        buffer << UintVar(msg.group_id);
        buffer << UintVar(msg.subgroup_id);
        buffer << UintVar(msg.object_id);
        buffer.push_back(msg.publisher_priority);
        SerializeExtensions(buffer, msg.extensions, msg.immutable_extensions);
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
                if (!ParseUintVField(buffer, msg.group_id)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 1: {
                if (!ParseUintVField(buffer, msg.subgroup_id)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 2: {
                if (!ParseUintVField(buffer, msg.object_id)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 3: {
                auto val = buffer.Front();
                if (!val) {
                    return false;
                }
                buffer.Pop();
                msg.publisher_priority = val.value();
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 4: {
                if (!ParseExtensions(buffer,
                                     msg.extension_headers_length,
                                     msg.extensions,
                                     msg.immutable_extensions,
                                     msg.extension_bytes_remaining,
                                     msg.current_tag)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }

            case 5: {
                if (!ParseUintVField(buffer, msg.payload_len)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 6: {
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

            case 7: {
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
        buffer << UintVar(static_cast<uint64_t>(msg.GetType()));
        buffer << UintVar(msg.track_alias);
        buffer << UintVar(msg.group_id);
        buffer << UintVar(msg.object_id);
        buffer.push_back(msg.priority);
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
                const auto header_type = static_cast<DatagramHeaderType>(type);
                msg.type = header_type;
                const auto properties = DatagramHeaderProperties(header_type);
                msg.end_of_group = properties.end_of_group;
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
                if (!ParseUintVField(buffer, msg.object_id)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 4: {
                auto val = buffer.Front();
                if (!val) {
                    return false;
                }
                buffer.Pop();
                msg.priority = val.value();
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 5: {
                const auto properties = DatagramHeaderProperties(msg.type.value());
                if (properties.has_extensions) {
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
          DatagramStatusProperties(msg.extensions.has_value() || msg.immutable_extensions.has_value());
        buffer << UintVar(static_cast<uint64_t>(properties.GetType()));
        buffer << UintVar(msg.track_alias);
        buffer << UintVar(msg.group_id);
        buffer << UintVar(msg.object_id);
        buffer.push_back(msg.priority);
        if (properties.has_extensions) {
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
                msg.type = static_cast<DatagramStatusType>(type);
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
                if (!ParseUintVField(buffer, msg.object_id)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 4: {
                auto val = buffer.Front();
                if (!val) {
                    return false;
                }
                buffer.Pop();
                msg.priority = val.value();
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 5: {
                const auto properties = DatagramStatusProperties(msg.type.value());
                if (properties.has_extensions) {
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
        buffer << UintVar(static_cast<uint64_t>(msg.type));
        buffer << UintVar(msg.track_alias);
        buffer << UintVar(msg.group_id);
        const auto properties = StreamHeaderProperties(msg.type);
        if (properties.subgroup_id_type == SubgroupIdType::kExplicit) {
            if (!msg.subgroup_id.has_value()) {
                throw std::invalid_argument("Subgroup ID must be set when type is kExplicit");
            }
            buffer << UintVar(msg.subgroup_id.value());
        } else if (msg.subgroup_id.has_value()) {
            throw std::invalid_argument("Subgroup ID must be not set when type is not kExplicit");
        }
        buffer.push_back(msg.priority);
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
                msg.type = static_cast<StreamHeaderType>(subgroup_type);
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
                const auto properties = StreamHeaderProperties(msg.type);
                switch (properties.subgroup_id_type) {
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
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 4: {
                auto val = buffer.Front();
                if (!val) {
                    return false;
                }
                buffer.Pop();
                msg.priority = val.value();
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
        assert(msg.stream_type.has_value()); // Stream type must have been set before serialization.
        const auto properties = StreamHeaderProperties(*msg.stream_type);
        if (!properties.may_contain_extensions &&
            (msg.extensions.has_value() || msg.immutable_extensions.has_value())) {
            // This is not allowed.
            assert(false);
        }
        if (properties.may_contain_extensions) {
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
                assert(msg.stream_type.has_value());
                const auto properties = StreamHeaderProperties(*msg.stream_type);
                if (properties.may_contain_extensions) {
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
