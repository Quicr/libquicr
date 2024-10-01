// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/common.h"
#include "quicr/detail/serializer.h"
#include "stream_buffer.h"

#include <map>
#include <string>
#include <vector>

namespace quicr::messages {

    using Version = uint64_t;
    using TrackNamespace = Bytes;
    using TrackName = Bytes;
    using ErrorCode = uint64_t;
    using StatusCode = uint64_t;
    using ReasonPhrase = Bytes;
    using GroupId = uint64_t;
    using ObjectId = uint64_t;
    using ObjectPriority = uint64_t;
    using SubscribeId = uint64_t;
    using TrackAlias = uint64_t;
    using ParamType = uint64_t;
    using Extensions = std::map<uint64_t, std::vector<uint8_t>>;

    enum class MoqTerminationReason : uint64_t
    {
        NO_ERROR = 0x0,
        INTERNAL_ERROR,
        UNAUTHORIZED,
        PROTOCOL_VIOLATION,
        DUP_TRACK_ALIAS,
        PARAM_LEN_MISMATCH,

        GOAWAY_TIMEOUT = 0x10,
    };

    // Ref: https://moq-wg.github.io/moq-transport/draft-ietf-moq-transport.html#name-messages
    enum class MoqMessageType : uint64_t
    {
        OBJECT_STREAM = 0x0,
        OBJECT_DATAGRAM,

        SUBSCRIBE = 0x03,
        SUBSCRIBE_OK,
        SUBSCRIBE_ERROR,
        ANNOUNCE,
        ANNOUNCE_OK,
        ANNOUNCE_ERROR,
        UNANNOUNCE,
        UNSUBSCRIBE,
        SUBSCRIBE_DONE,
        ANNOUNCE_CANCEL,
        TRACK_STATUS_REQUEST,
        TRACK_STATUS,

        GOAWAY = 0x10,

        CLIENT_SETUP = 0x40,
        SERVER_SETUP,

        STREAM_HEADER_TRACK = 0x50,
        STREAM_HEADER_GROUP,
    };

    enum class SubscribeError : uint8_t
    {
        INTERNAL_ERROR = 0x0,
        INVALID_RANGE,
        RETRY_TRACK_ALIAS,

        TRACK_NOT_EXIST = 0xF0 // Missing in draft
    };

    // TODO (Suhas): rename it to StreamMapping
    enum ForwardingPreference : uint8_t
    {
        StreamPerGroup = 0,
        StreamPerObject,
        StreamPerPriority,
        StreamPerTrack,
        Datagram
    };

    //
    // Parameters
    //

    enum struct ParameterType : uint8_t
    {
        Role = 0x0,
        Path = 0x1,
        AuthorizationInfo = 0x2, // version specific, unused
        EndpointId = 0xF0,       // Endpoint ID, using temp value for now
        Invalid = 0xFF,          // used internally.
    };

    struct MoqParameter
    {
        uint64_t type{ 0 };
        uint64_t length{ 0 };
        Bytes value;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqParameter& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqParameter& msg);
    };


    // This is added to support v6, but this may not sustain for v7 onwards
    // tuple is a bad design
    struct Tuple {

    };

    //
    // Setup
    //

    struct MoqClientSetup
    {
        uint64_t num_versions{ 0 };
        std::vector<Version> supported_versions;
        MoqParameter role_parameter;
        MoqParameter path_parameter;
        MoqParameter endpoint_id_parameter;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqClientSetup& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqClientSetup& msg);

      private:
        size_t current_pos{ 0 };
        std::optional<uint64_t> num_params;
        std::optional<MoqParameter> current_param{};
        bool parse_completed{ false };
    };

    struct MoqServerSetup
    {
        Version selection_version;
        MoqParameter role_parameter;
        MoqParameter path_parameter;
        MoqParameter endpoint_id_parameter;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqServerSetup& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqServerSetup& msg);

      private:
        size_t current_pos{ 0 };
        std::optional<uint64_t> num_params;
        bool parse_completed{ false };
        std::optional<MoqParameter> current_param{};
    };

    //
    // Subscribe
    //
    enum struct FilterType : uint64_t
    {
        None = 0x0,
        LatestGroup,
        LatestObject,
        AbsoluteStart,
        AbsoluteRange
    };

    enum struct GroupOrder: uint8_t {
        PublisherOrder = 0x0,
        Ascending = 0x1,
        Descending = 0x2
    };

    struct MoqSubscribe
    {
        uint64_t subscribe_id;
        uint64_t track_alias;
        TrackNamespace track_namespace;
        TrackName track_name;
        uint8_t subscriber_priority;
        uint8_t group_order;
        FilterType filter_type{ FilterType::None };
        uint64_t start_group{ 0 };
        uint64_t end_group{ 0 };
        uint64_t start_object{ 0 };
        uint64_t end_object{ 0 };
        std::optional<uint64_t> num_params;
        std::vector<MoqParameter> track_params;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqSubscribe& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqSubscribe& msg);
      private:
        std::optional<MoqParameter> current_param{};
        size_t current_pos{ 0 };
        bool parsing_completed{ false };
    };

    struct MoqSubscribeOk
    {
        SubscribeId subscribe_id;
        uint64_t expires;
        bool content_exists;
        uint64_t largest_group{ 0 };
        uint64_t largest_object{ 0 };
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqSubscribeOk& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqSubscribeOk& msg);

      private:
        size_t current_pos{ 0 };
        size_t MAX_FIELDS{ 5 };
    };

    struct MoqSubscribeError
    {
        uint64_t subscribe_id;
        ErrorCode err_code;
        ReasonPhrase reason_phrase;
        uint64_t track_alias;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqSubscribeError& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqSubscribeError& msg);

      private:
        size_t current_pos{ 0 };
        size_t MAX_FIELDS{ 4 };
    };

    struct MoqUnsubscribe
    {
        SubscribeId subscribe_id;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqUnsubscribe& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqUnsubscribe& msg);
    };

    struct MoqSubscribeDone
    {
        uint64_t subscribe_id;
        uint64_t status_code;
        ReasonPhrase reason_phrase;
        bool content_exists;
        uint64_t final_group_id;
        uint64_t final_object_id;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqSubscribeDone& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqSubscribeDone& msg);

      private:
        size_t current_pos{ 0 };
        size_t MAX_FIELDS = { 6 };
    };

    //
    // Track Status
    //
    struct MoqTrackStatusRequest
    {
        TrackNamespace track_namespace;
        TrackName track_name;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqTrackStatusRequest& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqTrackStatusRequest& msg);

      private:
        size_t current_pos{ 0 };
        bool parsing_completed{ false };
    };

    enum class TrackStatus : uint64_t
    {
        IN_PROGRESS = 0x00,
        DOES_NOT_EXIST,
        NOT_STARTED,
        FINISHED,
        UNKNOWN
    };
    struct MoqTrackStatus
    {
        TrackNamespace track_namespace;
        TrackName track_name;
        TrackStatus status_code;
        uint64_t last_group_id{ 0 };
        uint64_t last_object_id{ 0 };
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqTrackStatus& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqTrackStatus& msg);

      private:
        size_t current_pos{ 0 };
        bool parsing_completed{ false };
    };

    //
    // Announce
    //

    struct MoqAnnounce
    {
        TrackNamespace track_namespace;
        std::vector<MoqParameter> params;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqAnnounce& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqAnnounce& msg);

      private:
        uint64_t num_params{ 0 };
        MoqParameter current_param{};
    };

    struct MoqAnnounceOk
    {
        TrackNamespace track_namespace;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqAnnounceOk& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqAnnounceOk& msg);
    };

    struct MoqAnnounceError
    {
        std::optional<TrackNamespace> track_namespace;
        std::optional<ErrorCode> err_code;
        std::optional<ReasonPhrase> reason_phrase;

        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqAnnounceError& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqAnnounceError& msg);
    };

    struct MoqUnannounce
    {
        TrackNamespace track_namespace;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqUnannounce& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqUnannounce& msg);
    };

    struct MoqAnnounceCancel
    {
        TrackNamespace track_namespace;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqAnnounceCancel& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqAnnounceCancel& msg);
    };

    //
    // GoAway
    //
    struct MoqGoaway
    {
        Bytes new_session_uri;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqGoaway& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqGoaway& msg);
    };

    //
    // Object
    //
    struct MoqObjectStream
    {
        SubscribeId subscribe_id;
        TrackAlias track_alias;
        GroupId group_id;
        ObjectId object_id;
        ObjectPriority priority;
        std::optional<Extensions> extensions;
        Bytes payload;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqObjectStream& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqObjectStream& msg);

      private:
        uint64_t num_extensions{ 0 };
        std::optional<uint64_t> current_tag{};
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    struct MoqObjectDatagram
    {
        SubscribeId subscribe_id;
        TrackAlias track_alias;
        GroupId group_id;
        ObjectId object_id;
        ObjectPriority priority;
        std::optional<Extensions> extensions;
        Bytes payload;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqObjectDatagram& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqObjectDatagram& msg);

      private:
        uint64_t num_extensions{ 0 };
        std::optional<uint64_t> current_tag{};
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    struct MoqStreamHeaderTrack
    {
        SubscribeId subscribe_id;
        TrackAlias track_alias;
        ObjectPriority priority;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqStreamHeaderTrack& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqStreamHeaderTrack& msg);

      private:
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    struct MoqStreamTrackObject
    {
        GroupId group_id;
        ObjectId object_id;
        std::optional<Extensions> extensions;
        Bytes payload;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqStreamTrackObject& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqStreamTrackObject& msg);

      private:
        uint64_t num_extensions{ 0 };
        std::optional<uint64_t> current_tag{};
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    struct MoqStreamHeaderGroup
    {
        SubscribeId subscribe_id;
        TrackAlias track_alias;
        GroupId group_id;
        ObjectPriority priority;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqStreamHeaderGroup& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqStreamHeaderGroup& msg);

      private:
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    struct MoqStreamGroupObject
    {
        ObjectId object_id;
        std::optional<Extensions> extensions;
        Bytes payload;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, MoqStreamGroupObject& msg);
        friend Serializer& operator<<(Serializer& buffer, const MoqStreamGroupObject& msg);

      private:
        uint64_t num_extensions{ 0 };
        std::optional<uint64_t> current_tag{};
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

} // end of namespace quicr::messages
