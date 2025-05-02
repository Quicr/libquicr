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
    static bool ParseExtensions(StreamBufferType& buffer,
                                uint64_t& count,
                                std::optional<Extensions>& extensions,
                                std::optional<uint64_t>& current_tag)
    {

        // get the count of extensions
        if (count == 0 && !ParseUintVField(buffer, count)) {
            return false;
        }

        if (count == 0) {
            return true;
        }

        if (extensions == std::nullopt) {
            extensions = Extensions();
        }

        size_t completed = 0;
        for (size_t extension = 0; extension < count; extension++) {
            if (current_tag == std::nullopt) {
                uint64_t tag{ 0 };
                if (!ParseUintVField(buffer, tag)) {
                    count -= completed;
                    return false;
                }
                current_tag = tag;
            }

            if (current_tag.value() % 2 == 0) {
                auto val = buffer.DecodeUintV();
                if (!val) {
                    count -= completed;
                    return false;
                }
                std::vector<uint8_t> bytes(8);
                memcpy(bytes.data(), &val.value(), 8);
                extensions.value()[current_tag.value()] = std::move(bytes);
            } else {
                auto val = buffer.DecodeBytes();
                if (!val) {
                    count -= completed;
                    return false;
                }
                extensions.value()[current_tag.value()] = std::move(val.value());
            }
            current_tag = std::nullopt;
            completed++;
        }
        count -= completed;
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

    static void PushBytes(Bytes& buffer, const Bytes& bytes)
    {
        buffer.insert(buffer.end(), bytes.begin(), bytes.end());
    }

    static void PushExtensions(Bytes& buffer, const std::optional<Extensions>& extensions)
    {
        if (!extensions.has_value()) {
            buffer.push_back(0);
            return;
        }

        buffer << UintVar(extensions.value().size());
        for (const auto& extension : extensions.value()) {
            buffer << UintVar(extension.first);
            if (extension.first % 2 == 0) {
                // Even types are a single varint value.
                std::uint64_t val = 0;
                std::memcpy(&val, extension.second.data(), std::min(extension.second.size(), sizeof(std::uint64_t)));
                buffer << UintVar(val);
                continue;
            }
            // Odd types are varint length + bytes.
            buffer << UintVar(extension.second.size());
            PushBytes(buffer, extension.second);
        }
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, quicr::messages::Parameter& param)
    {
        if (!ParseUintVField(buffer, param.type)) {
            return false;
        }

        auto val = buffer.DecodeBytes();
        if (!val) {
            return false;
        }

        param.value = std::move(val.value());

        return true;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, FetchHeader& msg)
    {
        switch (msg.current_pos) {
            case 0: {
                if (!ParseUintVField(buffer, msg.subscribe_id)) {
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
        buffer << UintVar(static_cast<uint64_t>(DataMessageType::kFetchHeader));
        buffer << UintVar(msg.subscribe_id);
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, const FetchObject& msg)
    {
        buffer << UintVar(msg.group_id);
        buffer << UintVar(msg.subgroup_id);
        buffer << UintVar(msg.object_id);
        buffer.push_back(msg.publisher_priority);
        PushExtensions(buffer, msg.extensions);
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
                if (!ParseExtensions(buffer, msg.num_extensions, msg.extensions, msg.current_tag)) {
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
        buffer << UintVar(static_cast<uint64_t>(DataMessageType::kObjectDatagram));
        buffer << UintVar(msg.track_alias);
        buffer << UintVar(msg.group_id);
        buffer << UintVar(msg.object_id);
        buffer.push_back(msg.priority);
        PushExtensions(buffer, msg.extensions);

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
                if (!ParseUintVField(buffer, msg.track_alias)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 1: {
                if (!ParseUintVField(buffer, msg.group_id)) {
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
                msg.priority = val.value();
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 4: {
                if (!ParseExtensions(buffer, msg.num_extensions, msg.extensions, msg.current_tag)) {
                    return false;
                }
                msg.current_pos += 1;
                msg.payload_len = buffer.Size();
                [[fallthrough]];
            }
            case 5: {
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
        buffer << UintVar(static_cast<uint64_t>(DataMessageType::kObjectDatagramStatus));
        buffer << UintVar(msg.track_alias);
        buffer << UintVar(msg.group_id);
        buffer << UintVar(msg.object_id);
        buffer.push_back(msg.priority);
        buffer << UintVar(static_cast<uint8_t>(msg.status));

        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, ObjectDatagramStatus& msg)
    {
        switch (msg.current_pos) {
            case 0: {
                if (!ParseUintVField(buffer, msg.track_alias)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 1: {
                if (!ParseUintVField(buffer, msg.group_id)) {
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
                msg.priority = val.value();
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 4: {
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
        switch (msg.type) {
            case StreamHeaderType::kSubgroupExplicitNoExtensions:
                [[fallthrough]];
            case StreamHeaderType::kSubgroupExplicitWithExtensions:
                assert(msg.subgroup_id.has_value());
                buffer << UintVar(msg.subgroup_id.value());
                break;
            default:
                break;
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
                switch (msg.type) {
                    case StreamHeaderType::kSubgroupZeroNoExtensions:
                    case StreamHeaderType::kSubgroupZeroWithExtensions:
                        msg.subgroup_id = 0;
                        break;
                    case StreamHeaderType::kSubgroupFirstObjectNoExtensions:
                    case StreamHeaderType::kSubgroupFirstObjectWithExtensions:
                        msg.subgroup_id = std::nullopt; // Will be updated by first object.
                        break;
                    case StreamHeaderType::kSubgroupExplicitNoExtensions:
                    case StreamHeaderType::kSubgroupExplicitWithExtensions:
                        messages::SubGroupId subgroup_id;
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
        buffer << UintVar(msg.object_id);
        if (msg.serialize_extensions) {
            PushExtensions(buffer, msg.extensions);
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
                if (!ParseUintVField(buffer, msg.object_id)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 1: {
                if (msg.serialize_extensions) {
                    if (!ParseExtensions(buffer, msg.num_extensions, msg.extensions, msg.current_tag)) {
                        return false;
                    }
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
