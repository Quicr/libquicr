// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/common.h"
#include "quicr/object.h"
#include "quicr/track_name.h"
#include "stream_buffer.h"

#include <map>
#include <string>
#include <vector>

namespace quicr::messages {

    using Version = uint64_t;
    using TrackName = Bytes;
    using ErrorCode = uint64_t;
    using StatusCode = uint64_t;
    using ReasonPhrase = Bytes;
    using GroupId = uint64_t;
    using SubGroupId = uint64_t;
    using ObjectId = uint64_t;
    using ObjectPriority = uint8_t;
    using SubscribeId = uint64_t;
    using TrackAlias = uint64_t;
    using ParamType = uint64_t;
    using Extensions = std::map<uint64_t, std::vector<uint8_t>>;

    enum class TerminationReason : uint64_t
    {
        kNoError = 0x0,
        kInternalError,
        kUnauthorized,
        kProtocolViolation,
        kDupTrackAlias,
        kParamLengthMismatch,

        kGoAwayTimeout = 0x10,
    };

    // Ref: https://moq-wg.github.io/moq-transport/draft-ietf-moq-transport.html#name-messages
    enum class ControlMessageType : uint64_t
    {
        kSubscribeUpdate = 0x02,
        kSubscribe,
        kSubscribeOk,
        kSubscribeError,
        kAnnounce,
        kAnnounceOk,
        kAnnounceError,
        kUnannounce,
        kUnsubscribe,
        kSubscribeDone,
        kAnnounceCancel,
        kTrackStatusRequest,
        kTrackStatus,

        kGoAway = 0x10,
        kSubscribeAnnounces,
        kSubscribeAnnouncesOk,
        kSubscribeAnnouncesError,
        kUnsubscribeAnnounces,

        kMaxSubscribeID = 0x15,
        kFetch,
        kFetchCancel,
        kFetchOk,
        kFetchError,
        kSubscribesBlocked,

        kClientSetup = 0x40,
        kServerSetup,

        kNewGroup, // Missing in draft
    };

    enum class DataMessageType : uint8_t
    {
        kObjectDatagram = 0x01,
        kObjectDatagramStatus = 0x02,
        kStreamHeaderSubgroup = 0x04,
        kFetchHeader = 0x5,
    };

    enum class SubscribeErrorCode : uint8_t
    {
        kInternalError = 0x0,
        kUnauthorized,
        kTimeout,
        kNotSupported,
        kTrackDoesNotExist,
        kInvalidRange,
        kRetryTrackAlias,

        kTrackNotExist = 0xF0 // Missing in draft
    };

    enum class FetchErrorCode : uint8_t
    {
        kInternalError = 0x0,
        kUnauthorized = 0x1,
        kTimeout = 0x2,
        kNotSupported = 0x3,
        kTrackDoesNotExist = 0x4,
        kInvalidRange = 0x5,
    };

    // TODO (Suhas): rename it to StreamMapping
    enum ForwardingPreference : uint8_t
    {
        kStreamPerGroup = 0,
        kStreamPerObject,
        kStreamPerPriority,
        kStreamPerTrack,
        kDatagram
    };

    enum struct GroupOrder : uint8_t
    {
        kOriginalPublisherOrder = 0x0,
        kAscending,
        kDescending
    };

    //
    // Parameters
    //

    enum struct ParameterType : uint8_t
    {
        kPath = 0x1,
        kMaxSubscribeId = 0x2, // version specific, unused
        kEndpointId = 0xF0,    // Endpoint ID, using temp value for now
        kInvalid = 0xFF,       // used internally.
    };

    Bytes& operator<<(Bytes& buffer, BytesSpan bytes);

    struct Parameter
    {
        uint64_t type{ 0 };
        uint64_t length{ 0 };
        Bytes value;
    };

    BytesSpan operator>>(BytesSpan buffer, Parameter& msg);
    Bytes& operator<<(Bytes& buffer, const Parameter& msg);

    //
    // Namespace
    //

    BytesSpan operator>>(BytesSpan buffer, TrackNamespace& msg);
    Bytes& operator<<(Bytes& buffer, const TrackNamespace& msg);

    //
    // Setup
    //

    struct ClientSetup
    {
        uint64_t num_versions{ 0 };
        uint64_t max_subscribe_id{ 0xFFFFFFFF };
        std::vector<Version> supported_versions;
        Parameter endpoint_id_parameter;
    };

    BytesSpan operator>>(BytesSpan buffer, ClientSetup& msg);
    Bytes& operator<<(Bytes& buffer, const ClientSetup& msg);

    struct ServerSetup
    {
        Version selection_version;
        uint64_t max_subscribe_id{ 0xFFFFFFFF };
        Parameter endpoint_id_parameter;
    };

    BytesSpan operator>>(BytesSpan buffer, ServerSetup& msg);
    Bytes& operator<<(Bytes& buffer, const ServerSetup& msg);

    //
    // Subscribe Announces
    //
    struct SubscribeAnnounces
    {
        TrackNamespace prefix_namespace;
        std::vector<Parameter> params;
    };

    BytesSpan operator>>(BytesSpan buffer, SubscribeAnnounces& msg);
    Bytes& operator<<(Bytes& buffer, const SubscribeAnnounces& msg);

    //
    // Subscribe Announces Ok
    //
    struct SubscribeAnnouncesOk
    {
        TrackNamespace prefix_namespace;
    };

    BytesSpan operator>>(BytesSpan buffer, SubscribeAnnouncesOk& msg);
    Bytes& operator<<(Bytes& buffer, const SubscribeAnnouncesOk& msg);

    //
    // Unsubscribe Announces
    //
    struct UnsubscribeAnnounces
    {
        TrackNamespace prefix_namespace;
    };

    BytesSpan operator>>(BytesSpan buffer, UnsubscribeAnnounces& msg);
    Bytes& operator<<(Bytes& buffer, const UnsubscribeAnnounces& msg);

    //
    // Subscribe Announces Error
    //
    enum class SubscribeAnnouncesErrorCode : uint64_t
    {
        kInternalError = 0x0,
        kUnauthorized,
        kTimeout,
        kNotSupported,
        kNamespacePrefixUnknown,
    };

    struct SubscribeAnnouncesError
    {
        TrackNamespace prefix_namespace;
        SubscribeAnnouncesErrorCode error_code;
        ReasonPhrase reason_phrase;
    };

    BytesSpan operator>>(BytesSpan buffer, SubscribeAnnouncesError& msg);
    Bytes& operator<<(Bytes& buffer, const SubscribeAnnouncesError& msg);

    //
    // New Group
    //

    struct NewGroupRequest
    {
        uint64_t subscribe_id;
        uint64_t track_alias;
    };

    BytesSpan operator>>(BytesSpan buffer, NewGroupRequest& msg);
    Bytes& operator<<(Bytes& buffer, const NewGroupRequest& msg);

    //
    // Subscribe
    //
    enum struct FilterType : uint64_t
    {
        kNone = 0x0,
        kLatestGroup,
        kLatestObject,
        kAbsoluteStart,
        kAbsoluteRange
    };

    struct Subscribe
    {
        uint64_t subscribe_id;
        uint64_t track_alias;
        TrackNamespace track_namespace;
        TrackName track_name;
        ObjectPriority priority;
        GroupOrder group_order;
        FilterType filter_type{ FilterType::kNone };
        uint64_t start_group{ 0 };
        uint64_t start_object{ 0 };
        uint64_t end_group{ 0 };
        std::vector<Parameter> track_params;
    };

    BytesSpan operator>>(BytesSpan buffer, Subscribe& msg);
    Bytes& operator<<(Bytes& buffer, const Subscribe& msg);

    struct SubscribeOk
    {
        SubscribeId subscribe_id;
        uint64_t expires{ 0 };

        // TODO: Statically set ascending because we don't support anything else
        uint8_t group_order{ static_cast<uint8_t>(GroupOrder::kAscending) };

        bool content_exists;
        uint64_t largest_group{ 0 };
        uint64_t largest_object{ 0 };
        std::vector<Parameter> params;
    };

    BytesSpan operator>>(BytesSpan buffer, SubscribeOk& msg);
    Bytes& operator<<(Bytes& buffer, const SubscribeOk& msg);

    struct SubscribeUpdate
    {
        SubscribeId subscribe_id;
        GroupId start_group;
        ObjectId start_object;
        GroupId end_group;
        ObjectPriority priority;
        std::vector<Parameter> track_params;
    };

    BytesSpan operator>>(BytesSpan buffer, SubscribeUpdate& msg);
    Bytes& operator<<(Bytes& buffer, const SubscribeUpdate& msg);

    struct SubscribeError
    {
        uint64_t subscribe_id;
        ErrorCode err_code;
        ReasonPhrase reason_phrase;
        uint64_t track_alias;
    };

    BytesSpan operator>>(BytesSpan buffer, SubscribeError& msg);
    Bytes& operator<<(Bytes& buffer, const SubscribeError& msg);

    struct Unsubscribe
    {
        SubscribeId subscribe_id;
    };

    BytesSpan operator>>(BytesSpan buffer, Unsubscribe& msg);
    Bytes& operator<<(Bytes& buffer, const Unsubscribe& msg);

    struct SubscribeDone
    {
        uint64_t subscribe_id;
        uint64_t status_code;
        uint64_t stream_count;
        ReasonPhrase reason_phrase;
    };

    BytesSpan operator>>(BytesSpan buffer, SubscribeDone& msg);
    Bytes& operator<<(Bytes& buffer, const SubscribeDone& msg);

    struct SubscribesBlocked
    {
        uint64_t max_subscribe_id;
    };

    BytesSpan operator>>(BytesSpan buffer, SubscribesBlocked& msg);
    Bytes& operator<<(Bytes& buffer, const SubscribesBlocked& msg);

    //
    // Track Status
    //
    struct TrackStatusRequest
    {
        TrackNamespace track_namespace;
        TrackName track_name;
    };

    BytesSpan operator>>(BytesSpan buffer, TrackStatusRequest& msg);
    Bytes& operator<<(Bytes& buffer, const TrackStatusRequest& msg);

    enum class TrackStatusCode : uint64_t
    {
        kInProgress = 0x00,
        kDoesNotExist,
        kNotStarted,
        kFinished,
        kUnknown
    };
    struct TrackStatus
    {
        TrackNamespace track_namespace;
        TrackName track_name;
        TrackStatusCode status_code;
        uint64_t last_group_id{ 0 };
        uint64_t last_object_id{ 0 };
    };

    BytesSpan operator>>(BytesSpan buffer, TrackStatus& msg);
    Bytes& operator<<(Bytes& buffer, const TrackStatus& msg);

    //
    // Announce
    //

    struct Announce
    {
        TrackNamespace track_namespace;
        std::vector<Parameter> params;
    };

    BytesSpan operator>>(BytesSpan buffer, Announce& msg);
    Bytes& operator<<(Bytes& buffer, const Announce& msg);

    struct AnnounceOk
    {
        TrackNamespace track_namespace;
    };

    BytesSpan operator>>(BytesSpan buffer, AnnounceOk& msg);
    Bytes& operator<<(Bytes& buffer, const AnnounceOk& msg);

    struct AnnounceError
    {
        std::optional<TrackNamespace> track_namespace;
        std::optional<ErrorCode> err_code;
        std::optional<ReasonPhrase> reason_phrase;
    };

    BytesSpan operator>>(BytesSpan buffer, AnnounceError& msg);
    Bytes& operator<<(Bytes& buffer, const AnnounceError& msg);

    struct Unannounce
    {
        TrackNamespace track_namespace;
    };

    BytesSpan operator>>(BytesSpan buffer, Unannounce& msg);
    Bytes& operator<<(Bytes& buffer, const Unannounce& msg);

    struct AnnounceCancel
    {
        TrackNamespace track_namespace;
        uint64_t error_code;
        ReasonPhrase reason_phrase;
    };

    BytesSpan operator>>(BytesSpan buffer, AnnounceCancel& msg);
    Bytes& operator<<(Bytes& buffer, const AnnounceCancel& msg);

    //
    // GoAway
    //
    struct GoAway
    {
        Bytes new_session_uri;
    };

    BytesSpan operator>>(BytesSpan buffer, GoAway& msg);
    Bytes& operator<<(Bytes& buffer, const GoAway& msg);

    //
    // Fetch
    //

    enum class FetchType : uint8_t
    {
        kStandalone = 0x1,
        kJoiningFetch,
    };

    // TODO(RichLogan): Consider a separate JoiningFetch structure.

    struct Fetch
    {
        uint64_t subscribe_id;
        ObjectPriority priority;
        GroupOrder group_order;
        FetchType fetch_type;
        TrackNamespace track_namespace;
        TrackName track_name;
        GroupId start_group;
        ObjectId start_object;
        GroupId end_group;
        ObjectId end_object;
        SubscribeId joining_subscribe_id;
        uint64_t preceding_group_offset;
        std::vector<Parameter> params;

        static inline std::size_t SizeOf(const Fetch& fetch) noexcept
        {
            return sizeof(Fetch) + fetch.track_namespace.size() + fetch.track_name.size();
        }
    };

    BytesSpan operator>>(BytesSpan buffer, Fetch& msg);
    Bytes& operator<<(Bytes& buffer, const Fetch& msg);

    struct FetchOk
    {
        SubscribeId subscribe_id;
        GroupOrder group_order;
        bool end_of_track;
        uint64_t largest_group{ 0 };
        uint64_t largest_object{ 0 };
        std::vector<Parameter> params;
    };

    BytesSpan operator>>(BytesSpan buffer, FetchOk& msg);
    Bytes& operator<<(Bytes& buffer, const FetchOk& msg);

    struct FetchError
    {
        uint64_t subscribe_id;
        ErrorCode err_code;
        ReasonPhrase reason_phrase;
    };

    BytesSpan operator>>(BytesSpan buffer, FetchError& msg);
    Bytes& operator<<(Bytes& buffer, const FetchError& msg);

    struct FetchCancel
    {
        uint64_t subscribe_id;
    };

    BytesSpan operator>>(BytesSpan buffer, FetchCancel& msg);
    Bytes& operator<<(Bytes& buffer, const FetchCancel& msg);

    struct FetchHeader
    {
        uint64_t subscribe_id;

        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, FetchHeader& msg);

      private:
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    BytesSpan operator>>(BytesSpan buffer, FetchHeader& msg);
    Bytes& operator<<(Bytes& buffer, const FetchHeader& msg);

    struct FetchObject
    {
        GroupId group_id;
        SubGroupId subgroup_id;
        ObjectId object_id;
        ObjectPriority publisher_priority;
        std::optional<Extensions> extensions;
        uint64_t payload_len{ 0 };
        ObjectStatus object_status;
        Bytes payload;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, FetchObject& msg);

      private:
        uint64_t num_extensions{ 0 };
        std::optional<uint64_t> current_tag{};
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    bool operator>>(Bytes& buffer, FetchObject& msg);
    Bytes& operator<<(Bytes& buffer, const FetchObject& msg);

    //
    // Data Streams
    //

    struct ObjectDatagram
    {
        TrackAlias track_alias;
        GroupId group_id;
        ObjectId object_id;
        ObjectPriority priority;
        std::optional<Extensions> extensions;
        uint64_t payload_len{ 0 };
        ObjectStatus object_status;
        Bytes payload;

        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, ObjectDatagram& msg);

      private:
        uint64_t num_extensions{ 0 };
        std::optional<uint64_t> current_tag{};
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    Bytes& operator<<(Bytes& buffer, const ObjectDatagram& msg);

    struct ObjectDatagramStatus
    {
        TrackAlias track_alias;
        GroupId group_id;
        ObjectId object_id;
        ObjectPriority priority;
        ObjectStatus status;

        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, ObjectDatagramStatus& msg);

      private:
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    Bytes& operator<<(Bytes& buffer, const ObjectDatagramStatus& msg);

    // SubGroups
    struct StreamHeaderSubGroup
    {
        TrackAlias track_alias;
        GroupId group_id;
        SubGroupId subgroup_id;
        ObjectPriority priority;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, StreamHeaderSubGroup& msg);

      private:
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    bool operator>>(Bytes& buffer, StreamHeaderSubGroup& msg);
    Bytes& operator<<(Bytes& buffer, const StreamHeaderSubGroup& msg);

    struct StreamSubGroupObject
    {
        ObjectId object_id;
        uint64_t payload_len{ 0 };
        ObjectStatus object_status;
        std::optional<Extensions> extensions;
        Bytes payload;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, StreamSubGroupObject& msg);

      private:
        uint64_t num_extensions{ 0 };
        std::optional<uint64_t> current_tag{};
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    bool operator>>(Bytes& buffer, StreamSubGroupObject& msg);
    Bytes& operator<<(Bytes& buffer, const StreamSubGroupObject& msg);

} // end of namespace quicr::messages
