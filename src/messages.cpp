// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/detail/messages.h"

namespace quicr::messages {

    Bytes& operator<<(Bytes& buffer, BytesSpan bytes)
    {
        buffer.insert(buffer.end(), bytes.begin(), bytes.end());
        return buffer;
    }

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

    static void PushExtensions(Bytes& buffer, const std::optional<Extensions>& extensions)
    {
        if (!extensions.has_value()) {
            buffer.push_back(0);
            return;
        }

        buffer << UintVar(extensions.value().size());
        for (const auto& extension : extensions.value()) {
            buffer << UintVar(extension.first);
            buffer << UintVar(extension.second.size());
            buffer << extension.second;
        }
    }

    //
    // MoqParameter
    //

    Bytes& operator<<(Bytes& buffer, const MoqParameter& param)
    {
        buffer << UintVar(param.type);
        buffer << UintVar(param.length);
        if (param.length) {
            buffer << param.value;
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
    Bytes& operator<<(Bytes& buffer, const MoqTrackStatus& msg)
    {
        buffer << UintVar(static_cast<uint64_t>(MoqMessageType::TRACK_STATUS));
        buffer << UintVar(msg.track_namespace.size());
        buffer << msg.track_namespace;
        buffer << UintVar(msg.track_name.size());
        buffer << msg.track_name;
        buffer << UintVar(static_cast<uint64_t>(msg.status_code));
        buffer << UintVar(msg.last_group_id);
        buffer << UintVar(msg.last_object_id);

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

    Bytes& operator<<(Bytes& buffer, const MoqTrackStatusRequest& msg)
    {
        buffer << UintVar(static_cast<uint64_t>(MoqMessageType::TRACK_STATUS_REQUEST));
        buffer << UintVar(msg.track_namespace.size());
        buffer << msg.track_namespace;
        buffer << UintVar(msg.track_name.size());
        buffer << msg.track_name;

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

    Bytes& operator<<(Bytes& buffer, const MoqSubscribe& msg)
    {
        buffer << UintVar(static_cast<uint64_t>(MoqMessageType::SUBSCRIBE));
        buffer << UintVar(msg.subscribe_id);
        buffer << UintVar(msg.track_alias);
        buffer << UintVar(msg.track_namespace.size());
        buffer << msg.track_namespace;
        buffer << UintVar(msg.track_name.size());
        buffer << msg.track_name;
        buffer << UintVar(static_cast<uint64_t>(msg.filter_type));

        switch (msg.filter_type) {
            case FilterType::None:
            case FilterType::LatestGroup:
            case FilterType::LatestObject:
                break;
            case FilterType::AbsoluteStart: {
                buffer << UintVar(msg.start_group);
                buffer << UintVar(msg.start_object);
            } break;
            case FilterType::AbsoluteRange:
                buffer << UintVar(msg.start_group);
                buffer << UintVar(msg.start_object);
                buffer << UintVar(msg.end_group);
                buffer << UintVar(msg.end_object);
                break;
        }

        buffer << UintVar(msg.track_params.size());
        for (const auto& param : msg.track_params) {
            buffer << UintVar(static_cast<uint64_t>(param.type));
            buffer << UintVar(param.value.size());
            buffer << param.value;
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

    Bytes& operator<<(Bytes& buffer, const MoqUnsubscribe& msg)
    {
        buffer << UintVar(static_cast<uint64_t>(MoqMessageType::UNSUBSCRIBE));
        buffer << UintVar(msg.subscribe_id);
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqUnsubscribe& msg)
    {
        return ParseUintVField(buffer, msg.subscribe_id);
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqUnsubscribe&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqUnsubscribe&);

    Bytes& operator<<(Bytes& buffer, const MoqSubscribeDone& msg)
    {
        buffer << UintVar(static_cast<uint64_t>(MoqMessageType::SUBSCRIBE_DONE));
        buffer << UintVar(msg.subscribe_id);
        buffer << UintVar(msg.status_code);
        buffer << UintVar(msg.reason_phrase.size());
        buffer << msg.reason_phrase;
        msg.content_exists ? buffer.push_back(static_cast<uint8_t>(1)) : buffer.push_back(static_cast<uint8_t>(0));
        if (msg.content_exists) {
            buffer << UintVar(msg.final_group_id);
            buffer << UintVar(msg.final_object_id);
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

    Bytes& operator<<(Bytes& buffer, const MoqSubscribeOk& msg)
    {
        buffer << UintVar(static_cast<uint64_t>(MoqMessageType::SUBSCRIBE_OK));
        buffer << UintVar(msg.subscribe_id);
        buffer << UintVar(msg.expires);
        msg.content_exists ? buffer.push_back(static_cast<uint8_t>(1)) : buffer.push_back(static_cast<uint8_t>(0));
        if (msg.content_exists) {
            buffer << UintVar(msg.largest_group);
            buffer << UintVar(msg.largest_object);
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

    Bytes& operator<<(Bytes& buffer, const MoqSubscribeError& msg)
    {
        buffer << UintVar(static_cast<uint64_t>(MoqMessageType::SUBSCRIBE_ERROR));
        buffer << UintVar(msg.subscribe_id);
        buffer << UintVar(msg.err_code);
        buffer << UintVar(msg.reason_phrase.size());
        buffer << msg.reason_phrase;
        buffer << UintVar(msg.track_alias);
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

    Bytes& operator<<(Bytes& buffer, const MoqAnnounce& msg)
    {
        buffer << UintVar(static_cast<uint64_t>(MoqMessageType::ANNOUNCE));
        buffer << UintVar(msg.track_namespace.size());
        buffer << msg.track_namespace;
        buffer << UintVar(static_cast<uint64_t>(0));
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

    Bytes& operator<<(Bytes& buffer, const MoqAnnounceOk& msg)
    {
        buffer << UintVar(static_cast<uint64_t>(MoqMessageType::ANNOUNCE_OK));
        buffer << UintVar(msg.track_namespace.size());
        buffer << msg.track_namespace;
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

    Bytes& operator<<(Bytes& buffer, const MoqAnnounceError& msg)
    {
        buffer << UintVar(static_cast<uint64_t>(MoqMessageType::ANNOUNCE_ERROR));
        buffer << UintVar(msg.track_namespace.value().size());
        buffer << msg.track_namespace.value();
        buffer << UintVar(msg.err_code.value());
        buffer << UintVar(msg.reason_phrase.value().size());
        buffer << msg.reason_phrase.value();
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
        while (!msg.reason_phrase.has_value()) {
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

    Bytes& operator<<(Bytes& buffer, const MoqUnannounce& msg)
    {
        buffer << UintVar(static_cast<uint64_t>(MoqMessageType::UNANNOUNCE));
        buffer << UintVar(msg.track_namespace.size());
        buffer << msg.track_namespace;
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

    Bytes& operator<<(Bytes& buffer, const MoqAnnounceCancel& msg)
    {
        buffer << UintVar(static_cast<uint64_t>(MoqMessageType::ANNOUNCE_CANCEL));
        buffer << UintVar(msg.track_namespace.size());
        buffer << msg.track_namespace;
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

    Bytes& operator<<(Bytes& buffer, const MoqGoaway& msg)
    {
        buffer << UintVar(static_cast<uint64_t>(MoqMessageType::GOAWAY));
        buffer << UintVar(msg.new_session_uri.size());
        buffer << msg.new_session_uri;
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

    Bytes& operator<<(Bytes& buffer, const MoqObjectDatagram& msg)
    {
        buffer << UintVar(static_cast<uint64_t>(MoqMessageType::OBJECT_DATAGRAM));
        buffer << UintVar(msg.subscribe_id);
        buffer << UintVar(msg.track_alias);
        buffer << UintVar(msg.group_id);
        buffer << UintVar(msg.object_id);
        buffer << UintVar(msg.priority);
        PushExtensions(buffer, msg.extensions);
        buffer << UintVar(msg.payload.size());
        buffer << msg.payload;
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

    Bytes& operator<<(Bytes& buffer, const MoqStreamHeaderSubGroup& msg)
    {
        buffer << UintVar(static_cast<uint64_t>(MoqMessageType::STREAM_HEADER_SUBGROUP));
        buffer << UintVar(msg.track_alias);
        buffer << UintVar(msg.subscribe_id);
        buffer << UintVar(msg.group_id);
        buffer << UintVar(msg.subgroup_id);
        buffer << UintVar(msg.priority);
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqStreamHeaderSubGroup& msg)
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
                if (!ParseUintVField(buffer, msg.subscribe_id)) {
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
                if (!ParseUintVField(buffer, msg.subgroup_id)) {
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

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqStreamHeaderSubGroup&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqStreamHeaderSubGroup&);

    Bytes& operator<<(Bytes& buffer, const MoqStreamSubGroupObject& msg)
    {
        buffer << UintVar(msg.object_id);
        PushExtensions(buffer, msg.extensions);
        buffer << UintVar(msg.payload.size());
        buffer << msg.payload;
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, MoqStreamSubGroupObject& msg)
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

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, MoqStreamSubGroupObject&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, MoqStreamSubGroupObject&);

    // Client Setup message
    Bytes& operator<<(Bytes& buffer, const MoqClientSetup& msg)
    {
        buffer << UintVar(static_cast<uint64_t>(MoqMessageType::CLIENT_SETUP));
        buffer << UintVar(msg.supported_versions.size());
        // versions
        for (const auto& ver : msg.supported_versions) {
            buffer << UintVar(ver);
        }

        /// num params
        buffer << UintVar(static_cast<uint64_t>(2));
        // role param
        buffer << UintVar(msg.role_parameter.type);
        buffer << UintVar(msg.role_parameter.value.size());
        buffer << msg.role_parameter.value;
        // endpoint_id param
        buffer << UintVar(static_cast<uint64_t>(ParameterType::EndpointId));
        buffer << UintVar(msg.endpoint_id_parameter.value.size());
        buffer << msg.endpoint_id_parameter.value;

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

    Bytes& operator<<(Bytes& buffer, const MoqServerSetup& msg)
    {
        buffer << UintVar(static_cast<uint64_t>(MoqMessageType::SERVER_SETUP));
        buffer << UintVar(msg.selection_version);

        /// num params
        buffer << UintVar(static_cast<uint64_t>(2));
        // role param
        buffer << UintVar(static_cast<uint64_t>(msg.role_parameter.type));
        buffer << UintVar(msg.role_parameter.value.size());
        buffer << msg.role_parameter.value;

        // endpoint_id param
        buffer << UintVar(static_cast<uint64_t>(ParameterType::EndpointId));
        buffer << UintVar(msg.endpoint_id_parameter.value.size());
        buffer << msg.endpoint_id_parameter.value;

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
