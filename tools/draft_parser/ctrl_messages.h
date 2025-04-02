#pragma once
#include "ctrl_message_types.h"
#include <quicr/common.h>
#include <quicr/track_name.h>
#include <vector>

namespace quicr::messages {

    // usings
    using SupportedVersions = std::vector<std::uint64_t>;
    using SetupParameters = std::vector<quicr::messages::Parameter>;
    using SelectedVersion = std::uint64_t;
    using NewSessionURI = quicr::Bytes;
    using SubscribeID = std::uint64_t;
    using MaximumSubscribeID = std::uint64_t;
    using TrackAlias = std::uint64_t;
    using TrackNamespace = quicr::TrackNamespace;
    using TrackName = quicr::Bytes;
    using SubscriberPriority = std::uint8_t;
    using GroupOrder = quicr::messages::GroupOrder;
    using FilterType = quicr::messages::FilterType;
    using StartGroup = quicr::messages::GroupId;
    using StartObject = quicr::messages::ObjectId;
    using EndGroup = quicr::messages::GroupId;
    using SubscribeParameters = std::vector<quicr::messages::Parameter>;
    using Expires = std::uint64_t;
    using ContentExists = std::uint8_t;
    using LargestGroupID = std::uint64_t;
    using LargestObjectID = std::uint64_t;
    using SubscribeErrorErrorCode = quicr::messages::SubscribeErrorCode;
    using ReasonPhrase = quicr::Bytes;
    using SubscribeDoneStatusCode = quicr::messages::SubscribeDoneStatusCode;
    using StreamCount = std::uint64_t;
    using FetchType = quicr::messages::FetchType;
    using EndObject = quicr::messages::ObjectId;
    using JoiningSubscribeID = std::uint64_t;
    using PrecedingGroupOffset = std::uint64_t;
    using Parameters = std::vector<quicr::messages::Parameter>;
    using EndOfTrack = std::uint8_t;
    using FetchErrorErrorCode = quicr::messages::FetchErrorCode;
    using StatusCode = std::uint64_t;
    using LastGroupID = std::uint64_t;
    using LastObjectID = std::uint64_t;
    using AnnounceErrorErrorCode = quicr::messages::AnnounceErrorCode;
    using AnnounceCancelErrorCode = quicr::messages::AnnounceErrorCode;
    using TrackNamespacePrefix = quicr::TrackNamespace;
    using SubscribeAnnouncesErrorErrorCode = quicr::messages::SubscribeAnnouncesErrorCode;

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
        kNewGroupRequest = 0x42,
    };
    /**
     * @brief SubscribeUpdate
     */
    struct SubscribeUpdate
    {

      public:
        // Default constructor
        SubscribeUpdate() {}

        // All fields constructor
        SubscribeUpdate(SubscribeID subscribe_id,
                        StartGroup start_group,
                        StartObject start_object,
                        EndGroup end_group,
                        SubscriberPriority subscriber_priority,
                        SubscribeParameters subscribe_parameters)
          : subscribe_id(subscribe_id)
          , start_group(start_group)
          , start_object(start_object)
          , end_group(end_group)
          , subscriber_priority(subscriber_priority)
          , subscribe_parameters(subscribe_parameters)
        {
        }

      public:
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
      public:
        // Optional Groups
        struct Group_0
        {
            StartGroup start_group;
            StartObject start_object;
        };
        struct Group_1
        {
            EndGroup end_group;
        };

      public:
        // Have optionals - delete default constructor
        Subscribe() = delete;

        // All fields constructor
        Subscribe(SubscribeID subscribe_id,
                  TrackAlias track_alias,
                  TrackNamespace track_namespace,
                  TrackName track_name,
                  SubscriberPriority subscriber_priority,
                  GroupOrder group_order,
                  FilterType filter_type,
                  std::function<void(Subscribe&)> group_0_cb,
                  std::optional<Subscribe::Group_0> group_0,
                  std::function<void(Subscribe&)> group_1_cb,
                  std::optional<Subscribe::Group_1> group_1,
                  SubscribeParameters subscribe_parameters)
          : subscribe_id(subscribe_id)
          , track_alias(track_alias)
          , track_namespace(track_namespace)
          , track_name(track_name)
          , subscriber_priority(subscriber_priority)
          , group_order(group_order)
          , filter_type(filter_type)
          , group_0_cb(group_0_cb)
          , group_0(group_0)
          , group_1_cb(group_1_cb)
          , group_1(group_1)
          , subscribe_parameters(subscribe_parameters)
        {
        }

        // Optional callback constructor

        Subscribe(std::function<void(Subscribe&)> group_0_cb, std::function<void(Subscribe&)> group_1_cb);

      public:
        SubscribeID subscribe_id;
        TrackAlias track_alias;
        TrackNamespace track_namespace;
        TrackName track_name;
        SubscriberPriority subscriber_priority;
        GroupOrder group_order;
        FilterType filter_type;
        std::function<void(Subscribe&)> group_0_cb;
        std::optional<Subscribe::Group_0> group_0;
        std::function<void(Subscribe&)> group_1_cb;
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
      public:
        // Optional Groups
        struct Group_0
        {
            LargestGroupID largest_group_id;
            LargestObjectID largest_object_id;
        };

      public:
        // Have optionals - delete default constructor
        SubscribeOk() = delete;

        // All fields constructor
        SubscribeOk(SubscribeID subscribe_id,
                    Expires expires,
                    GroupOrder group_order,
                    ContentExists content_exists,
                    std::function<void(SubscribeOk&)> group_0_cb,
                    std::optional<SubscribeOk::Group_0> group_0,
                    SubscribeParameters subscribe_parameters)
          : subscribe_id(subscribe_id)
          , expires(expires)
          , group_order(group_order)
          , content_exists(content_exists)
          , group_0_cb(group_0_cb)
          , group_0(group_0)
          , subscribe_parameters(subscribe_parameters)
        {
        }

        // Optional callback constructor

        SubscribeOk(std::function<void(SubscribeOk&)> group_0_cb);

      public:
        SubscribeID subscribe_id;
        Expires expires;
        GroupOrder group_order;
        ContentExists content_exists;
        std::function<void(SubscribeOk&)> group_0_cb;
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

      public:
        // Default constructor
        SubscribeError() {}

        // All fields constructor
        SubscribeError(SubscribeID subscribe_id,
                       SubscribeErrorErrorCode error_code,
                       ReasonPhrase reason_phrase,
                       TrackAlias track_alias)
          : subscribe_id(subscribe_id)
          , error_code(error_code)
          , reason_phrase(reason_phrase)
          , track_alias(track_alias)
        {
        }

      public:
        SubscribeID subscribe_id;
        SubscribeErrorErrorCode error_code;
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

      public:
        // Default constructor
        Announce() {}

        // All fields constructor
        Announce(TrackNamespace track_namespace, Parameters parameters)
          : track_namespace(track_namespace)
          , parameters(parameters)
        {
        }

      public:
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

      public:
        // Default constructor
        AnnounceOk() {}

        // All fields constructor
        AnnounceOk(TrackNamespace track_namespace)
          : track_namespace(track_namespace)
        {
        }

      public:
        TrackNamespace track_namespace;
    };

    Bytes& operator<<(Bytes& buffer, const AnnounceOk& msg);
    BytesSpan operator>>(BytesSpan buffer, AnnounceOk& msg);

    /**
     * @brief AnnounceError
     */
    struct AnnounceError
    {

      public:
        // Default constructor
        AnnounceError() {}

        // All fields constructor
        AnnounceError(TrackNamespace track_namespace, AnnounceErrorErrorCode error_code, ReasonPhrase reason_phrase)
          : track_namespace(track_namespace)
          , error_code(error_code)
          , reason_phrase(reason_phrase)
        {
        }

      public:
        TrackNamespace track_namespace;
        AnnounceErrorErrorCode error_code;
        ReasonPhrase reason_phrase;
    };

    Bytes& operator<<(Bytes& buffer, const AnnounceError& msg);
    BytesSpan operator>>(BytesSpan buffer, AnnounceError& msg);

    /**
     * @brief Unannounce
     */
    struct Unannounce
    {

      public:
        // Default constructor
        Unannounce() {}

        // All fields constructor
        Unannounce(TrackNamespace track_namespace)
          : track_namespace(track_namespace)
        {
        }

      public:
        TrackNamespace track_namespace;
    };

    Bytes& operator<<(Bytes& buffer, const Unannounce& msg);
    BytesSpan operator>>(BytesSpan buffer, Unannounce& msg);

    /**
     * @brief Unsubscribe
     */
    struct Unsubscribe
    {

      public:
        // Default constructor
        Unsubscribe() {}

        // All fields constructor
        Unsubscribe(SubscribeID subscribe_id)
          : subscribe_id(subscribe_id)
        {
        }

      public:
        SubscribeID subscribe_id;
    };

    Bytes& operator<<(Bytes& buffer, const Unsubscribe& msg);
    BytesSpan operator>>(BytesSpan buffer, Unsubscribe& msg);

    /**
     * @brief SubscribeDone
     */
    struct SubscribeDone
    {

      public:
        // Default constructor
        SubscribeDone() {}

        // All fields constructor
        SubscribeDone(SubscribeID subscribe_id,
                      SubscribeDoneStatusCode status_code,
                      StreamCount stream_count,
                      ReasonPhrase reason_phrase)
          : subscribe_id(subscribe_id)
          , status_code(status_code)
          , stream_count(stream_count)
          , reason_phrase(reason_phrase)
        {
        }

      public:
        SubscribeID subscribe_id;
        SubscribeDoneStatusCode status_code;
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

      public:
        // Default constructor
        AnnounceCancel() {}

        // All fields constructor
        AnnounceCancel(TrackNamespace track_namespace, AnnounceCancelErrorCode error_code, ReasonPhrase reason_phrase)
          : track_namespace(track_namespace)
          , error_code(error_code)
          , reason_phrase(reason_phrase)
        {
        }

      public:
        TrackNamespace track_namespace;
        AnnounceCancelErrorCode error_code;
        ReasonPhrase reason_phrase;
    };

    Bytes& operator<<(Bytes& buffer, const AnnounceCancel& msg);
    BytesSpan operator>>(BytesSpan buffer, AnnounceCancel& msg);

    /**
     * @brief TrackStatusRequest
     */
    struct TrackStatusRequest
    {

      public:
        // Default constructor
        TrackStatusRequest() {}

        // All fields constructor
        TrackStatusRequest(TrackNamespace track_namespace, TrackName track_name)
          : track_namespace(track_namespace)
          , track_name(track_name)
        {
        }

      public:
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

      public:
        // Default constructor
        TrackStatus() {}

        // All fields constructor
        TrackStatus(TrackNamespace track_namespace,
                    TrackName track_name,
                    StatusCode status_code,
                    LastGroupID last_group_id,
                    LastObjectID last_object_id)
          : track_namespace(track_namespace)
          , track_name(track_name)
          , status_code(status_code)
          , last_group_id(last_group_id)
          , last_object_id(last_object_id)
        {
        }

      public:
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

      public:
        // Default constructor
        Goaway() {}

        // All fields constructor
        Goaway(NewSessionURI new_session_uri)
          : new_session_uri(new_session_uri)
        {
        }

      public:
        NewSessionURI new_session_uri;
    };

    Bytes& operator<<(Bytes& buffer, const Goaway& msg);
    BytesSpan operator>>(BytesSpan buffer, Goaway& msg);

    /**
     * @brief SubscribeAnnounces
     */
    struct SubscribeAnnounces
    {

      public:
        // Default constructor
        SubscribeAnnounces() {}

        // All fields constructor
        SubscribeAnnounces(TrackNamespacePrefix track_namespace_prefix, Parameters parameters)
          : track_namespace_prefix(track_namespace_prefix)
          , parameters(parameters)
        {
        }

      public:
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

      public:
        // Default constructor
        SubscribeAnnouncesOk() {}

        // All fields constructor
        SubscribeAnnouncesOk(TrackNamespacePrefix track_namespace_prefix)
          : track_namespace_prefix(track_namespace_prefix)
        {
        }

      public:
        TrackNamespacePrefix track_namespace_prefix;
    };

    Bytes& operator<<(Bytes& buffer, const SubscribeAnnouncesOk& msg);
    BytesSpan operator>>(BytesSpan buffer, SubscribeAnnouncesOk& msg);

    /**
     * @brief SubscribeAnnouncesError
     */
    struct SubscribeAnnouncesError
    {

      public:
        // Default constructor
        SubscribeAnnouncesError() {}

        // All fields constructor
        SubscribeAnnouncesError(TrackNamespacePrefix track_namespace_prefix,
                                SubscribeAnnouncesErrorErrorCode error_code,
                                ReasonPhrase reason_phrase)
          : track_namespace_prefix(track_namespace_prefix)
          , error_code(error_code)
          , reason_phrase(reason_phrase)
        {
        }

      public:
        TrackNamespacePrefix track_namespace_prefix;
        SubscribeAnnouncesErrorErrorCode error_code;
        ReasonPhrase reason_phrase;
    };

    Bytes& operator<<(Bytes& buffer, const SubscribeAnnouncesError& msg);
    BytesSpan operator>>(BytesSpan buffer, SubscribeAnnouncesError& msg);

    /**
     * @brief UnsubscribeAnnounces
     */
    struct UnsubscribeAnnounces
    {

      public:
        // Default constructor
        UnsubscribeAnnounces() {}

        // All fields constructor
        UnsubscribeAnnounces(TrackNamespacePrefix track_namespace_prefix)
          : track_namespace_prefix(track_namespace_prefix)
        {
        }

      public:
        TrackNamespacePrefix track_namespace_prefix;
    };

    Bytes& operator<<(Bytes& buffer, const UnsubscribeAnnounces& msg);
    BytesSpan operator>>(BytesSpan buffer, UnsubscribeAnnounces& msg);

    /**
     * @brief MaxSubscribeId
     */
    struct MaxSubscribeId
    {

      public:
        // Default constructor
        MaxSubscribeId() {}

        // All fields constructor
        MaxSubscribeId(SubscribeID subscribe_id)
          : subscribe_id(subscribe_id)
        {
        }

      public:
        SubscribeID subscribe_id;
    };

    Bytes& operator<<(Bytes& buffer, const MaxSubscribeId& msg);
    BytesSpan operator>>(BytesSpan buffer, MaxSubscribeId& msg);

    /**
     * @brief Fetch
     */
    struct Fetch
    {
      public:
        // Optional Groups
        struct Group_0
        {
            TrackNamespace track_namespace;
            TrackName track_name;
            StartGroup start_group;
            StartObject start_object;
            EndGroup end_group;
            EndObject end_object;
        };
        struct Group_1
        {
            JoiningSubscribeID joining_subscribe_id;
            PrecedingGroupOffset preceding_group_offset;
        };

      public:
        // Have optionals - delete default constructor
        Fetch() = delete;

        // All fields constructor
        Fetch(SubscribeID subscribe_id,
              SubscriberPriority subscriber_priority,
              GroupOrder group_order,
              FetchType fetch_type,
              std::function<void(Fetch&)> group_0_cb,
              std::optional<Fetch::Group_0> group_0,
              std::function<void(Fetch&)> group_1_cb,
              std::optional<Fetch::Group_1> group_1,
              Parameters parameters)
          : subscribe_id(subscribe_id)
          , subscriber_priority(subscriber_priority)
          , group_order(group_order)
          , fetch_type(fetch_type)
          , group_0_cb(group_0_cb)
          , group_0(group_0)
          , group_1_cb(group_1_cb)
          , group_1(group_1)
          , parameters(parameters)
        {
        }

        // Optional callback constructor

        Fetch(std::function<void(Fetch&)> group_0_cb, std::function<void(Fetch&)> group_1_cb);

      public:
        SubscribeID subscribe_id;
        SubscriberPriority subscriber_priority;
        GroupOrder group_order;
        FetchType fetch_type;
        std::function<void(Fetch&)> group_0_cb;
        std::optional<Fetch::Group_0> group_0;
        std::function<void(Fetch&)> group_1_cb;
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
        FetchCancel() {}

        // All fields constructor
        FetchCancel(SubscribeID subscribe_id)
          : subscribe_id(subscribe_id)
        {
        }

      public:
        SubscribeID subscribe_id;
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
        FetchOk() {}

        // All fields constructor
        FetchOk(SubscribeID subscribe_id,
                GroupOrder group_order,
                EndOfTrack end_of_track,
                LargestGroupID largest_group_id,
                LargestObjectID largest_object_id,
                SubscribeParameters subscribe_parameters)
          : subscribe_id(subscribe_id)
          , group_order(group_order)
          , end_of_track(end_of_track)
          , largest_group_id(largest_group_id)
          , largest_object_id(largest_object_id)
          , subscribe_parameters(subscribe_parameters)
        {
        }

      public:
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

      public:
        // Default constructor
        FetchError() {}

        // All fields constructor
        FetchError(SubscribeID subscribe_id, FetchErrorErrorCode error_code, ReasonPhrase reason_phrase)
          : subscribe_id(subscribe_id)
          , error_code(error_code)
          , reason_phrase(reason_phrase)
        {
        }

      public:
        SubscribeID subscribe_id;
        FetchErrorErrorCode error_code;
        ReasonPhrase reason_phrase;
    };

    Bytes& operator<<(Bytes& buffer, const FetchError& msg);
    BytesSpan operator>>(BytesSpan buffer, FetchError& msg);

    /**
     * @brief SubscribesBlocked
     */
    struct SubscribesBlocked
    {

      public:
        // Default constructor
        SubscribesBlocked() {}

        // All fields constructor
        SubscribesBlocked(MaximumSubscribeID maximum_subscribe_id)
          : maximum_subscribe_id(maximum_subscribe_id)
        {
        }

      public:
        MaximumSubscribeID maximum_subscribe_id;
    };

    Bytes& operator<<(Bytes& buffer, const SubscribesBlocked& msg);
    BytesSpan operator>>(BytesSpan buffer, SubscribesBlocked& msg);

    /**
     * @brief ClientSetup
     */
    struct ClientSetup
    {

      public:
        // Default constructor
        ClientSetup() {}

        // All fields constructor
        ClientSetup(SupportedVersions supported_versions, SetupParameters setup_parameters)
          : supported_versions(supported_versions)
          , setup_parameters(setup_parameters)
        {
        }

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
        ServerSetup() {}

        // All fields constructor
        ServerSetup(SelectedVersion selected_version, SetupParameters setup_parameters)
          : selected_version(selected_version)
          , setup_parameters(setup_parameters)
        {
        }

      public:
        SelectedVersion selected_version;
        SetupParameters setup_parameters;
    };

    Bytes& operator<<(Bytes& buffer, const ServerSetup& msg);
    BytesSpan operator>>(BytesSpan buffer, ServerSetup& msg);

    /**
     * @brief NewGroupRequest
     */
    struct NewGroupRequest
    {

      public:
        // Default constructor
        NewGroupRequest() {}

        // All fields constructor
        NewGroupRequest(SubscribeID subscribe_id, TrackAlias track_alias)
          : subscribe_id(subscribe_id)
          , track_alias(track_alias)
        {
        }

      public:
        SubscribeID subscribe_id;
        TrackAlias track_alias;
    };

    Bytes& operator<<(Bytes& buffer, const NewGroupRequest& msg);
    BytesSpan operator>>(BytesSpan buffer, NewGroupRequest& msg);

    Bytes& operator<<(Bytes& buffer, const std::vector<std::uint64_t>& vec);
    BytesSpan operator>>(BytesSpan buffer, std::vector<std::uint64_t>& vec);

    Bytes& operator<<(Bytes& buffer, const std::vector<quicr::messages::Parameter>& vec);
    BytesSpan operator>>(BytesSpan buffer, std::vector<quicr::messages::Parameter>& vec);

    /**
     * @brief Subscribe attributes
     */
    struct SubscribeAttributes
    {
        uint8_t priority;       ///< Subscriber priority
        GroupOrder group_order; ///< Subscriber group order
    };

    /**
     * @brief Fetch attributes
     */
    struct FetchAttributes
    {
        uint8_t priority;                    ///< Fetch priority
        GroupOrder group_order;              ///< Fetch group order
        StartGroup start_group;              ///< Fetch starting group in range
        StartObject start_object;            ///< Fetch starting object in group
        EndGroup end_group;                  ///< Fetch final group in range
        std::optional<EndObject> end_object; ///< Fetch final object in group
    };

    Bytes& operator<<(Bytes& buffer, ControlMessageType message_type);
    BytesSpan operator>>(BytesSpan buffer, TrackNamespace& msg);
    Bytes& operator<<(Bytes& buffer, const TrackNamespace& msg);

    BytesSpan operator>>(BytesSpan buffer, Parameter& msg);
    Bytes& operator<<(Bytes& buffer, const Parameter& msg);

} // namespace
