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

    BytesSpan operator>>(BytesSpan buffer, uint64_t& value)
    {
        UintVar value_uv(buffer);
        value = static_cast<uint64_t>(value_uv);
        return buffer.subspan(value_uv.size());
    }

    BytesSpan operator>>(BytesSpan buffer, Bytes& value)
    {
        uint64_t size = 0;
        buffer = buffer >> size;
        value.assign(buffer.begin(), std::next(buffer.begin(), size));
        return buffer.subspan(value.size());
    }

    //
    // Parameter
    //

    Bytes& operator<<(Bytes& buffer, const Parameter& param)
    {
        buffer << UintVar(param.type);
        buffer << UintVar(param.length);
        if (param.length) {
            buffer << param.value;
        }
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, Parameter& param)
    {
        buffer = (buffer >> param.type >> param.value);
        param.length = param.value.size();
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, Parameter& param)
    {
        if (!ParseUintVField(buffer, param.type)) {
            return false;
        }

        auto val = buffer.DecodeBytes();
        if (!val) {
            return false;
        }

        param.length = val->size();
        param.value = std::move(val.value());

        return true;
    }

    Bytes& operator<<(Bytes& buffer, const TrackNamespace& ns)
    {
        const auto& entries = ns.GetEntries();

        buffer << UintVar(entries.size());
        for (const auto& entry : entries) {
            buffer << UintVar(entry.size());
            buffer << entry;
        }

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, TrackNamespace& msg)
    {
        uint64_t size = 0;
        buffer = buffer >> size;

        std::vector<Bytes> entries(size);
        for (auto& entry : entries) {
            buffer = buffer >> entry;
        }

        msg = TrackNamespace{ entries };

        return buffer;
    }

    // Client Setup message
    Bytes& operator<<(Bytes& buffer, const ClientSetup& msg)
    {
        Bytes payload;
        payload << UintVar(msg.supported_versions.size());
        // versions
        for (const auto& ver : msg.supported_versions) {
            payload << UintVar(ver);
        }

        /// num params
        payload << UintVar(static_cast<uint64_t>(2));
        // role param
        payload << UintVar(msg.role_parameter.type);
        payload << UintVar(msg.role_parameter.value.size());
        payload << msg.role_parameter.value;
        // endpoint_id param
        payload << UintVar(static_cast<uint64_t>(ParameterType::kEndpointId));
        payload << UintVar(msg.endpoint_id_parameter.value.size());
        payload << msg.endpoint_id_parameter.value;

        buffer << UintVar(static_cast<uint64_t>(ControlMessageType::kClientSetup));
        buffer << UintVar(payload.size());
        buffer << payload;

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, ClientSetup& msg)
    {
        buffer = buffer >> msg.num_versions;

        for (uint64_t i = 0; i < msg.num_versions; ++i) {
            uint64_t version{ 0 };
            buffer = buffer >> version;
            msg.supported_versions.push_back(version);
        }

        uint64_t num_params = 0;
        buffer = buffer >> num_params;

        for (uint64_t i = 0; i < num_params; ++i) {
            Parameter param;
            buffer = buffer >> param;

            switch (static_cast<ParameterType>(param.type)) {
                case ParameterType::kRole:
                    msg.role_parameter = std::move(param);
                    break;
                case ParameterType::kPath:
                    msg.path_parameter = std::move(param);
                    break;
                case ParameterType::kEndpointId:
                    msg.endpoint_id_parameter = std::move(param);
                    break;
                default:
                    break;
            }
        }

        return buffer;
    }

    // Server Setup message

    Bytes& operator<<(Bytes& buffer, const ServerSetup& msg)
    {
        Bytes payload;
        payload << UintVar(msg.selection_version);

        /// num params
        payload << UintVar(static_cast<uint64_t>(2));
        // role param
        payload << UintVar(static_cast<uint64_t>(msg.role_parameter.type));
        payload << UintVar(msg.role_parameter.value.size());
        payload << msg.role_parameter.value;

        // endpoint_id param
        payload << UintVar(static_cast<uint64_t>(ParameterType::kEndpointId));
        payload << UintVar(msg.endpoint_id_parameter.value.size());
        payload << msg.endpoint_id_parameter.value;

        buffer << UintVar(static_cast<uint64_t>(ControlMessageType::kServerSetup));
        buffer << UintVar(payload.size());
        buffer << payload;

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, ServerSetup& msg)
    {
        buffer = buffer >> msg.selection_version;

        uint64_t num_params = 0;
        buffer = buffer >> num_params;

        for (uint64_t i = 0; i < num_params; ++i) {
            Parameter param;
            buffer = buffer >> param;

            switch (static_cast<ParameterType>(param.type)) {
                case ParameterType::kRole:
                    msg.role_parameter = std::move(param);
                    break;
                case ParameterType::kPath:
                    msg.path_parameter = std::move(param);
                    break;
                case ParameterType::kEndpointId:
                    msg.endpoint_id_parameter = std::move(param);
                    break;
                default:
                    break;
            }
        }

        return buffer;
    }

    //
    // New Group Request
    //

    Bytes& operator<<(Bytes& buffer, const NewGroupRequest& msg)
    {
        Bytes payload;
        payload << UintVar(msg.subscribe_id);
        payload << UintVar(msg.track_alias);

        buffer << UintVar(static_cast<uint64_t>(ControlMessageType::kNewGroup));
        buffer << UintVar(payload.size());
        buffer << payload;

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, NewGroupRequest& msg)
    {
        buffer = buffer >> msg.subscribe_id;
        buffer = buffer >> msg.track_alias;

        return buffer;
    }

    //
    // Track Status
    //
    Bytes& operator<<(Bytes& buffer, const TrackStatus& msg)
    {
        Bytes payload;
        payload << msg.track_namespace;
        payload << UintVar(msg.track_name.size());
        payload << msg.track_name;
        payload << UintVar(static_cast<uint64_t>(msg.status_code));
        payload << UintVar(msg.last_group_id);
        payload << UintVar(msg.last_object_id);

        buffer << UintVar(static_cast<uint64_t>(ControlMessageType::kTrackStatus));
        buffer << UintVar(payload.size());
        buffer << payload;

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, TrackStatus& msg)
    {
        buffer = buffer >> msg.track_namespace;
        buffer = buffer >> msg.track_name;

        uint64_t status_code = 0;
        buffer = buffer >> status_code;
        msg.status_code = static_cast<TrackStatusCode>(status_code);

        buffer = buffer >> msg.last_group_id;
        buffer = buffer >> msg.last_object_id;

        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, const TrackStatusRequest& msg)
    {
        Bytes payload;
        payload << msg.track_namespace;
        payload << UintVar(msg.track_name.size());
        payload << msg.track_name;

        buffer << UintVar(static_cast<uint64_t>(ControlMessageType::kTrackStatusRequest));
        buffer << UintVar(payload.size());
        buffer << payload;

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, TrackStatusRequest& msg)
    {
        return buffer >> msg.track_namespace >> msg.track_name;
    }

    //
    // Subscribe
    //

    Bytes& operator<<(Bytes& buffer, const Subscribe& msg)
    {
        Bytes payload;
        payload << UintVar(msg.subscribe_id);
        payload << UintVar(msg.track_alias);
        payload << msg.track_namespace;
        payload << UintVar(msg.track_name.size());
        payload << msg.track_name;
        payload.push_back(msg.priority);
        auto group_order = static_cast<uint8_t>(msg.group_order);
        payload.push_back(group_order);
        payload << UintVar(static_cast<uint64_t>(msg.filter_type));
        switch (msg.filter_type) {
            case FilterType::kNone:
            case FilterType::kLatestGroup:
            case FilterType::kLatestObject:
                break;
            case FilterType::kAbsoluteStart: {
                payload << UintVar(msg.start_group);
                payload << UintVar(msg.start_object);
            } break;
            case FilterType::kAbsoluteRange:
                payload << UintVar(msg.start_group);
                payload << UintVar(msg.start_object);
                payload << UintVar(msg.end_group);
                break;
            default:
                throw std::runtime_error("Malformed filter type");
        }

        payload << UintVar(msg.track_params.size());
        for (const auto& param : msg.track_params) {
            payload << param;
        }

        buffer << UintVar(static_cast<uint64_t>(ControlMessageType::kSubscribe));
        buffer << UintVar(payload.size());
        buffer << payload;

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, Subscribe& msg)
    {
        buffer = buffer >> msg.subscribe_id;
        buffer = buffer >> msg.track_alias;
        buffer = buffer >> msg.track_namespace;
        buffer = buffer >> msg.track_name;
        msg.priority = buffer.front();
        buffer = buffer.subspan(sizeof(ObjectPriority));
        msg.group_order = static_cast<GroupOrder>(buffer.front());
        buffer = buffer.subspan(sizeof(GroupOrder));
        uint64_t filter = 0;
        buffer = buffer >> filter;
        msg.filter_type = static_cast<FilterType>(filter);

        if (msg.filter_type == FilterType::kAbsoluteStart || msg.filter_type == FilterType::kAbsoluteRange) {
            buffer = buffer >> msg.start_group;
            buffer = buffer >> msg.start_object;

            if (msg.filter_type == FilterType::kAbsoluteRange) {
                buffer = buffer >> msg.end_group;
            }
        }

        uint64_t num_params = 0;
        buffer = buffer >> num_params;

        for (uint64_t i = 0; i < num_params; ++i) {
            Parameter param;
            buffer = buffer >> param;
            msg.track_params.push_back(param);
        }

        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, const SubscribeUpdate& msg)
    {
        Bytes payload;
        payload << UintVar(msg.subscribe_id);
        payload << UintVar(msg.start_group);
        payload << UintVar(msg.start_object);
        payload << UintVar(msg.end_group);
        payload.push_back(msg.priority);

        payload << UintVar(msg.track_params.size());
        for (const auto& param : msg.track_params) {
            payload << param;
        }

        buffer << UintVar(static_cast<uint64_t>(ControlMessageType::kSubscribeUpdate));
        buffer << UintVar(payload.size());
        buffer << payload;

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, SubscribeUpdate& msg)
    {
        buffer = buffer >> msg.subscribe_id;
        buffer = buffer >> msg.start_group;
        buffer = buffer >> msg.start_object;
        buffer = buffer >> msg.end_group;
        msg.priority = buffer.front();
        buffer = buffer.subspan(sizeof(ObjectPriority));

        uint64_t num_params = 0;
        buffer = buffer >> num_params;

        for (uint64_t i = 0; i < num_params; ++i) {
            Parameter param;
            buffer = buffer >> param;
            msg.track_params.push_back(param);
        }

        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, const Unsubscribe& msg)
    {
        Bytes payload;
        payload << UintVar(msg.subscribe_id);

        buffer << UintVar(static_cast<uint64_t>(ControlMessageType::kUnsubscribe));
        buffer << UintVar(payload.size());
        buffer << payload;

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, Unsubscribe& msg)
    {
        return buffer >> msg.subscribe_id;
    }

    Bytes& operator<<(Bytes& buffer, const SubscribeDone& msg)
    {
        Bytes payload;
        payload << UintVar(msg.subscribe_id);
        payload << UintVar(msg.status_code);
        payload << UintVar(msg.reason_phrase.size());
        payload << msg.reason_phrase;
        msg.content_exists ? payload.push_back(static_cast<uint8_t>(1)) : payload.push_back(static_cast<uint8_t>(0));
        if (msg.content_exists) {
            payload << UintVar(msg.final_group_id);
            payload << UintVar(msg.final_object_id);
        }

        buffer << UintVar(static_cast<uint64_t>(ControlMessageType::kSubscribeDone));
        buffer << UintVar(payload.size());
        buffer << payload;

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, SubscribeDone& msg)
    {
        buffer = buffer >> msg.subscribe_id;
        buffer = buffer >> msg.status_code;
        buffer = buffer >> msg.reason_phrase;

        msg.content_exists = static_cast<bool>(buffer.front());
        buffer = buffer.subspan(1);

        if (!msg.content_exists) {
            return buffer;
        }

        buffer = buffer >> msg.final_group_id;
        buffer = buffer >> msg.final_object_id;

        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, const SubscribeOk& msg)
    {
        Bytes payload;
        payload << UintVar(msg.subscribe_id);
        payload << UintVar(msg.expires);
        msg.content_exists ? payload.push_back(static_cast<uint8_t>(1)) : payload.push_back(static_cast<uint8_t>(0));
        if (msg.content_exists) {
            payload << UintVar(msg.largest_group);
            payload << UintVar(msg.largest_object);
        }

        buffer << UintVar(static_cast<uint64_t>(ControlMessageType::kSubscribeOk));
        buffer << UintVar(payload.size());
        buffer << payload;

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, SubscribeOk& msg)
    {
        buffer = buffer >> msg.subscribe_id;
        buffer = buffer >> msg.expires;

        msg.content_exists = static_cast<bool>(buffer.front());
        buffer = buffer.subspan(1);

        if (!msg.content_exists) {
            return buffer;
        }

        buffer = buffer >> msg.largest_group;
        buffer = buffer >> msg.largest_object;

        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, const SubscribeError& msg)
    {
        Bytes payload;
        payload << UintVar(msg.subscribe_id);
        payload << UintVar(msg.err_code);
        payload << UintVar(msg.reason_phrase.size());
        payload << msg.reason_phrase;
        payload << UintVar(msg.track_alias);

        buffer << UintVar(static_cast<uint64_t>(ControlMessageType::kSubscribeError));
        buffer << UintVar(payload.size());
        buffer << payload;

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, SubscribeError& msg)
    {
        return buffer >> msg.subscribe_id >> msg.err_code >> msg.reason_phrase >> msg.track_alias;
    }

    //
    // Announce
    //

    Bytes& operator<<(Bytes& buffer, const Announce& msg)
    {
        Bytes payload;
        payload << msg.track_namespace;
        payload << UintVar(static_cast<uint64_t>(0));

        buffer << UintVar(static_cast<uint64_t>(ControlMessageType::kAnnounce));
        buffer << UintVar(payload.size());
        buffer << payload;

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, Announce& msg)
    {
        buffer = buffer >> msg.track_namespace;

        uint64_t num_params = 0;
        buffer = buffer >> num_params;

        for (uint64_t i = 0; i < num_params; ++i) {
            Parameter param;
            buffer = buffer >> param;
            msg.params.push_back(param);
        }

        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, const AnnounceOk& msg)
    {
        Bytes payload;
        payload << msg.track_namespace;

        buffer << UintVar(static_cast<uint64_t>(ControlMessageType::kAnnounceOk));
        buffer << UintVar(payload.size());
        buffer << payload;

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, AnnounceOk& msg)
    {
        return buffer >> msg.track_namespace;
    }

    Bytes& operator<<(Bytes& buffer, const AnnounceError& msg)
    {
        Bytes payload;
        payload << msg.track_namespace.value();
        payload << UintVar(msg.err_code.value());
        payload << UintVar(msg.reason_phrase.value().size());
        payload << msg.reason_phrase.value();

        buffer << UintVar(static_cast<uint64_t>(ControlMessageType::kAnnounceError));
        buffer << UintVar(payload.size());
        buffer << payload;

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, AnnounceError& msg)
    {
        TrackNamespace track_ns;
        buffer = buffer >> track_ns;
        msg.track_namespace = track_ns;

        ErrorCode err_code;
        buffer = buffer >> err_code;
        msg.err_code = err_code;

        ReasonPhrase reason_phrase;
        buffer = buffer >> reason_phrase;
        msg.reason_phrase = reason_phrase;

        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, const Unannounce& msg)
    {
        Bytes payload;
        payload << msg.track_namespace;

        buffer << UintVar(static_cast<uint64_t>(ControlMessageType::kUnannounce));
        buffer << UintVar(payload.size());
        buffer << payload;

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, Unannounce& msg)
    {
        return buffer >> msg.track_namespace;
    }

    Bytes& operator<<(Bytes& buffer, const AnnounceCancel& msg)
    {
        Bytes payload;
        payload << msg.track_namespace;

        buffer << UintVar(static_cast<uint64_t>(ControlMessageType::kAnnounceCancel));
        buffer << UintVar(payload.size());
        buffer << payload;

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, AnnounceCancel& msg)
    {
        return buffer >> msg.track_namespace;
    }

    //
    // GoAway
    //

    Bytes& operator<<(Bytes& buffer, const GoAway& msg)
    {
        Bytes payload;
        payload << UintVar(msg.new_session_uri.size());
        payload << msg.new_session_uri;

        buffer << UintVar(static_cast<uint64_t>(ControlMessageType::kGoAway));
        buffer << UintVar(payload.size());
        buffer << payload;

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, GoAway& msg)
    {
        return buffer >> msg.new_session_uri;
    }

    //
    // Fetch
    //

    Bytes& operator<<(Bytes& buffer, const Fetch& msg)
    {
        Bytes payload;

        payload << UintVar(msg.subscribe_id);
        payload << msg.track_namespace;
        payload << UintVar(msg.track_name.size());
        payload << msg.track_name;
        payload.push_back(msg.priority);
        auto group_order = static_cast<uint8_t>(msg.group_order);
        payload.push_back(group_order);
        payload << UintVar(msg.start_group);
        payload << UintVar(msg.start_object);
        payload << UintVar(msg.end_group);
        payload << UintVar(msg.end_object);

        payload << UintVar(msg.params.size());
        for (const auto& param : msg.params) {
            payload << param;
        }

        buffer << UintVar(static_cast<uint64_t>(ControlMessageType::kFetch));
        buffer << UintVar(payload.size());
        buffer << payload;

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, Fetch& msg)
    {
        buffer = buffer >> msg.subscribe_id;
        buffer = buffer >> msg.track_namespace;
        buffer = buffer >> msg.track_name;
        msg.priority = buffer.front();
        buffer = buffer.subspan(sizeof(ObjectPriority));
        msg.group_order = static_cast<GroupOrder>(buffer.front());
        buffer = buffer.subspan(sizeof(uint8_t));
        buffer = buffer >> msg.start_group;
        buffer = buffer >> msg.start_object;
        buffer = buffer >> msg.end_group;
        buffer = buffer >> msg.end_object;

        uint64_t num_params = 0;
        buffer = buffer >> num_params;

        for (uint64_t i = 0; i < num_params; ++i) {
            Parameter param;
            buffer = buffer >> param;
            msg.params.push_back(param);
        }

        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, const FetchOk& msg)
    {
        Bytes payload;
        payload << UintVar(msg.subscribe_id);
        auto group_order = static_cast<uint8_t>(msg.group_order);
        payload.push_back(group_order);
        payload.push_back(static_cast<uint8_t>(msg.end_of_track));
        payload << UintVar(msg.largest_group);
        payload << UintVar(msg.largest_object);

        payload << UintVar(msg.params.size());
        for (const auto& param : msg.params) {
            payload << param;
        }

        buffer << UintVar(static_cast<uint64_t>(ControlMessageType::kFetchOk));
        buffer << UintVar(payload.size());
        buffer << payload;

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, FetchOk& msg)
    {
        buffer = buffer >> msg.subscribe_id;
        msg.group_order = static_cast<GroupOrder>(buffer.front());
        buffer = buffer.subspan(sizeof(GroupOrder));
        msg.end_of_track = static_cast<bool>(buffer.front());
        buffer = buffer.subspan(sizeof(uint8_t));
        buffer = buffer >> msg.largest_group;
        buffer = buffer >> msg.largest_object;

        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, const FetchCancel& msg)
    {
        Bytes payload;
        payload << UintVar(msg.subscribe_id);

        buffer << UintVar(static_cast<uint64_t>(ControlMessageType::kFetchCancel));
        buffer << UintVar(payload.size());
        buffer << payload;

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, FetchCancel& msg)
    {
        return buffer >> msg.subscribe_id;
    }

    Bytes& operator<<(Bytes& buffer, const FetchError& msg)
    {
        Bytes payload;
        payload << UintVar(msg.subscribe_id);
        payload << UintVar(msg.err_code);
        payload << UintVar(msg.reason_phrase.size());
        payload << msg.reason_phrase;

        buffer << UintVar(static_cast<uint64_t>(ControlMessageType::kFetchError));
        buffer << UintVar(payload.size());
        buffer << payload;

        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, FetchError& msg)
    {
        return buffer >> msg.subscribe_id >> msg.err_code >> msg.reason_phrase;
    }

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
        buffer << UintVar(msg.payload.size());
        if (msg.payload.empty()) {
            // empty payload needs a object status to be set
            buffer << UintVar(static_cast<uint8_t>(msg.object_status));
            PushExtensions(buffer, msg.extensions);
        } else {
            PushExtensions(buffer, msg.extensions);
            buffer << msg.payload;
        }
        buffer << msg.payload;
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
                if (!ParseUintVField(buffer, msg.payload_len)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 5: {
                if (msg.payload_len == 0) {
                    uint64_t status = 0;
                    if (!ParseUintVField(buffer, status)) {
                        return false;
                    }
                    msg.object_status = static_cast<ObjectStatus>(status);
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }

            case 6: {
                if (!ParseExtensions(buffer, msg.num_extensions, msg.extensions, msg.current_tag)) {
                    return false;
                }
                msg.current_pos += 1;
                if (msg.payload_len == 0) {
                    msg.parse_completed = true;
                    break;
                }
                [[fallthrough]];
            }

            case 7: {
                if (!buffer.Available(msg.payload_len)) {
                    return false;
                }
                auto val = buffer.Front(msg.payload_len);
                msg.payload = std::move(val);
                buffer.Pop(msg.payload_len);
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

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, ObjectDatagram&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, ObjectDatagram&);

    Bytes& operator<<(Bytes& buffer, const StreamHeaderSubGroup& msg)
    {
        buffer << UintVar(static_cast<uint64_t>(DataMessageType::kStreamHeaderSubgroup));
        buffer << UintVar(msg.track_alias);
        buffer << UintVar(msg.group_id);
        buffer << UintVar(msg.subgroup_id);
        buffer.push_back(msg.priority);
        return buffer;
    }

    template<class StreamBufferType>
    bool operator>>(StreamBufferType& buffer, StreamHeaderSubGroup& msg)
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
                if (!ParseUintVField(buffer, msg.subgroup_id)) {
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

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, StreamHeaderSubGroup&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, StreamHeaderSubGroup&);

    Bytes& operator<<(Bytes& buffer, const StreamSubGroupObject& msg)
    {
        buffer << UintVar(msg.object_id);
        buffer << UintVar(msg.payload.size());
        if (msg.payload.empty()) {
            // empty payload needs a object status to be set
            buffer << UintVar(static_cast<uint8_t>(msg.object_status));
            PushExtensions(buffer, msg.extensions);
        } else {
            PushExtensions(buffer, msg.extensions);
            buffer << msg.payload;
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
                if (!ParseUintVField(buffer, msg.payload_len)) {
                    return false;
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }
            case 2: {
                if (msg.payload_len == 0) {
                    uint64_t status = 0;
                    if (!ParseUintVField(buffer, status)) {
                        return false;
                    }
                    msg.object_status = static_cast<ObjectStatus>(status);
                }
                msg.current_pos += 1;
                [[fallthrough]];
            }

            case 3: {
                if (!ParseExtensions(buffer, msg.num_extensions, msg.extensions, msg.current_tag)) {
                    return false;
                }
                msg.current_pos += 1;
                if (msg.payload_len == 0) {
                    msg.parse_completed = true;
                    break;
                }
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

        if (!msg.parse_completed) {
            return false;
        }

        return true;
    }

    template bool operator>> <StreamBuffer<uint8_t>>(StreamBuffer<uint8_t>&, StreamSubGroupObject&);
    template bool operator>> <SafeStreamBuffer<uint8_t>>(SafeStreamBuffer<uint8_t>&, StreamSubGroupObject&);

}
