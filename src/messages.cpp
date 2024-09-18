// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/detail/messages.h"

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
            auto val = buffer.DecodeBytes();
            if (!val) {
                count -= completed;
                return false;
            }
            extensions.value()[current_tag.value()] = std::move(val.value());
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

    static void PushExtensions(Serializer& buffer, const std::optional<Extensions>& extensions)
    {
        if (!extensions.has_value()) {
            buffer.Push(UintVar(0).Bytes());
            return;
        }

        buffer.Push(UintVar(extensions.value().size()).Bytes());
        for (const auto& extension : extensions.value()) {
            buffer.Push(UintVar(extension.first).Bytes());
            buffer.PushLengthBytes(extension.second);
        }
    }

    //
    // MoqParameter
    //

    Serializer& operator<<(Serializer& buffer, const MoqParameter& param)
    {

        buffer.Push(UintVar(param.type).Bytes());
        buffer.Push(UintVar(param.length).Bytes());
        if (param.length) {
            buffer.PushLengthBytes(param.value);
        }
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqParameter& param)
    {

        if (!ParseUintVField(buffer, param.type)) {
            return false;
        }

        if (!ParseUintVField(buffer, param.length)) {
            return false;
        }

        if (param.length) {
            auto val = buffer.DecodeBytes();
            if (!val) {
                return false;
            }
            param.value = std::move(val.value());
        }

        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqParameter&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqParameter&);

    //
    // Track Status
    //
    Serializer& operator<<(Serializer& buffer, const MoqTrackStatus& msg)
    {
        buffer.Push(UintVar(static_cast<uint64_t>(MoqMessageType::TRACK_STATUS)).Bytes());
        buffer.PushLengthBytes(msg.track_namespace);
        buffer.PushLengthBytes(msg.track_name);
        buffer.Push(UintVar(static_cast<uint64_t>(msg.status_code)).Bytes());
        buffer.Push(UintVar(msg.last_group_id).Bytes());
        buffer.Push(UintVar(msg.last_object_id).Bytes());

        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqTrackStatus& msg)
    {

        switch (msg.current_pos) {
            case 0: {
                if (!ParseBytesField(buffer, msg.track_namespace)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 1: {
                if (!ParseBytesField(buffer, msg.track_name)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 2: {
                auto val = buffer.DecodeUintV();
                if (!val) {
                    return false;
                }
                msg.status_code = static_cast<TrackStatus>(*val);
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 3: {
                if (!ParseUintVField(buffer, msg.last_group_id)) {
                    return false;
                }
                msg.current_pos += 1;

                [[fallthrough]];
            }
            case 4: {
                if (!ParseUintVField(buffer, msg.last_object_id)) {
                    return false;
                }
                msg.current_pos += 1;

                msg.parsing_completed = true;

                [[fallthrough]];
            }
            default:
                break;
        }

        if (!msg.parsing_completed) {
            return false;
        }

        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqTrackStatus&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqTrackStatus&);

    Serializer& operator<<(Serializer& buffer, const MoqTrackStatusRequest& msg)
    {
        buffer.Push(UintVar(static_cast<uint64_t>(MoqMessageType::TRACK_STATUS_REQUEST)).Bytes());
        buffer.PushLengthBytes(msg.track_namespace);
        buffer.PushLengthBytes(msg.track_name);

        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqTrackStatusRequest& msg)
    {

        switch (msg.current_pos) {
            case 0: {
                if (!ParseBytesField(buffer, msg.track_namespace)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 1: {
                if (!ParseBytesField(buffer, msg.track_name)) {
                    return false;
                }
                msg.current_pos += 1;
                msg.parsing_completed = true;
                [[fallthrough]];
            }
            default:
                break;
        }

        if (!msg.parsing_completed) {
            return false;
        }

        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqTrackStatusRequest&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqTrackStatusRequest&);

    //
    // Subscribe
    //

    Serializer& operator<<(Serializer& buffer, const MoqSubscribe& msg)
    {
        buffer.Push(UintVar(static_cast<uint64_t>(MoqMessageType::SUBSCRIBE)).Bytes());
        buffer.Push(UintVar(msg.subscribe_id).Bytes());
        buffer.Push(UintVar(msg.track_alias).Bytes());
        buffer.PushLengthBytes(msg.track_namespace);
        buffer.PushLengthBytes(msg.track_name);
        buffer.Push(UintVar(static_cast<uint64_t>(msg.filter_type)).Bytes());

        switch (msg.filter_type) {
            case FilterType::None:
            case FilterType::LatestGroup:
            case FilterType::LatestObject:
                break;
            case FilterType::AbsoluteStart: {
                buffer.Push(UintVar(msg.start_group).Bytes());
                buffer.Push(UintVar(msg.start_object).Bytes());
            } break;
            case FilterType::AbsoluteRange:
                buffer.Push(UintVar(msg.start_group).Bytes());
                buffer.Push(UintVar(msg.start_object).Bytes());
                buffer.Push(UintVar(msg.end_group).Bytes());
                buffer.Push(UintVar(msg.end_object).Bytes());
                break;
        }

        buffer.Push(UintVar(msg.track_params.size()).Bytes());
        for (const auto& param : msg.track_params) {
            buffer.Push(UintVar(static_cast<uint64_t>(param.type)).Bytes());
            buffer.PushLengthBytes(param.value);
        }

        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqSubscribe& msg)
    {

        switch (msg.current_pos) {
            case 0: {
                if (!ParseUintVField(buffer, msg.subscribe_id)) {
                    return false;
                }
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
                if (!ParseBytesField(buffer, msg.track_namespace)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 3: {
                if (!ParseBytesField(buffer, msg.track_name)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 4: {
                auto val = buffer.DecodeUintV();
                if (!val) {
                    return false;
                }
                auto filter = val.value();
                msg.filter_type = static_cast<FilterType>(filter);
                if (msg.filter_type == FilterType::LatestGroup || msg.filter_type == FilterType::LatestObject) {
                    // we don't get further fields until parameters
                    msg.current_pos = 9;
                } else {
                    msg.current_pos += 1;
                }
                [[fallthrough]];
            }
            case 5: {
                if (msg.filter_type == FilterType::AbsoluteStart || msg.filter_type == FilterType::AbsoluteRange) {
                    if (!ParseUintVField(buffer, msg.start_group)) {
                        return false;
                    }
                    msg.current_pos += 1;
                }
                [[fallthrough]];
            }
            case 6: {
                if (msg.filter_type == FilterType::AbsoluteStart || msg.filter_type == FilterType::AbsoluteRange) {
                    if (!ParseUintVField(buffer, msg.start_object)) {
                        return false;
                    }

                    if (msg.filter_type == FilterType::AbsoluteStart) {
                        msg.current_pos = 9;
                    } else {
                        msg.current_pos += 1;
                    }
                }
                [[fallthrough]];
            }
            case 7: {
                if (msg.filter_type == FilterType::AbsoluteRange) {
                    if (!ParseUintVField(buffer, msg.end_group)) {
                        return false;
                    }
                    msg.current_pos += 1;
                }

                [[fallthrough]];
            }
            case 8: {
                if (msg.filter_type == FilterType::AbsoluteRange) {
                    if (!ParseUintVField(buffer, msg.end_object)) {
                        return false;
                    }
                    msg.current_pos += 1;
                }
                [[fallthrough]];
            }
            case 9: {
                if (!msg.num_params.has_value()) {
                    uint64_t num = 0;
                    if (!ParseUintVField(buffer, num)) {
                        return false;
                    }

                    msg.num_params = num;
                }
                // parse each param
                while (*msg.num_params > 0) {
                    if (!msg.current_param.has_value()) {
                        uint64_t type{ 0 };
                        if (!ParseUintVField(buffer, type)) {
                            return false;
                        }

                        msg.current_param = MoqParameter{};
                        msg.current_param->type = type;
                    }

                    // decode param_len:<bytes>
                    auto param = buffer.DecodeBytes();
                    if (!param) {
                        return false;
                    }
                    msg.current_param.value().length = param->size();
                    msg.current_param.value().value = param.value();
                    msg.track_params.push_back(msg.current_param.value());
                    msg.current_param = std::nullopt;
                    *msg.num_params -= 1;
                }

                msg.parsing_completed = true;
                [[fallthrough]];
            }

            default:
                break;
        }

        if (!msg.parsing_completed) {
            return false;
        }

        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqSubscribe&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqSubscribe&);

    Serializer& operator<<(Serializer& buffer, const MoqUnsubscribe& msg)
    {
        buffer.Push(UintVar(static_cast<uint64_t>(MoqMessageType::UNSUBSCRIBE)).Bytes());
        buffer.Push(UintVar(msg.subscribe_id).Bytes());
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqUnsubscribe& msg)
    {
        return ParseUintVField(buffer, msg.subscribe_id);
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqUnsubscribe&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqUnsubscribe&);

    Serializer& operator<<(Serializer& buffer, const MoqSubscribeDone& msg)
    {
        buffer.Push(UintVar(static_cast<uint64_t>(MoqMessageType::SUBSCRIBE_DONE)).Bytes());
        buffer.Push(UintVar(msg.subscribe_id).Bytes());
        buffer.Push(UintVar(msg.status_code).Bytes());
        buffer.PushLengthBytes(msg.reason_phrase);
        msg.content_exists ? buffer.Push(static_cast<uint8_t>(1)) : buffer.Push(static_cast<uint8_t>(0));
        if (msg.content_exists) {
            buffer.Push(UintVar(msg.final_group_id).Bytes());
            buffer.Push(UintVar(msg.final_object_id).Bytes());
        }

        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqSubscribeDone& msg)
    {
        switch (msg.current_pos) {
            case 0: {
                if (!ParseUintVField(buffer, msg.subscribe_id)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 1: {
                if (!ParseUintVField(buffer, msg.status_code)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 2: {
                auto val = buffer.DecodeBytes();
                if (!val) {
                    return false;
                }
                msg.reason_phrase = std::move(val.value());
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 3: {
                auto val = buffer.Front();
                if (!val) {
                    return false;
                }
                buffer.Pop();
                msg.content_exists = (val.value()) == 1;
                msg.current_pos += 1;
                if (!msg.content_exists) {
                    // nothing more to process.
                    return true;
                }
                [[fallthrough]];
            }
            case 4: {
                if (!ParseUintVField(buffer, msg.final_group_id)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 5: {
                if (!ParseUintVField(buffer, msg.final_object_id)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            default:
                break;
        }

        if (msg.current_pos < msg.MAX_FIELDS) {
            return false;
        }
        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqSubscribeDone&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqSubscribeDone&);

    Serializer& operator<<(Serializer& buffer, const MoqSubscribeOk& msg)
    {
        buffer.Push(UintVar(static_cast<uint64_t>(MoqMessageType::SUBSCRIBE_OK)).Bytes());
        buffer.Push(UintVar(msg.subscribe_id).Bytes());
        buffer.Push(UintVar(msg.expires).Bytes());
        msg.content_exists ? buffer.Push(static_cast<uint8_t>(1)) : buffer.Push(static_cast<uint8_t>(0));
        if (msg.content_exists) {
            buffer.Push(UintVar(msg.largest_group).Bytes());
            buffer.Push(UintVar(msg.largest_object).Bytes());
        }
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqSubscribeOk& msg)
    {
        switch (msg.current_pos) {
            case 0: {
                if (!ParseUintVField(buffer, msg.subscribe_id)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 1: {
                if (!ParseUintVField(buffer, msg.expires)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 2: {
                auto val = buffer.Front();
                if (!val) {
                    return false;
                }
                buffer.Pop();
                msg.content_exists = (val.value()) == 1;
                msg.current_pos += 1;
                if (!msg.content_exists) {
                    // nothing more to process.
                    return true;
                }
                [[fallthrough]];
            }
            case 3: {
                if (!ParseUintVField(buffer, msg.largest_group)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 4: {
                if (!ParseUintVField(buffer, msg.largest_object)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            default:
                break;
        }

        if (msg.current_pos < msg.MAX_FIELDS) {
            return false;
        }
        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqSubscribeOk&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqSubscribeOk&);

    Serializer& operator<<(Serializer& buffer, const MoqSubscribeError& msg)
    {
        buffer.Push(UintVar(static_cast<uint64_t>(MoqMessageType::SUBSCRIBE_ERROR)).Bytes());
        buffer.Push(UintVar(msg.subscribe_id).Bytes());
        buffer.Push(UintVar(msg.err_code).Bytes());
        buffer.PushLengthBytes(msg.reason_phrase);
        buffer.Push(UintVar(msg.track_alias).Bytes());
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqSubscribeError& msg)
    {

        switch (msg.current_pos) {
            case 0: {
                if (!ParseUintVField(buffer, msg.subscribe_id)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 1: {
                if (!ParseUintVField(buffer, msg.err_code)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 2: {
                auto val = buffer.DecodeBytes();
                if (!val) {
                    return false;
                }
                msg.reason_phrase = std::move(val.value());
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 3: {
                if (!ParseUintVField(buffer, msg.track_alias)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            default:
                break;
        }

        if (msg.current_pos < msg.MAX_FIELDS) {
            return false;
        }
        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqSubscribeError&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqSubscribeError&);

    //
    // Announce
    //

    Serializer& operator<<(Serializer& buffer, const MoqAnnounce& msg)
    {
        buffer.Push(UintVar(static_cast<uint64_t>(MoqMessageType::ANNOUNCE)).Bytes());
        buffer.PushLengthBytes(msg.track_namespace);
        buffer.Push(UintVar(static_cast<uint64_t>(0)).Bytes());
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqAnnounce& msg)
    {

        // read namespace
        if (msg.track_namespace.empty()) {
            auto val = buffer.DecodeBytes();
            if (!val) {
                return false;
            }
            msg.track_namespace = *val;
        }

        if (!msg.num_params) {
            auto val = buffer.DecodeUintV();
            if (!val) {
                return false;
            }
            msg.num_params = *val;
        }

        // parse each param
        while (msg.num_params > 0) {
            if (!msg.current_param.type) {
                uint64_t type{ 0 };
                if (!ParseUintVField(buffer, type)) {
                    return false;
                }

                msg.current_param = {};
                msg.current_param.type = type;
            }

            // decode param_len:<bytes>
            auto param = buffer.DecodeBytes();
            if (!param) {
                return false;
            }

            msg.current_param.length = param->size();
            msg.current_param.value = param.value();
            msg.params.push_back(msg.current_param);
            msg.current_param = {};
            msg.num_params -= 1;
        }

        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqAnnounce&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqAnnounce&);

    Serializer& operator<<(Serializer& buffer, const MoqAnnounceOk& msg)
    {
        buffer.Push(UintVar(static_cast<uint64_t>(MoqMessageType::ANNOUNCE_OK)).Bytes());
        buffer.PushLengthBytes(msg.track_namespace);
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqAnnounceOk& msg)
    {

        // read namespace
        if (msg.track_namespace.empty()) {
            auto val = buffer.DecodeBytes();
            if (!val) {
                return false;
            }
            msg.track_namespace = *val;
        }
        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqAnnounceOk&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqAnnounceOk&);

    Serializer& operator<<(Serializer& buffer, const MoqAnnounceError& msg)
    {
        buffer.Push(UintVar(static_cast<uint64_t>(MoqMessageType::ANNOUNCE_ERROR)).Bytes());
        buffer.PushLengthBytes(msg.track_namespace.value());
        buffer.Push(UintVar(msg.err_code.value()).Bytes());
        buffer.PushLengthBytes(msg.reason_phrase.value());
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqAnnounceError& msg)
    {

        // read namespace
        if (!msg.track_namespace) {
            auto val = buffer.DecodeBytes();
            if (!val) {
                return false;
            }
            msg.track_namespace = *val;
        }

        if (!msg.err_code) {
            auto val = buffer.DecodeUintV();
            if (!val) {
                return false;
            }

            msg.err_code = *val;
        }
        while (!msg.reason_phrase > 0) {
            auto reason = buffer.DecodeBytes();
            if (!reason) {
                return false;
            }
            msg.reason_phrase = reason;
        }

        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqAnnounceError&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqAnnounceError&);

    Serializer& operator<<(Serializer& buffer, const MoqUnannounce& msg)
    {
        buffer.Push(UintVar(static_cast<uint64_t>(MoqMessageType::UNANNOUNCE)).Bytes());
        buffer.PushLengthBytes(msg.track_namespace);
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqUnannounce& msg)
    {

        // read namespace
        if (msg.track_namespace.empty()) {
            auto val = buffer.DecodeBytes();
            if (!val) {
                return false;
            }
            msg.track_namespace = *val;
        }
        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqUnannounce&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqUnannounce&);

    Serializer& operator<<(Serializer& buffer, const MoqAnnounceCancel& msg)
    {
        buffer.Push(UintVar(static_cast<uint64_t>(MoqMessageType::ANNOUNCE_CANCEL)).Bytes());
        buffer.PushLengthBytes(msg.track_namespace);
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqAnnounceCancel& msg)
    {

        // read namespace
        if (msg.track_namespace.empty()) {
            auto val = buffer.DecodeBytes();
            if (!val) {
                return false;
            }
            msg.track_namespace = *val;
        }
        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqAnnounceCancel&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqAnnounceCancel&);

    //
    // Goaway
    //

    Serializer& operator<<(Serializer& buffer, const MoqGoaway& msg)
    {
        buffer.Push(UintVar(static_cast<uint64_t>(MoqMessageType::GOAWAY)).Bytes());
        buffer.PushLengthBytes(msg.new_session_uri);
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqGoaway& msg)
    {

        auto val = buffer.DecodeBytes();
        if (!val) {
            return false;
        }
        msg.new_session_uri = std::move(val.value());
        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqGoaway&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqGoaway&);

    //
    // Object
    //

    Serializer& operator<<(Serializer& buffer, const MoqObjectStream& msg)
    {

        buffer.Push(UintVar(static_cast<uint64_t>(MoqMessageType::OBJECT_STREAM)).Bytes());
        buffer.Push(UintVar(msg.subscribe_id).Bytes());
        buffer.Push(UintVar(msg.track_alias).Bytes());
        buffer.Push(UintVar(msg.group_id).Bytes());
        buffer.Push(UintVar(msg.object_id).Bytes());
        buffer.Push(UintVar(msg.priority).Bytes());
        PushExtensions(buffer, msg.extensions);
        buffer.PushLengthBytes(msg.payload);
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqObjectStream& msg)
    {
        switch (msg.current_pos) {
            case 0: {
                if (!ParseUintVField(buffer, msg.subscribe_id)) {
                    return false;
                }
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
                if (!ParseUintVField(buffer, msg.priority)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 5:
                if (!ParseUintVField(buffer, msg.num_extensions)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            case 6:
                if (!ParseExtensions(buffer, msg.num_extensions, msg.extensions, msg.current_tag)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            case 7: {
                auto val = buffer.DecodeBytes();
                if (!val) {
                    return false;
                }
                msg.payload = std::move(val.value());
                msg.parse_completed = true;
                [[fallthrough]];
            }
            default:
                break;
        }

        if (!msg.parse_completed) {
            return false;
        }

        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqObjectStream&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqObjectStream&);

    Serializer& operator<<(Serializer& buffer, const MoqObjectDatagram& msg)
    {
        buffer.Push(UintVar(static_cast<uint64_t>(MoqMessageType::OBJECT_DATAGRAM)).Bytes());
        buffer.Push(UintVar(msg.subscribe_id).Bytes());
        buffer.Push(UintVar(msg.track_alias).Bytes());
        buffer.Push(UintVar(msg.group_id).Bytes());
        buffer.Push(UintVar(msg.object_id).Bytes());
        buffer.Push(UintVar(msg.priority).Bytes());
        PushExtensions(buffer, msg.extensions);
        buffer.PushLengthBytes(msg.payload);
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqObjectDatagram& msg)
    {
        switch (msg.current_pos) {
            case 0: {
                if (!ParseUintVField(buffer, msg.subscribe_id)) {
                    return false;
                }
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
                if (!ParseUintVField(buffer, msg.priority)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 5: {
                if (!ParseUintVField(buffer, msg.num_extensions)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 6:
                if (!ParseExtensions(buffer, msg.num_extensions, msg.extensions, msg.current_tag)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            case 7: {
                auto val = buffer.DecodeBytes();
                if (!val) {
                    return false;
                }
                msg.payload = std::move(val.value());
                msg.parse_completed = true;
                [[fallthrough]];
            }
            default:
                break;
        }

        if (!msg.parse_completed) {
            return false;
        }

        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqObjectDatagram&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqObjectDatagram&);

    Serializer& operator<<(Serializer& buffer, const MoqStreamHeaderTrack& msg)
    {

        buffer.Push(UintVar(static_cast<uint64_t>(MoqMessageType::STREAM_HEADER_TRACK)).Bytes());
        buffer.Push(UintVar(msg.subscribe_id).Bytes());
        buffer.Push(UintVar(msg.track_alias).Bytes());
        buffer.Push(UintVar(msg.priority).Bytes());
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqStreamHeaderTrack& msg)
    {
        switch (msg.current_pos) {
            case 0: {
                if (!ParseUintVField(buffer, msg.subscribe_id)) {
                    return false;
                }
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
                if (!ParseUintVField(buffer, msg.priority)) {
                    return false;
                }
                msg.current_pos += 1;
                msg.parse_completed = true;
                [[fallthrough]];
            }
            default:
                break;
        }

        if (!msg.parse_completed) {
            return false;
        }
        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqStreamHeaderTrack&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqStreamHeaderTrack&);

    Serializer& operator<<(Serializer& buffer, const MoqStreamTrackObject& msg)
    {
        buffer.Push(UintVar(msg.group_id).Bytes());
        buffer.Push(UintVar(msg.object_id).Bytes());
        PushExtensions(buffer, msg.extensions);
        buffer.PushLengthBytes(msg.payload);
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqStreamTrackObject& msg)
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
                if (!ParseUintVField(buffer, msg.object_id)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 2: {
                if (!ParseUintVField(buffer, msg.num_extensions)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 3: {
                if (!ParseExtensions(buffer, msg.num_extensions, msg.extensions, msg.current_tag)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 4: {
                auto val = buffer.DecodeBytes();
                if (!val) {
                    return false;
                }
                msg.payload = std::move(val.value());
                msg.parse_completed = true;
                [[fallthrough]];
            }
            default:
                break;
        }

        if (!msg.parse_completed) {
            return false;
        }
        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqStreamTrackObject&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqStreamTrackObject&);

    Serializer& operator<<(Serializer& buffer, const MoqStreamHeaderGroup& msg)
    {
        buffer.Push(UintVar(static_cast<uint64_t>(MoqMessageType::STREAM_HEADER_GROUP)).Bytes());
        buffer.Push(UintVar(msg.subscribe_id).Bytes());
        buffer.Push(UintVar(msg.track_alias).Bytes());
        buffer.Push(UintVar(msg.group_id).Bytes());
        buffer.Push(UintVar(msg.priority).Bytes());
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqStreamHeaderGroup& msg)
    {
        switch (msg.current_pos) {
            case 0: {
                if (!ParseUintVField(buffer, msg.subscribe_id)) {
                    return false;
                }
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
                if (!ParseUintVField(buffer, msg.priority)) {
                    return false;
                }
                msg.current_pos += 1;
                msg.parse_completed = true;
                [[fallthrough]];
            }
            default:
                break;
        }

        if (!msg.parse_completed) {
            return false;
        }

        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqStreamHeaderGroup&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqStreamHeaderGroup&);

    Serializer& operator<<(Serializer& buffer, const MoqStreamGroupObject& msg)
    {
        buffer.Push(UintVar(msg.object_id).Bytes());
        PushExtensions(buffer, msg.extensions);
        buffer.PushLengthBytes(msg.payload);
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqStreamGroupObject& msg)
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
                if (!ParseUintVField(buffer, msg.num_extensions)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 2: {
                if (!ParseExtensions(buffer, msg.num_extensions, msg.extensions, msg.current_tag)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 3: {
                auto val = buffer.DecodeBytes();
                if (!val) {
                    return false;
                }
                msg.payload = std::move(val.value());
                msg.parse_completed = true;
                [[fallthrough]];
            }
            default:
                break;
        }

        if (!msg.parse_completed) {
            return false;
        }

        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqStreamGroupObject&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqStreamGroupObject&);

    // Client Setup message
    Serializer& operator<<(Serializer& buffer, const MoqClientSetup& msg)
    {
        buffer.Push(UintVar(static_cast<uint64_t>(MoqMessageType::CLIENT_SETUP)).Bytes());
        buffer.Push(UintVar(msg.supported_versions.size()).Bytes());
        // versions
        for (const auto& ver : msg.supported_versions) {
            buffer.Push(UintVar(ver).Bytes());
        }

        /// num params
        buffer.Push(UintVar(static_cast<uint64_t>(2)).Bytes());
        // role param
        buffer.Push(UintVar(msg.role_parameter.type).Bytes());
        buffer.PushLengthBytes(msg.role_parameter.value);
        // endpoint_id param
        buffer.Push(UintVar(static_cast<uint64_t>(ParameterType::EndpointId)).Bytes());
        buffer.PushLengthBytes(msg.endpoint_id_parameter.value);

        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqClientSetup& msg)
    {
        switch (msg.current_pos) {
            case 0: {
                if (!ParseUintVField(buffer, msg.num_versions)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 1: {
                while (msg.num_versions > 0) {
                    uint64_t version{ 0 };
                    if (!ParseUintVField(buffer, version)) {
                        return false;
                    }
                    msg.supported_versions.push_back(version);
                    msg.num_versions -= 1;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 2: {
                if (!msg.num_params.has_value()) {
                    auto params = uint64_t{ 0 };
                    if (!ParseUintVField(buffer, params)) {
                        return false;
                    }
                    msg.num_params = params;
                }
                while (msg.num_params > 0) {
                    if (!msg.current_param.has_value()) {
                        uint64_t type{ 0 };
                        if (!ParseUintVField(buffer, type)) {
                            return false;
                        }

                        msg.current_param = MoqParameter{};
                        msg.current_param->type = type;
                    }

                    auto param = buffer.DecodeBytes();
                    if (!param) {
                        return false;
                    }
                    msg.current_param->length = param->size();
                    msg.current_param->value = param.value();

                    switch (static_cast<ParameterType>(msg.current_param->type)) {
                        case ParameterType::Role:
                            msg.role_parameter = std::move(msg.current_param.value());
                            break;
                        case ParameterType::Path:
                            msg.path_parameter = std::move(msg.current_param.value());
                            break;
                        case ParameterType::EndpointId:
                            msg.endpoint_id_parameter = std::move(msg.current_param.value());
                            break;
                        default:
                            break;
                    }

                    msg.current_param = std::nullopt;
                    msg.num_params.value() -= 1;
                }

                msg.parse_completed = true;
                [[fallthrough]];
            }
            default:
                break;
        }

        if (!msg.parse_completed) {
            return false;
        }

        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqClientSetup&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqClientSetup&);

    // Server Setup message

    Serializer& operator<<(Serializer& buffer, const MoqServerSetup& msg)
    {

        buffer.Push(UintVar(static_cast<uint64_t>(MoqMessageType::SERVER_SETUP)).Bytes());
        buffer.Push(UintVar(msg.selection_version).Bytes());

        /// num params
        buffer.Push(UintVar(static_cast<uint64_t>(2)).Bytes());
        // role param
        buffer.Push(UintVar(static_cast<uint64_t>(msg.role_parameter.type)).Bytes());
        buffer.PushLengthBytes(msg.role_parameter.value);

        // endpoint_id param
        buffer.Push(UintVar(static_cast<uint64_t>(ParameterType::EndpointId)).Bytes());
        buffer.PushLengthBytes(msg.endpoint_id_parameter.value);

        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqServerSetup& msg)
    {
        switch (msg.current_pos) {
            case 0: {
                if (!ParseUintVField(buffer, msg.selection_version)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 1: {
                if (!msg.num_params.has_value()) {
                    auto params = uint64_t{ 0 };
                    if (!ParseUintVField(buffer, params)) {
                        return false;
                    }
                    msg.num_params = params;
                }
                while (msg.num_params > 0) {
                    if (!msg.current_param.has_value()) {
                        uint64_t type{ 0 };
                        if (!ParseUintVField(buffer, type)) {
                            return false;
                        }

                        msg.current_param = MoqParameter{};
                        msg.current_param->type = type;
                    }

                    auto param = buffer.DecodeBytes();
                    if (!param) {
                        return false;
                    }
                    msg.current_param->length = param->size();
                    msg.current_param->value = param.value();

                    switch (static_cast<ParameterType>(msg.current_param->type)) {
                        case ParameterType::Role:
                            msg.role_parameter = std::move(msg.current_param.value());
                            break;
                        case ParameterType::Path:
                            msg.path_parameter = std::move(msg.current_param.value());
                            break;
                        case ParameterType::EndpointId:
                            msg.endpoint_id_parameter = std::move(msg.current_param.value());
                            break;
                        default:
                            break;
                    }

                    msg.current_param = std::nullopt;
                    msg.num_params.value() -= 1;
                }
                msg.parse_completed = true;
                [[fallthrough]];
            }
            default:
                break;
        }

        if (!msg.parse_completed) {
            return false;
        }

        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqServerSetup&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqServerSetup&);

}
