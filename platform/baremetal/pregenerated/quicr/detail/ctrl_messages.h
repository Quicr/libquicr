// Generated from: draft-ietf-moq-transport-14_edited.txt

#pragma once
#include <vector>
#include <quicr/common.h>
#include <quicr/track_name.h>
#include <quicr/detail/ctrl_message_types.h>

namespace quicr::messages {  

        
    
    // usings
    using SupportedVersions = std::vector<std::uint64_t>;
    using SetupParameters = std::vector<quicr::messages::SetupParameter>;
    using SelectedVersion = std::uint64_t;
    using NewSessionURI = quicr::Bytes;
    using RequestID = std::uint64_t;
    using MaximumRequestID = std::uint64_t;
    using TrackNamespace = quicr::TrackNamespace;
    using TrackName = quicr::messages::TrackName;
    using SubscriberPriority = std::uint8_t;
    using GroupOrder = quicr::messages::GroupOrder;
    using Forward = std::uint8_t;
    using FilterType = quicr::messages::FilterType;
    using StartLocation = quicr::messages::Location;
    using EndGroup = std::uint64_t;
    using Parameters = std::vector<quicr::messages::Parameter>;
    using TrackAlias = std::uint64_t;
    using Expires = std::uint64_t;
    using ContentExists = std::uint8_t;
    using LargestLocation = quicr::messages::Location;
    using SubscribeErrorErrorCode = quicr::messages::SubscribeErrorCode;
    using ErrorReason = quicr::Bytes;
    using SubscriptionRequestID = std::uint64_t;
    using PublishDoneStatusCode = quicr::messages::PublishDoneStatusCode;
    using StreamCount = std::uint64_t;
    using ErrorCode = std::uint64_t;
    using FetchType = quicr::messages::FetchType;
    using Standalone = quicr::messages::StandaloneFetch;
    using Joining = quicr::messages::JoiningFetch;
    using EndOfTrack = std::uint8_t;
    using EndLocation = quicr::messages::Location;
    using FetchErrorErrorCode = quicr::messages::FetchErrorCode;
    using TrackStatusErrorErrorCode = quicr::messages::SubscribeErrorCode;
    using PublishNamespaceErrorErrorCode = quicr::messages::PublishNamespaceErrorCode;
    using TrackNamespacePrefix = quicr::TrackNamespace;
    using SubscribeNamespaceErrorErrorCode = quicr::messages::SubscribeNamespaceErrorCode;


    // enums
    enum class ControlMessageType : uint64_t
    {
        kSubscribeUpdate = 0x2,
        kSubscribe = 0x3,
        kSubscribeOk = 0x4,
        kSubscribeError = 0x5,
        kPublishNamespace = 0x6,
        kPublishNamespaceOk = 0x7,
        kPublishNamespaceError = 0x8,
        kPublishNamespaceDone = 0x9,
        kUnsubscribe = 0xa,
        kPublishDone = 0xb,
        kPublishNamespaceCancel = 0xc,
        kTrackStatus = 0xd,
        kTrackStatusOk = 0xe,
        kTrackStatusError = 0xf,
        kGoaway = 0x10,
        kSubscribeNamespace = 0x11,
        kSubscribeNamespaceOk = 0x12,
        kSubscribeNamespaceError = 0x13,
        kUnsubscribeNamespace = 0x14,
        kMaxRequestId = 0x15,
        kFetch = 0x16,
        kFetchCancel = 0x17,
        kFetchOk = 0x18,
        kFetchError = 0x19,
        kRequestsBlocked = 0x1a,
        kPublish = 0x1d,
        kPublishOk = 0x1e,
        kPublishError = 0x1f,
        kClientSetup = 0x20,
        kServerSetup = 0x21,
    };
    /**
     * @brief SubscribeUpdate
     */
    struct SubscribeUpdate
    {        

    public:
        // Default constructor
        SubscribeUpdate () {}
        
        // All fields constructor
        SubscribeUpdate (
            RequestID request_id,
            SubscriptionRequestID subscription_request_id,
            StartLocation start_location,
            EndGroup end_group,
            SubscriberPriority subscriber_priority,
            Forward forward,
            Parameters parameters):
                request_id(request_id),
                subscription_request_id(subscription_request_id),
                start_location(start_location),
                end_group(end_group),
                subscriber_priority(subscriber_priority),
                forward(forward),
                parameters(parameters)
            {}

            

    public:
        RequestID request_id;
        SubscriptionRequestID subscription_request_id;
        StartLocation start_location;
        EndGroup end_group;
        SubscriberPriority subscriber_priority;
        Forward forward;
        Parameters parameters;
    };

    Bytes& operator<<(Bytes& buffer, const SubscribeUpdate& msg);
    BytesSpan operator>>(BytesSpan buffer, SubscribeUpdate& msg);    

    /**
     * @brief Subscribe
     */
    struct Subscribe
    {        
    public:
        // Optional Groups
        struct Group_0 {
            StartLocation start_location;
        };
        struct Group_1 {
            EndGroup end_group;
        };

    public:
        // Have optionals - delete default constructor
        Subscribe () = delete;
        
        // All fields constructor
        Subscribe (
            RequestID request_id,
            TrackNamespace track_namespace,
            TrackName track_name,
            SubscriberPriority subscriber_priority,
            GroupOrder group_order,
            Forward forward,
            FilterType filter_type,
            std::optional<Subscribe::Group_0> group_0,
            std::optional<Subscribe::Group_1> group_1,
            Parameters parameters):
                request_id(request_id),
                track_namespace(track_namespace),
                track_name(track_name),
                subscriber_priority(subscriber_priority),
                group_order(group_order),
                forward(forward),
                filter_type(filter_type),
                group_0(group_0),
                group_1(group_1),
                parameters(parameters)
            {}

            
        // Optional callback constructor 

        Subscribe (
            std::function<void (Subscribe&)> group_0_cb,
            std::function<void (Subscribe&)> group_1_cb
        );

    public:
        RequestID request_id;
        TrackNamespace track_namespace;
        TrackName track_name;
        SubscriberPriority subscriber_priority;
        GroupOrder group_order;
        Forward forward;
        FilterType filter_type;
        std::function<void (Subscribe&)> group_0_cb;
        std::optional<Subscribe::Group_0> group_0;
        std::function<void (Subscribe&)> group_1_cb;
        std::optional<Subscribe::Group_1> group_1;
        Parameters parameters;
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
    public:
        // Optional Groups
        struct Group_0 {
            LargestLocation largest_location;
        };

    public:
        // Have optionals - delete default constructor
        SubscribeOk () = delete;
        
        // All fields constructor
        SubscribeOk (
            RequestID request_id,
            TrackAlias track_alias,
            Expires expires,
            GroupOrder group_order,
            ContentExists content_exists,
            std::optional<SubscribeOk::Group_0> group_0,
            Parameters parameters):
                request_id(request_id),
                track_alias(track_alias),
                expires(expires),
                group_order(group_order),
                content_exists(content_exists),
                group_0(group_0),
                parameters(parameters)
            {}

            
        // Optional callback constructor 

        SubscribeOk (
            std::function<void (SubscribeOk&)> group_0_cb
        );

    public:
        RequestID request_id;
        TrackAlias track_alias;
        Expires expires;
        GroupOrder group_order;
        ContentExists content_exists;
        std::function<void (SubscribeOk&)> group_0_cb;
        std::optional<SubscribeOk::Group_0> group_0;
        Parameters parameters;
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

    public:
        // Default constructor
        SubscribeError () {}
        
        // All fields constructor
        SubscribeError (
            RequestID request_id,
            SubscribeErrorErrorCode error_code,
            ErrorReason error_reason):
                request_id(request_id),
                error_code(error_code),
                error_reason(error_reason)
            {}

            

    public:
        RequestID request_id;
        SubscribeErrorErrorCode error_code;
        ErrorReason error_reason;
    };

    Bytes& operator<<(Bytes& buffer, const SubscribeError& msg);
    BytesSpan operator>>(BytesSpan buffer, SubscribeError& msg);    

    /**
     * @brief PublishNamespace
     */
    struct PublishNamespace
    {        

    public:
        // Default constructor
        PublishNamespace () {}
        
        // All fields constructor
        PublishNamespace (
            RequestID request_id,
            TrackNamespace track_namespace,
            Parameters parameters):
                request_id(request_id),
                track_namespace(track_namespace),
                parameters(parameters)
            {}

            

    public:
        RequestID request_id;
        TrackNamespace track_namespace;
        Parameters parameters;
    };

    Bytes& operator<<(Bytes& buffer, const PublishNamespace& msg);
    BytesSpan operator>>(BytesSpan buffer, PublishNamespace& msg);    

    /**
     * @brief PublishNamespaceOk
     */
    struct PublishNamespaceOk
    {        

    public:
        // Default constructor
        PublishNamespaceOk () {}
        
        // All fields constructor
        PublishNamespaceOk (
            RequestID request_id):
                request_id(request_id)
            {}

            

    public:
        RequestID request_id;
    };

    Bytes& operator<<(Bytes& buffer, const PublishNamespaceOk& msg);
    BytesSpan operator>>(BytesSpan buffer, PublishNamespaceOk& msg);    

    /**
     * @brief PublishNamespaceError
     */
    struct PublishNamespaceError
    {        

    public:
        // Default constructor
        PublishNamespaceError () {}
        
        // All fields constructor
        PublishNamespaceError (
            RequestID request_id,
            PublishNamespaceErrorErrorCode error_code,
            ErrorReason error_reason):
                request_id(request_id),
                error_code(error_code),
                error_reason(error_reason)
            {}

            

    public:
        RequestID request_id;
        PublishNamespaceErrorErrorCode error_code;
        ErrorReason error_reason;
    };

    Bytes& operator<<(Bytes& buffer, const PublishNamespaceError& msg);
    BytesSpan operator>>(BytesSpan buffer, PublishNamespaceError& msg);    

    /**
     * @brief PublishNamespaceDone
     */
    struct PublishNamespaceDone
    {        

    public:
        // Default constructor
        PublishNamespaceDone () {}
        
        // All fields constructor
        PublishNamespaceDone (
            TrackNamespace track_namespace):
                track_namespace(track_namespace)
            {}

            

    public:
        TrackNamespace track_namespace;
    };

    Bytes& operator<<(Bytes& buffer, const PublishNamespaceDone& msg);
    BytesSpan operator>>(BytesSpan buffer, PublishNamespaceDone& msg);    

    /**
     * @brief Unsubscribe
     */
    struct Unsubscribe
    {        

    public:
        // Default constructor
        Unsubscribe () {}
        
        // All fields constructor
        Unsubscribe (
            RequestID request_id):
                request_id(request_id)
            {}

            

    public:
        RequestID request_id;
    };

    Bytes& operator<<(Bytes& buffer, const Unsubscribe& msg);
    BytesSpan operator>>(BytesSpan buffer, Unsubscribe& msg);    

    /**
     * @brief PublishDone
     */
    struct PublishDone
    {        

    public:
        // Default constructor
        PublishDone () {}
        
        // All fields constructor
        PublishDone (
            RequestID request_id,
            PublishDoneStatusCode status_code,
            StreamCount stream_count,
            ErrorReason error_reason):
                request_id(request_id),
                status_code(status_code),
                stream_count(stream_count),
                error_reason(error_reason)
            {}

            

    public:
        RequestID request_id;
        PublishDoneStatusCode status_code;
        StreamCount stream_count;
        ErrorReason error_reason;
    };

    Bytes& operator<<(Bytes& buffer, const PublishDone& msg);
    BytesSpan operator>>(BytesSpan buffer, PublishDone& msg);    

    /**
     * @brief PublishNamespaceCancel
     */
    struct PublishNamespaceCancel
    {        

    public:
        // Default constructor
        PublishNamespaceCancel () {}
        
        // All fields constructor
        PublishNamespaceCancel (
            TrackNamespace track_namespace,
            ErrorCode error_code,
            ErrorReason error_reason):
                track_namespace(track_namespace),
                error_code(error_code),
                error_reason(error_reason)
            {}

            

    public:
        TrackNamespace track_namespace;
        ErrorCode error_code;
        ErrorReason error_reason;
    };

    Bytes& operator<<(Bytes& buffer, const PublishNamespaceCancel& msg);
    BytesSpan operator>>(BytesSpan buffer, PublishNamespaceCancel& msg);    

    /**
     * @brief TrackStatus
     */
    struct TrackStatus
    {        
    public:
        // Optional Groups
        struct Group_0 {
            StartLocation start_location;
        };
        struct Group_1 {
            EndGroup end_group;
        };

    public:
        // Have optionals - delete default constructor
        TrackStatus () = delete;
        
        // All fields constructor
        TrackStatus (
            RequestID request_id,
            TrackNamespace track_namespace,
            TrackName track_name,
            SubscriberPriority subscriber_priority,
            GroupOrder group_order,
            Forward forward,
            FilterType filter_type,
            std::optional<TrackStatus::Group_0> group_0,
            std::optional<TrackStatus::Group_1> group_1,
            Parameters parameters):
                request_id(request_id),
                track_namespace(track_namespace),
                track_name(track_name),
                subscriber_priority(subscriber_priority),
                group_order(group_order),
                forward(forward),
                filter_type(filter_type),
                group_0(group_0),
                group_1(group_1),
                parameters(parameters)
            {}

            
        // Optional callback constructor 

        TrackStatus (
            std::function<void (TrackStatus&)> group_0_cb,
            std::function<void (TrackStatus&)> group_1_cb
        );

    public:
        RequestID request_id;
        TrackNamespace track_namespace;
        TrackName track_name;
        SubscriberPriority subscriber_priority;
        GroupOrder group_order;
        Forward forward;
        FilterType filter_type;
        std::function<void (TrackStatus&)> group_0_cb;
        std::optional<TrackStatus::Group_0> group_0;
        std::function<void (TrackStatus&)> group_1_cb;
        std::optional<TrackStatus::Group_1> group_1;
        Parameters parameters;
    };

    Bytes& operator<<(Bytes& buffer, const TrackStatus& msg);
    BytesSpan operator>>(BytesSpan buffer, TrackStatus& msg);    

    Bytes& operator<<(Bytes& buffer, const std::optional<TrackStatus::Group_0>& grp);
    BytesSpan operator>>(BytesSpan buffer, std::optional<TrackStatus::Group_0>& grp);

    Bytes& operator<<(Bytes& buffer, const std::optional<TrackStatus::Group_1>& grp);
    BytesSpan operator>>(BytesSpan buffer, std::optional<TrackStatus::Group_1>& grp);

    /**
     * @brief TrackStatusOk
     */
    struct TrackStatusOk
    {        
    public:
        // Optional Groups
        struct Group_0 {
            LargestLocation largest_location;
        };

    public:
        // Have optionals - delete default constructor
        TrackStatusOk () = delete;
        
        // All fields constructor
        TrackStatusOk (
            RequestID request_id,
            TrackAlias track_alias,
            Expires expires,
            GroupOrder group_order,
            ContentExists content_exists,
            std::optional<TrackStatusOk::Group_0> group_0,
            Parameters parameters):
                request_id(request_id),
                track_alias(track_alias),
                expires(expires),
                group_order(group_order),
                content_exists(content_exists),
                group_0(group_0),
                parameters(parameters)
            {}

            
        // Optional callback constructor 

        TrackStatusOk (
            std::function<void (TrackStatusOk&)> group_0_cb
        );

    public:
        RequestID request_id;
        TrackAlias track_alias;
        Expires expires;
        GroupOrder group_order;
        ContentExists content_exists;
        std::function<void (TrackStatusOk&)> group_0_cb;
        std::optional<TrackStatusOk::Group_0> group_0;
        Parameters parameters;
    };

    Bytes& operator<<(Bytes& buffer, const TrackStatusOk& msg);
    BytesSpan operator>>(BytesSpan buffer, TrackStatusOk& msg);    

    Bytes& operator<<(Bytes& buffer, const std::optional<TrackStatusOk::Group_0>& grp);
    BytesSpan operator>>(BytesSpan buffer, std::optional<TrackStatusOk::Group_0>& grp);

    /**
     * @brief TrackStatusError
     */
    struct TrackStatusError
    {        

    public:
        // Default constructor
        TrackStatusError () {}
        
        // All fields constructor
        TrackStatusError (
            RequestID request_id,
            TrackStatusErrorErrorCode error_code,
            ErrorReason error_reason):
                request_id(request_id),
                error_code(error_code),
                error_reason(error_reason)
            {}

            

    public:
        RequestID request_id;
        TrackStatusErrorErrorCode error_code;
        ErrorReason error_reason;
    };

    Bytes& operator<<(Bytes& buffer, const TrackStatusError& msg);
    BytesSpan operator>>(BytesSpan buffer, TrackStatusError& msg);    

    /**
     * @brief Goaway
     */
    struct Goaway
    {        

    public:
        // Default constructor
        Goaway () {}
        
        // All fields constructor
        Goaway (
            NewSessionURI new_session_uri):
                new_session_uri(new_session_uri)
            {}

            

    public:
        NewSessionURI new_session_uri;
    };

    Bytes& operator<<(Bytes& buffer, const Goaway& msg);
    BytesSpan operator>>(BytesSpan buffer, Goaway& msg);    

    /**
     * @brief SubscribeNamespace
     */
    struct SubscribeNamespace
    {        

    public:
        // Default constructor
        SubscribeNamespace () {}
        
        // All fields constructor
        SubscribeNamespace (
            RequestID request_id,
            TrackNamespacePrefix track_namespace_prefix,
            Parameters parameters):
                request_id(request_id),
                track_namespace_prefix(track_namespace_prefix),
                parameters(parameters)
            {}

            

    public:
        RequestID request_id;
        TrackNamespacePrefix track_namespace_prefix;
        Parameters parameters;
    };

    Bytes& operator<<(Bytes& buffer, const SubscribeNamespace& msg);
    BytesSpan operator>>(BytesSpan buffer, SubscribeNamespace& msg);    

    /**
     * @brief SubscribeNamespaceOk
     */
    struct SubscribeNamespaceOk
    {        

    public:
        // Default constructor
        SubscribeNamespaceOk () {}
        
        // All fields constructor
        SubscribeNamespaceOk (
            RequestID request_id):
                request_id(request_id)
            {}

            

    public:
        RequestID request_id;
    };

    Bytes& operator<<(Bytes& buffer, const SubscribeNamespaceOk& msg);
    BytesSpan operator>>(BytesSpan buffer, SubscribeNamespaceOk& msg);    

    /**
     * @brief SubscribeNamespaceError
     */
    struct SubscribeNamespaceError
    {        

    public:
        // Default constructor
        SubscribeNamespaceError () {}
        
        // All fields constructor
        SubscribeNamespaceError (
            RequestID request_id,
            SubscribeNamespaceErrorErrorCode error_code,
            ErrorReason error_reason):
                request_id(request_id),
                error_code(error_code),
                error_reason(error_reason)
            {}

            

    public:
        RequestID request_id;
        SubscribeNamespaceErrorErrorCode error_code;
        ErrorReason error_reason;
    };

    Bytes& operator<<(Bytes& buffer, const SubscribeNamespaceError& msg);
    BytesSpan operator>>(BytesSpan buffer, SubscribeNamespaceError& msg);    

    /**
     * @brief UnsubscribeNamespace
     */
    struct UnsubscribeNamespace
    {        

    public:
        // Default constructor
        UnsubscribeNamespace () {}
        
        // All fields constructor
        UnsubscribeNamespace (
            TrackNamespacePrefix track_namespace_prefix):
                track_namespace_prefix(track_namespace_prefix)
            {}

            

    public:
        TrackNamespacePrefix track_namespace_prefix;
    };

    Bytes& operator<<(Bytes& buffer, const UnsubscribeNamespace& msg);
    BytesSpan operator>>(BytesSpan buffer, UnsubscribeNamespace& msg);    

    /**
     * @brief MaxRequestId
     */
    struct MaxRequestId
    {        

    public:
        // Default constructor
        MaxRequestId () {}
        
        // All fields constructor
        MaxRequestId (
            RequestID request_id):
                request_id(request_id)
            {}

            

    public:
        RequestID request_id;
    };

    Bytes& operator<<(Bytes& buffer, const MaxRequestId& msg);
    BytesSpan operator>>(BytesSpan buffer, MaxRequestId& msg);    

    /**
     * @brief Fetch
     */
    struct Fetch
    {        
    public:
        // Optional Groups
        struct Group_0 {
            Standalone standalone;
        };
        struct Group_1 {
            Joining joining;
        };

    public:
        // Have optionals - delete default constructor
        Fetch () = delete;
        
        // All fields constructor
        Fetch (
            RequestID request_id,
            SubscriberPriority subscriber_priority,
            GroupOrder group_order,
            FetchType fetch_type,
            std::optional<Fetch::Group_0> group_0,
            std::optional<Fetch::Group_1> group_1,
            Parameters parameters):
                request_id(request_id),
                subscriber_priority(subscriber_priority),
                group_order(group_order),
                fetch_type(fetch_type),
                group_0(group_0),
                group_1(group_1),
                parameters(parameters)
            {}

            
        // Optional callback constructor 

        Fetch (
            std::function<void (Fetch&)> group_0_cb,
            std::function<void (Fetch&)> group_1_cb
        );

    public:
        RequestID request_id;
        SubscriberPriority subscriber_priority;
        GroupOrder group_order;
        FetchType fetch_type;
        std::function<void (Fetch&)> group_0_cb;
        std::optional<Fetch::Group_0> group_0;
        std::function<void (Fetch&)> group_1_cb;
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

    public:
        // Default constructor
        FetchCancel () {}
        
        // All fields constructor
        FetchCancel (
            RequestID request_id):
                request_id(request_id)
            {}

            

    public:
        RequestID request_id;
    };

    Bytes& operator<<(Bytes& buffer, const FetchCancel& msg);
    BytesSpan operator>>(BytesSpan buffer, FetchCancel& msg);    

    /**
     * @brief FetchOk
     */
    struct FetchOk
    {        

    public:
        // Default constructor
        FetchOk () {}
        
        // All fields constructor
        FetchOk (
            RequestID request_id,
            GroupOrder group_order,
            EndOfTrack end_of_track,
            EndLocation end_location,
            Parameters parameters):
                request_id(request_id),
                group_order(group_order),
                end_of_track(end_of_track),
                end_location(end_location),
                parameters(parameters)
            {}

            

    public:
        RequestID request_id;
        GroupOrder group_order;
        EndOfTrack end_of_track;
        EndLocation end_location;
        Parameters parameters;
    };

    Bytes& operator<<(Bytes& buffer, const FetchOk& msg);
    BytesSpan operator>>(BytesSpan buffer, FetchOk& msg);    

    /**
     * @brief FetchError
     */
    struct FetchError
    {        

    public:
        // Default constructor
        FetchError () {}
        
        // All fields constructor
        FetchError (
            RequestID request_id,
            FetchErrorErrorCode error_code,
            ErrorReason error_reason):
                request_id(request_id),
                error_code(error_code),
                error_reason(error_reason)
            {}

            

    public:
        RequestID request_id;
        FetchErrorErrorCode error_code;
        ErrorReason error_reason;
    };

    Bytes& operator<<(Bytes& buffer, const FetchError& msg);
    BytesSpan operator>>(BytesSpan buffer, FetchError& msg);    

    /**
     * @brief RequestsBlocked
     */
    struct RequestsBlocked
    {        

    public:
        // Default constructor
        RequestsBlocked () {}
        
        // All fields constructor
        RequestsBlocked (
            MaximumRequestID maximum_request_id):
                maximum_request_id(maximum_request_id)
            {}

            

    public:
        MaximumRequestID maximum_request_id;
    };

    Bytes& operator<<(Bytes& buffer, const RequestsBlocked& msg);
    BytesSpan operator>>(BytesSpan buffer, RequestsBlocked& msg);    

    /**
     * @brief Publish
     */
    struct Publish
    {        
    public:
        // Optional Groups
        struct Group_0 {
            LargestLocation largest_location;
        };

    public:
        // Have optionals - delete default constructor
        Publish () = delete;
        
        // All fields constructor
        Publish (
            RequestID request_id,
            TrackNamespace track_namespace,
            TrackName track_name,
            TrackAlias track_alias,
            GroupOrder group_order,
            ContentExists content_exists,
            std::optional<Publish::Group_0> group_0,
            Forward forward,
            Parameters parameters):
                request_id(request_id),
                track_namespace(track_namespace),
                track_name(track_name),
                track_alias(track_alias),
                group_order(group_order),
                content_exists(content_exists),
                group_0(group_0),
                forward(forward),
                parameters(parameters)
            {}

            
        // Optional callback constructor 

        Publish (
            std::function<void (Publish&)> group_0_cb
        );

    public:
        RequestID request_id;
        TrackNamespace track_namespace;
        TrackName track_name;
        TrackAlias track_alias;
        GroupOrder group_order;
        ContentExists content_exists;
        std::function<void (Publish&)> group_0_cb;
        std::optional<Publish::Group_0> group_0;
        Forward forward;
        Parameters parameters;
    };

    Bytes& operator<<(Bytes& buffer, const Publish& msg);
    BytesSpan operator>>(BytesSpan buffer, Publish& msg);    

    Bytes& operator<<(Bytes& buffer, const std::optional<Publish::Group_0>& grp);
    BytesSpan operator>>(BytesSpan buffer, std::optional<Publish::Group_0>& grp);

    /**
     * @brief PublishOk
     */
    struct PublishOk
    {        
    public:
        // Optional Groups
        struct Group_0 {
            StartLocation start_location;
        };
        struct Group_1 {
            EndGroup end_group;
        };

    public:
        // Have optionals - delete default constructor
        PublishOk () = delete;
        
        // All fields constructor
        PublishOk (
            RequestID request_id,
            Forward forward,
            SubscriberPriority subscriber_priority,
            GroupOrder group_order,
            FilterType filter_type,
            std::optional<PublishOk::Group_0> group_0,
            std::optional<PublishOk::Group_1> group_1,
            Parameters parameters):
                request_id(request_id),
                forward(forward),
                subscriber_priority(subscriber_priority),
                group_order(group_order),
                filter_type(filter_type),
                group_0(group_0),
                group_1(group_1),
                parameters(parameters)
            {}

            
        // Optional callback constructor 

        PublishOk (
            std::function<void (PublishOk&)> group_0_cb,
            std::function<void (PublishOk&)> group_1_cb
        );

    public:
        RequestID request_id;
        Forward forward;
        SubscriberPriority subscriber_priority;
        GroupOrder group_order;
        FilterType filter_type;
        std::function<void (PublishOk&)> group_0_cb;
        std::optional<PublishOk::Group_0> group_0;
        std::function<void (PublishOk&)> group_1_cb;
        std::optional<PublishOk::Group_1> group_1;
        Parameters parameters;
    };

    Bytes& operator<<(Bytes& buffer, const PublishOk& msg);
    BytesSpan operator>>(BytesSpan buffer, PublishOk& msg);    

    Bytes& operator<<(Bytes& buffer, const std::optional<PublishOk::Group_0>& grp);
    BytesSpan operator>>(BytesSpan buffer, std::optional<PublishOk::Group_0>& grp);

    Bytes& operator<<(Bytes& buffer, const std::optional<PublishOk::Group_1>& grp);
    BytesSpan operator>>(BytesSpan buffer, std::optional<PublishOk::Group_1>& grp);

    /**
     * @brief PublishError
     */
    struct PublishError
    {        

    public:
        // Default constructor
        PublishError () {}
        
        // All fields constructor
        PublishError (
            RequestID request_id,
            ErrorCode error_code,
            ErrorReason error_reason):
                request_id(request_id),
                error_code(error_code),
                error_reason(error_reason)
            {}

            

    public:
        RequestID request_id;
        ErrorCode error_code;
        ErrorReason error_reason;
    };

    Bytes& operator<<(Bytes& buffer, const PublishError& msg);
    BytesSpan operator>>(BytesSpan buffer, PublishError& msg);    

    /**
     * @brief ClientSetup
     */
    struct ClientSetup
    {        

    public:
        // Default constructor
        ClientSetup () {}
        
        // All fields constructor
        ClientSetup (
            SupportedVersions supported_versions,
            SetupParameters setup_parameters):
                supported_versions(supported_versions),
                setup_parameters(setup_parameters)
            {}

            

    public:
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

    public:
        // Default constructor
        ServerSetup () {}
        
        // All fields constructor
        ServerSetup (
            SelectedVersion selected_version,
            SetupParameters setup_parameters):
                selected_version(selected_version),
                setup_parameters(setup_parameters)
            {}

            

    public:
        SelectedVersion selected_version;
        SetupParameters setup_parameters;
    };

    Bytes& operator<<(Bytes& buffer, const ServerSetup& msg);
    BytesSpan operator>>(BytesSpan buffer, ServerSetup& msg);    

    
    
    
    Bytes& operator<<(Bytes& buffer, const std::vector<std::uint64_t>& vec);
    BytesSpan operator>>(BytesSpan buffer, std::vector<std::uint64_t>& vec);

    Bytes& operator<<(Bytes& buffer, const std::vector<quicr::messages::SetupParameter>& vec);
    BytesSpan operator>>(BytesSpan buffer, std::vector<quicr::messages::SetupParameter>& vec);

    Bytes& operator<<(Bytes& buffer, const std::vector<quicr::messages::Parameter>& vec);
    BytesSpan operator>>(BytesSpan buffer, std::vector<quicr::messages::Parameter>& vec);


    Bytes& operator<<(Bytes& buffer, ControlMessageType message_type);

} // namespace
