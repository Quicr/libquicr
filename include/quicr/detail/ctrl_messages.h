#pragma once
#include <vector>

#include <quicr/track_name.h>

#include <quicr/common.h>
#include "ctrl_message_types.h"
       
namespace quicr::ctrl_messages {  

    struct Parameter
    {
        ParameterTypeEnum type{0};
        uint64_t length{0};
        Bytes value;
    };
    
    // usings
    using SupportedVersions = std::vector<std::uint64_t>;
    using SetupParameters = std::vector<quicr::ctrl_messages::Parameter>;
    using SelectedVersion = std::uint64_t;
    using NewSessionURI = quicr::Bytes;
    using SubscribeID = std::uint64_t;
    using TrackAlias = std::uint64_t;
    using TrackNamespace = quicr::TrackNamespace;
    using TrackName = quicr::Bytes;
    using SubscriberPriority = std::uint8_t;
    using GroupOrder = quicr::ctrl_messages::GroupOrderEnum;
    using FilterType = quicr::ctrl_messages::FilterTypeEnum;
    using StartGroup = std::uint64_t;
    using StartObject = std::uint64_t;
    using EndGroup = std::uint64_t;
    using SubscribeParameters = std::vector<quicr::ctrl_messages::Parameter>;
    using FetchType = quicr::ctrl_messages::FetchTypeEnum;
    using EndObject = std::uint64_t;
    using JoiningSubscribeID = std::uint64_t;
    using PrecedingGroupOffset = std::uint64_t;
    using Parameters = std::vector<quicr::ctrl_messages::Parameter>;
    using ErrorCode = std::uint64_t;
    using ReasonPhrase = quicr::Bytes;
    using TrackNamespacePrefix = quicr::TrackNamespace;
    using Expires = std::uint64_t;
    using ContentExists = std::uint8_t;
    using LargestGroupID = std::uint64_t;
    using LargestObjectID = std::uint64_t;
    using EndOfTrack = std::uint8_t;
    using StatusCode = std::uint64_t;
    using StreamCount = std::uint64_t;
    using MaximumSubscribeID = std::uint64_t;
    using LastGroupID = std::uint64_t;
    using LastObjectID = std::uint64_t;

    // enums
    enum class ControlMessageType : uint64_t
    {
        kSubscribeUpdate = 0x2,
        kSubscribe = 0x3,
        kSubscribeOk = 0x4,
        kSubscribeError = 0x5,
        kAnnounce = 0x6,
        kAnnounceOk = 0x7,
        kAnnounceError = 0x8,
        kUnannounce = 0x9,
        kUnsubscribe = 0xa,
        kSubscribeDone = 0xb,
        kAnnounceCancel = 0xc,
        kTrackStatusRequest = 0xd,
        kTrackStatus = 0xe,
        kGoaway = 0x10,
        kSubscribeAnnounces = 0x11,
        kSubscribeAnnouncesOk = 0x12,
        kSubscribeAnnouncesError = 0x13,
        kUnsubscribeAnnounces = 0x14,
        kMaxSubscribeId = 0x15,
        kFetch = 0x16,
        kFetchCancel = 0x17,
        kFetchOk = 0x18,
        kFetchError = 0x19,
        kSubscribesBlocked = 0x1a,
        kClientSetup = 0x40,
        kServerSetup = 0x41,
    };

    /**
     * @brief SubscribeUpdate
     */
    struct SubscribeUpdate
    {
        SubscribeID subscribe_id;
        StartGroup start_group;
        StartObject start_object;
        EndGroup end_group;
        SubscriberPriority subscriber_priority;
        SubscribeParameters subscribe_parameters;
    };

    Bytes& operator<<(Bytes& buffer, const SubscribeUpdate& msg);
    BytesSpan operator>>(BytesSpan buffer, SubscribeUpdate& msg);    


    /**
     * @brief Subscribe
     */
    struct Subscribe
    {
        struct Group_0 {
            StartGroup start_group;
            StartObject start_object;
        };
        struct Group_1 {
            EndGroup end_group;
        };
        SubscribeID subscribe_id;
        TrackAlias track_alias;
        TrackNamespace track_namespace;
        TrackName track_name;
        SubscriberPriority subscriber_priority;
        GroupOrder group_order;
        FilterType filter_type;
        std::function<void (Subscribe&)> optional_group_0_cb;
        std::optional<Subscribe::Group_0> group_0;
        std::function<void (Subscribe&)> optional_group_1_cb;
        std::optional<Subscribe::Group_1> group_1;
        SubscribeParameters subscribe_parameters;
    };

    Bytes& operator<<(Bytes& buffer, const Subscribe& msg);
    BytesSpan operator>>(BytesSpan buffer, Subscribe& msg);    

    Bytes& operator<<(Bytes& buffer, const std::optional<Subscribe::Group_0>& grp);
    BytesSpan operator>>(BytesSpan buffer, std::optional<Subscribe::Group_0>& grp);

    Bytes& operator<<(Bytes& buffer, const std::optional<Subscribe::Group_1>& grp);
    BytesSpan operator>>(BytesSpan buffer, std::optional<Subscribe::Group_1>& grp);


    /**
     * @brief SubscribeOk
     */
    struct SubscribeOk
    {
        struct Group_0 {
            LargestGroupID largest_group_id;
            LargestObjectID largest_object_id;
        };
        SubscribeID subscribe_id;
        Expires expires;
        GroupOrder group_order;
        ContentExists content_exists;
        std::function<void (SubscribeOk&)> optional_group_0_cb;
        std::optional<SubscribeOk::Group_0> group_0;
        SubscribeParameters subscribe_parameters;
    };

    Bytes& operator<<(Bytes& buffer, const SubscribeOk& msg);
    BytesSpan operator>>(BytesSpan buffer, SubscribeOk& msg);    

    Bytes& operator<<(Bytes& buffer, const std::optional<SubscribeOk::Group_0>& grp);
    BytesSpan operator>>(BytesSpan buffer, std::optional<SubscribeOk::Group_0>& grp);


    /**
     * @brief SubscribeError
     */
    struct SubscribeError
    {
        SubscribeID subscribe_id;
        ErrorCode error_code;
        ReasonPhrase reason_phrase;
        TrackAlias track_alias;
    };

    Bytes& operator<<(Bytes& buffer, const SubscribeError& msg);
    BytesSpan operator>>(BytesSpan buffer, SubscribeError& msg);    


    /**
     * @brief Announce
     */
    struct Announce
    {
        TrackNamespace track_namespace;
        Parameters parameters;
    };

    Bytes& operator<<(Bytes& buffer, const Announce& msg);
    BytesSpan operator>>(BytesSpan buffer, Announce& msg);    


    /**
     * @brief AnnounceOk
     */
    struct AnnounceOk
    {
        TrackNamespace track_namespace;
    };

    Bytes& operator<<(Bytes& buffer, const AnnounceOk& msg);
    BytesSpan operator>>(BytesSpan buffer, AnnounceOk& msg);    


    /**
     * @brief AnnounceError
     */
    struct AnnounceError
    {
        TrackNamespace track_namespace;
        ErrorCode error_code;
        ReasonPhrase reason_phrase;
    };

    Bytes& operator<<(Bytes& buffer, const AnnounceError& msg);
    BytesSpan operator>>(BytesSpan buffer, AnnounceError& msg);    


    /**
     * @brief Unannounce
     */
    struct Unannounce
    {
        TrackNamespace track_namespace;
    };

    Bytes& operator<<(Bytes& buffer, const Unannounce& msg);
    BytesSpan operator>>(BytesSpan buffer, Unannounce& msg);    


    /**
     * @brief Unsubscribe
     */
    struct Unsubscribe
    {
        SubscribeID subscribe_id;
    };

    Bytes& operator<<(Bytes& buffer, const Unsubscribe& msg);
    BytesSpan operator>>(BytesSpan buffer, Unsubscribe& msg);    


    /**
     * @brief SubscribeDone
     */
    struct SubscribeDone
    {
        SubscribeID subscribe_id;
        StatusCode status_code;
        StreamCount stream_count;
        ReasonPhrase reason_phrase;
    };

    Bytes& operator<<(Bytes& buffer, const SubscribeDone& msg);
    BytesSpan operator>>(BytesSpan buffer, SubscribeDone& msg);    


    /**
     * @brief AnnounceCancel
     */
    struct AnnounceCancel
    {
        TrackNamespace track_namespace;
        ErrorCode error_code;
        ReasonPhrase reason_phrase;
    };

    Bytes& operator<<(Bytes& buffer, const AnnounceCancel& msg);
    BytesSpan operator>>(BytesSpan buffer, AnnounceCancel& msg);    


    /**
     * @brief TrackStatusRequest
     */
    struct TrackStatusRequest
    {
        TrackNamespace track_namespace;
        TrackName track_name;
    };

    Bytes& operator<<(Bytes& buffer, const TrackStatusRequest& msg);
    BytesSpan operator>>(BytesSpan buffer, TrackStatusRequest& msg);    


    /**
     * @brief TrackStatus
     */
    struct TrackStatus
    {
        TrackNamespace track_namespace;
        TrackName track_name;
        StatusCode status_code;
        LastGroupID last_group_id;
        LastObjectID last_object_id;
    };

    Bytes& operator<<(Bytes& buffer, const TrackStatus& msg);
    BytesSpan operator>>(BytesSpan buffer, TrackStatus& msg);    


    /**
     * @brief Goaway
     */
    struct Goaway
    {
        NewSessionURI new_session_uri;
    };

    Bytes& operator<<(Bytes& buffer, const Goaway& msg);
    BytesSpan operator>>(BytesSpan buffer, Goaway& msg);    


    /**
     * @brief SubscribeAnnounces
     */
    struct SubscribeAnnounces
    {
        TrackNamespacePrefix track_namespace_prefix;
        Parameters parameters;
    };

    Bytes& operator<<(Bytes& buffer, const SubscribeAnnounces& msg);
    BytesSpan operator>>(BytesSpan buffer, SubscribeAnnounces& msg);    


    /**
     * @brief SubscribeAnnouncesOk
     */
    struct SubscribeAnnouncesOk
    {
        TrackNamespacePrefix track_namespace_prefix;
    };

    Bytes& operator<<(Bytes& buffer, const SubscribeAnnouncesOk& msg);
    BytesSpan operator>>(BytesSpan buffer, SubscribeAnnouncesOk& msg);    


    /**
     * @brief SubscribeAnnouncesError
     */
    struct SubscribeAnnouncesError
    {
        TrackNamespacePrefix track_namespace_prefix;
        ErrorCode error_code;
        ReasonPhrase reason_phrase;
    };

    Bytes& operator<<(Bytes& buffer, const SubscribeAnnouncesError& msg);
    BytesSpan operator>>(BytesSpan buffer, SubscribeAnnouncesError& msg);    


    /**
     * @brief UnsubscribeAnnounces
     */
    struct UnsubscribeAnnounces
    {
        TrackNamespacePrefix track_namespace_prefix;
    };

    Bytes& operator<<(Bytes& buffer, const UnsubscribeAnnounces& msg);
    BytesSpan operator>>(BytesSpan buffer, UnsubscribeAnnounces& msg);    


    /**
     * @brief MaxSubscribeId
     */
    struct MaxSubscribeId
    {
        SubscribeID subscribe_id;
    };

    Bytes& operator<<(Bytes& buffer, const MaxSubscribeId& msg);
    BytesSpan operator>>(BytesSpan buffer, MaxSubscribeId& msg);    


    /**
     * @brief Fetch
     */
    struct Fetch
    {
        struct Group_0 {
            TrackNamespace track_namespace;
            TrackName track_name;
            StartGroup start_group;
            StartObject start_object;
            EndGroup end_group;
            EndObject end_object;
        };
        struct Group_1 {
            JoiningSubscribeID joining_subscribe_id;
            PrecedingGroupOffset preceding_group_offset;
        };
        SubscribeID subscribe_id;
        SubscriberPriority subscriber_priority;
        GroupOrder group_order;
        FetchType fetch_type;
        std::function<void (Fetch&)> optional_group_0_cb;
        std::optional<Fetch::Group_0> group_0;
        std::function<void (Fetch&)> optional_group_1_cb;
        std::optional<Fetch::Group_1> group_1;
        Parameters parameters;
    };

    Bytes& operator<<(Bytes& buffer, const Fetch& msg);
    BytesSpan operator>>(BytesSpan buffer, Fetch& msg);    

    Bytes& operator<<(Bytes& buffer, const std::optional<Fetch::Group_0>& grp);
    BytesSpan operator>>(BytesSpan buffer, std::optional<Fetch::Group_0>& grp);

    Bytes& operator<<(Bytes& buffer, const std::optional<Fetch::Group_1>& grp);
    BytesSpan operator>>(BytesSpan buffer, std::optional<Fetch::Group_1>& grp);


    /**
     * @brief FetchCancel
     */
    struct FetchCancel
    {
        SubscribeID subscribe_id;
    };

    Bytes& operator<<(Bytes& buffer, const FetchCancel& msg);
    BytesSpan operator>>(BytesSpan buffer, FetchCancel& msg);    


    /**
     * @brief FetchOk
     */
    struct FetchOk
    {
        SubscribeID subscribe_id;
        GroupOrder group_order;
        EndOfTrack end_of_track;
        LargestGroupID largest_group_id;
        LargestObjectID largest_object_id;
        SubscribeParameters subscribe_parameters;
    };

    Bytes& operator<<(Bytes& buffer, const FetchOk& msg);
    BytesSpan operator>>(BytesSpan buffer, FetchOk& msg);    


    /**
     * @brief FetchError
     */
    struct FetchError
    {
        SubscribeID subscribe_id;
        ErrorCode error_code;
        ReasonPhrase reason_phrase;
    };

    Bytes& operator<<(Bytes& buffer, const FetchError& msg);
    BytesSpan operator>>(BytesSpan buffer, FetchError& msg);    


    /**
     * @brief SubscribesBlocked
     */
    struct SubscribesBlocked
    {
        MaximumSubscribeID maximum_subscribe_id;
    };

    Bytes& operator<<(Bytes& buffer, const SubscribesBlocked& msg);
    BytesSpan operator>>(BytesSpan buffer, SubscribesBlocked& msg);    


    /**
     * @brief ClientSetup
     */
    struct ClientSetup
    {
        SupportedVersions supported_versions;
        SetupParameters setup_parameters;
    };

    Bytes& operator<<(Bytes& buffer, const ClientSetup& msg);
    BytesSpan operator>>(BytesSpan buffer, ClientSetup& msg);    


    /**
     * @brief ServerSetup
     */
    struct ServerSetup
    {
        SelectedVersion selected_version;
        SetupParameters setup_parameters;
    };

    Bytes& operator<<(Bytes& buffer, const ServerSetup& msg);
    BytesSpan operator>>(BytesSpan buffer, ServerSetup& msg);    

    
    
    Bytes& operator<<(Bytes& buffer, const std::vector<std::uint64_t>& vec);
    BytesSpan operator>>(BytesSpan buffer, std::vector<std::uint64_t>& vec);

    Bytes& operator<<(Bytes& buffer, const std::vector<quicr::ctrl_messages::Parameter>& vec);
    BytesSpan operator>>(BytesSpan buffer, std::vector<quicr::ctrl_messages::Parameter>& vec);


    Bytes& operator<<(Bytes& buffer, ControlMessageType message_type);
    BytesSpan operator>>(BytesSpan buffer, TrackNamespace& msg);
    Bytes& operator<<(Bytes& buffer, const TrackNamespace& msg);

    BytesSpan operator>>(BytesSpan buffer, Parameter& msg);
    Bytes& operator<<(Bytes& buffer, const Parameter& msg);    

} // namespace
