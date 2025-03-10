
#include "quicr/detail/ctrl_messages.h"

namespace quicr::ctrl_messages {

    /*
     * SubscribeUpdate
     */
    Bytes& operator<<(Bytes& buffer, const SubscribeUpdate& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.subscribe_id; // (i)  << SubscribeID
        payload << msg.start_group; // (i)  << StartGroup
        payload << msg.start_object; // (i)  << StartObject
        payload << msg.end_group; // (i)  << EndGroup
        payload << msg.subscriber_priority; // (8)  << SubscriberPriority
        payload << msg.subscribe_parameters; // (..) ... << SubscribeParameters

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kSubscribeUpdate);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, SubscribeUpdate& msg)
    {
        buffer = buffer >> msg.subscribe_id; // (i)  >> SubscribeID
        buffer = buffer >> msg.start_group; // (i)  >> StartGroup
        buffer = buffer >> msg.start_object; // (i)  >> StartObject
        buffer = buffer >> msg.end_group; // (i)  >> EndGroup
        buffer = buffer >> msg.subscriber_priority; // (8)  >> SubscriberPriority
        buffer = buffer >> msg.subscribe_parameters; // (..) ... >> SubscribeParameters
        return buffer;
    }


    /*
     * Subscribe
     */
    Bytes& operator<<(Bytes& buffer, const Subscribe& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.subscribe_id; // (i)  << SubscribeID
        payload << msg.track_alias; // (i)  << TrackAlias
        payload << msg.track_namespace; // (tuple)  << TrackNamespace
        payload << msg.track_name; // (..)  << TrackName
        payload << msg.subscriber_priority; // (8)  << SubscriberPriority
        payload << msg.group_order; // (8)  << GroupOrder
        payload << msg.filter_type; // (i)  << FilterType
        payload << msg.group_0; // (optional group)  << Subscribe::Group_0
        payload << msg.group_1; // (optional group)  << Subscribe::Group_1
        payload << msg.subscribe_parameters; // (..) ... << SubscribeParameters

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kSubscribe);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, Subscribe& msg)
    {
        buffer = buffer >> msg.subscribe_id; // (i)  >> SubscribeID
        buffer = buffer >> msg.track_alias; // (i)  >> TrackAlias
        buffer = buffer >> msg.track_namespace; // (tuple)  >> TrackNamespace
        buffer = buffer >> msg.track_name; // (..)  >> TrackName
        buffer = buffer >> msg.subscriber_priority; // (8)  >> SubscriberPriority
        buffer = buffer >> msg.group_order; // (8)  >> GroupOrder
        buffer = buffer >> msg.filter_type; // (i)  >> FilterType
        if (msg.optional_group_0_cb) { msg.optional_group_0_cb(msg); }
        buffer = buffer >> msg.group_0; // (optional group)  >> Subscribe::Group_0
        if (msg.optional_group_1_cb) { msg.optional_group_1_cb(msg); }
        buffer = buffer >> msg.group_1; // (optional group)  >> Subscribe::Group_1
        buffer = buffer >> msg.subscribe_parameters; // (..) ... >> SubscribeParameters
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, const std::optional<Subscribe::Group_0>& grp)
    {
        if (grp.has_value()) {
            buffer << grp->start_group; // (i) << StartGroup
            buffer << grp->start_object; // (i) << StartObject
        }
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, std::optional<Subscribe::Group_0>& grp)
    {
        if (grp.has_value()) {
            buffer = buffer >> grp->start_group; // (i) >> StartGroup
            buffer = buffer >> grp->start_object; // (i) >> StartObject
        }
        return buffer;
    }    
    Bytes& operator<<(Bytes& buffer, const std::optional<Subscribe::Group_1>& grp)
    {
        if (grp.has_value()) {
            buffer << grp->end_group; // (i) << EndGroup
        }
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, std::optional<Subscribe::Group_1>& grp)
    {
        if (grp.has_value()) {
            buffer = buffer >> grp->end_group; // (i) >> EndGroup
        }
        return buffer;
    }    

    /*
     * SubscribeOk
     */
    Bytes& operator<<(Bytes& buffer, const SubscribeOk& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.subscribe_id; // (i)  << SubscribeID
        payload << msg.expires; // (i)  << Expires
        payload << msg.group_order; // (8)  << GroupOrder
        payload << msg.content_exists; // (8)  << ContentExists
        payload << msg.group_0; // (optional group)  << SubscribeOk::Group_0
        payload << msg.subscribe_parameters; // (..) ... << SubscribeParameters

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kSubscribeOk);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, SubscribeOk& msg)
    {
        buffer = buffer >> msg.subscribe_id; // (i)  >> SubscribeID
        buffer = buffer >> msg.expires; // (i)  >> Expires
        buffer = buffer >> msg.group_order; // (8)  >> GroupOrder
        buffer = buffer >> msg.content_exists; // (8)  >> ContentExists
        if (msg.optional_group_0_cb) { msg.optional_group_0_cb(msg); }
        buffer = buffer >> msg.group_0; // (optional group)  >> SubscribeOk::Group_0
        buffer = buffer >> msg.subscribe_parameters; // (..) ... >> SubscribeParameters
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, const std::optional<SubscribeOk::Group_0>& grp)
    {
        if (grp.has_value()) {
            buffer << grp->largest_group_id; // (i) << LargestGroupID
            buffer << grp->largest_object_id; // (i) << LargestObjectID
        }
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, std::optional<SubscribeOk::Group_0>& grp)
    {
        if (grp.has_value()) {
            buffer = buffer >> grp->largest_group_id; // (i) >> LargestGroupID
            buffer = buffer >> grp->largest_object_id; // (i) >> LargestObjectID
        }
        return buffer;
    }    

    /*
     * SubscribeError
     */
    Bytes& operator<<(Bytes& buffer, const SubscribeError& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.subscribe_id; // (i)  << SubscribeID
        payload << msg.error_code; // (i)  << ErrorCode
        payload << msg.reason_phrase; // (..)  << ReasonPhrase
        payload << msg.track_alias; // (i)  << TrackAlias

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kSubscribeError);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, SubscribeError& msg)
    {
        buffer = buffer >> msg.subscribe_id; // (i)  >> SubscribeID
        buffer = buffer >> msg.error_code; // (i)  >> ErrorCode
        buffer = buffer >> msg.reason_phrase; // (..)  >> ReasonPhrase
        buffer = buffer >> msg.track_alias; // (i)  >> TrackAlias
        return buffer;
    }


    /*
     * Announce
     */
    Bytes& operator<<(Bytes& buffer, const Announce& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.track_namespace; // (tuple)  << TrackNamespace
        payload << msg.parameters; // (..) ... << Parameters

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kAnnounce);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, Announce& msg)
    {
        buffer = buffer >> msg.track_namespace; // (tuple)  >> TrackNamespace
        buffer = buffer >> msg.parameters; // (..) ... >> Parameters
        return buffer;
    }


    /*
     * AnnounceOk
     */
    Bytes& operator<<(Bytes& buffer, const AnnounceOk& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.track_namespace; // (tuple)  << TrackNamespace

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kAnnounceOk);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, AnnounceOk& msg)
    {
        buffer = buffer >> msg.track_namespace; // (tuple)  >> TrackNamespace
        return buffer;
    }


    /*
     * AnnounceError
     */
    Bytes& operator<<(Bytes& buffer, const AnnounceError& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.track_namespace; // (tuple)  << TrackNamespace
        payload << msg.error_code; // (i)  << ErrorCode
        payload << msg.reason_phrase; // (..)  << ReasonPhrase

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kAnnounceError);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, AnnounceError& msg)
    {
        buffer = buffer >> msg.track_namespace; // (tuple)  >> TrackNamespace
        buffer = buffer >> msg.error_code; // (i)  >> ErrorCode
        buffer = buffer >> msg.reason_phrase; // (..)  >> ReasonPhrase
        return buffer;
    }


    /*
     * Unannounce
     */
    Bytes& operator<<(Bytes& buffer, const Unannounce& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.track_namespace; // (tuple)  << TrackNamespace

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kUnannounce);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, Unannounce& msg)
    {
        buffer = buffer >> msg.track_namespace; // (tuple)  >> TrackNamespace
        return buffer;
    }


    /*
     * Unsubscribe
     */
    Bytes& operator<<(Bytes& buffer, const Unsubscribe& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.subscribe_id; // (i)  << SubscribeID

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kUnsubscribe);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, Unsubscribe& msg)
    {
        buffer = buffer >> msg.subscribe_id; // (i)  >> SubscribeID
        return buffer;
    }


    /*
     * SubscribeDone
     */
    Bytes& operator<<(Bytes& buffer, const SubscribeDone& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.subscribe_id; // (i)  << SubscribeID
        payload << msg.status_code; // (i)  << StatusCode
        payload << msg.stream_count; // (i)  << StreamCount
        payload << msg.reason_phrase; // (..)  << ReasonPhrase

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kSubscribeDone);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, SubscribeDone& msg)
    {
        buffer = buffer >> msg.subscribe_id; // (i)  >> SubscribeID
        buffer = buffer >> msg.status_code; // (i)  >> StatusCode
        buffer = buffer >> msg.stream_count; // (i)  >> StreamCount
        buffer = buffer >> msg.reason_phrase; // (..)  >> ReasonPhrase
        return buffer;
    }


    /*
     * AnnounceCancel
     */
    Bytes& operator<<(Bytes& buffer, const AnnounceCancel& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.track_namespace; // (tuple)  << TrackNamespace
        payload << msg.error_code; // (i)  << ErrorCode
        payload << msg.reason_phrase; // (..)  << ReasonPhrase

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kAnnounceCancel);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, AnnounceCancel& msg)
    {
        buffer = buffer >> msg.track_namespace; // (tuple)  >> TrackNamespace
        buffer = buffer >> msg.error_code; // (i)  >> ErrorCode
        buffer = buffer >> msg.reason_phrase; // (..)  >> ReasonPhrase
        return buffer;
    }


    /*
     * TrackStatusRequest
     */
    Bytes& operator<<(Bytes& buffer, const TrackStatusRequest& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.track_namespace; // (tuple)  << TrackNamespace
        payload << msg.track_name; // (..)  << TrackName

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kTrackStatusRequest);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, TrackStatusRequest& msg)
    {
        buffer = buffer >> msg.track_namespace; // (tuple)  >> TrackNamespace
        buffer = buffer >> msg.track_name; // (..)  >> TrackName
        return buffer;
    }


    /*
     * TrackStatus
     */
    Bytes& operator<<(Bytes& buffer, const TrackStatus& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.track_namespace; // (tuple)  << TrackNamespace
        payload << msg.track_name; // (..)  << TrackName
        payload << msg.status_code; // (i)  << StatusCode
        payload << msg.last_group_id; // (i)  << LastGroupID
        payload << msg.last_object_id; // (i)  << LastObjectID

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kTrackStatus);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, TrackStatus& msg)
    {
        buffer = buffer >> msg.track_namespace; // (tuple)  >> TrackNamespace
        buffer = buffer >> msg.track_name; // (..)  >> TrackName
        buffer = buffer >> msg.status_code; // (i)  >> StatusCode
        buffer = buffer >> msg.last_group_id; // (i)  >> LastGroupID
        buffer = buffer >> msg.last_object_id; // (i)  >> LastObjectID
        return buffer;
    }


    /*
     * Goaway
     */
    Bytes& operator<<(Bytes& buffer, const Goaway& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.new_session_uri; // (..)  << NewSessionURI

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kGoaway);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, Goaway& msg)
    {
        buffer = buffer >> msg.new_session_uri; // (..)  >> NewSessionURI
        return buffer;
    }


    /*
     * SubscribeAnnounces
     */
    Bytes& operator<<(Bytes& buffer, const SubscribeAnnounces& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.track_namespace_prefix; // (tuple)  << TrackNamespacePrefix
        payload << msg.parameters; // (..) ... << Parameters

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kSubscribeAnnounces);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, SubscribeAnnounces& msg)
    {
        buffer = buffer >> msg.track_namespace_prefix; // (tuple)  >> TrackNamespacePrefix
        buffer = buffer >> msg.parameters; // (..) ... >> Parameters
        return buffer;
    }


    /*
     * SubscribeAnnouncesOk
     */
    Bytes& operator<<(Bytes& buffer, const SubscribeAnnouncesOk& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.track_namespace_prefix; // (tuple)  << TrackNamespacePrefix

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kSubscribeAnnouncesOk);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, SubscribeAnnouncesOk& msg)
    {
        buffer = buffer >> msg.track_namespace_prefix; // (tuple)  >> TrackNamespacePrefix
        return buffer;
    }


    /*
     * SubscribeAnnouncesError
     */
    Bytes& operator<<(Bytes& buffer, const SubscribeAnnouncesError& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.track_namespace_prefix; // (tuple)  << TrackNamespacePrefix
        payload << msg.error_code; // (i)  << ErrorCode
        payload << msg.reason_phrase; // (..)  << ReasonPhrase

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kSubscribeAnnouncesError);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, SubscribeAnnouncesError& msg)
    {
        buffer = buffer >> msg.track_namespace_prefix; // (tuple)  >> TrackNamespacePrefix
        buffer = buffer >> msg.error_code; // (i)  >> ErrorCode
        buffer = buffer >> msg.reason_phrase; // (..)  >> ReasonPhrase
        return buffer;
    }


    /*
     * UnsubscribeAnnounces
     */
    Bytes& operator<<(Bytes& buffer, const UnsubscribeAnnounces& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.track_namespace_prefix; // (tuple)  << TrackNamespacePrefix

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kUnsubscribeAnnounces);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, UnsubscribeAnnounces& msg)
    {
        buffer = buffer >> msg.track_namespace_prefix; // (tuple)  >> TrackNamespacePrefix
        return buffer;
    }


    /*
     * MaxSubscribeId
     */
    Bytes& operator<<(Bytes& buffer, const MaxSubscribeId& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.subscribe_id; // (i)  << SubscribeID

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kMaxSubscribeId);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, MaxSubscribeId& msg)
    {
        buffer = buffer >> msg.subscribe_id; // (i)  >> SubscribeID
        return buffer;
    }


    /*
     * Fetch
     */
    Bytes& operator<<(Bytes& buffer, const Fetch& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.subscribe_id; // (i)  << SubscribeID
        payload << msg.subscriber_priority; // (8)  << SubscriberPriority
        payload << msg.group_order; // (8)  << GroupOrder
        payload << msg.fetch_type; // (i)  << FetchType
        payload << msg.group_0; // (optional group)  << Fetch::Group_0
        payload << msg.group_1; // (optional group)  << Fetch::Group_1
        payload << msg.parameters; // (..) ... << Parameters

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kFetch);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, Fetch& msg)
    {
        buffer = buffer >> msg.subscribe_id; // (i)  >> SubscribeID
        buffer = buffer >> msg.subscriber_priority; // (8)  >> SubscriberPriority
        buffer = buffer >> msg.group_order; // (8)  >> GroupOrder
        buffer = buffer >> msg.fetch_type; // (i)  >> FetchType
        if (msg.optional_group_0_cb) { msg.optional_group_0_cb(msg); }
        buffer = buffer >> msg.group_0; // (optional group)  >> Fetch::Group_0
        if (msg.optional_group_1_cb) { msg.optional_group_1_cb(msg); }
        buffer = buffer >> msg.group_1; // (optional group)  >> Fetch::Group_1
        buffer = buffer >> msg.parameters; // (..) ... >> Parameters
        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, const std::optional<Fetch::Group_0>& grp)
    {
        if (grp.has_value()) {
            buffer << grp->track_namespace; // (tuple) << TrackNamespace
            buffer << grp->track_name; // (..) << TrackName
            buffer << grp->start_group; // (i) << StartGroup
            buffer << grp->start_object; // (i) << StartObject
            buffer << grp->end_group; // (i) << EndGroup
            buffer << grp->end_object; // (i) << EndObject
        }
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, std::optional<Fetch::Group_0>& grp)
    {
        if (grp.has_value()) {
            buffer = buffer >> grp->track_namespace; // (tuple) >> TrackNamespace
            buffer = buffer >> grp->track_name; // (..) >> TrackName
            buffer = buffer >> grp->start_group; // (i) >> StartGroup
            buffer = buffer >> grp->start_object; // (i) >> StartObject
            buffer = buffer >> grp->end_group; // (i) >> EndGroup
            buffer = buffer >> grp->end_object; // (i) >> EndObject
        }
        return buffer;
    }    
    Bytes& operator<<(Bytes& buffer, const std::optional<Fetch::Group_1>& grp)
    {
        if (grp.has_value()) {
            buffer << grp->joining_subscribe_id; // (i) << JoiningSubscribeID
            buffer << grp->preceding_group_offset; // (i) << PrecedingGroupOffset
        }
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, std::optional<Fetch::Group_1>& grp)
    {
        if (grp.has_value()) {
            buffer = buffer >> grp->joining_subscribe_id; // (i) >> JoiningSubscribeID
            buffer = buffer >> grp->preceding_group_offset; // (i) >> PrecedingGroupOffset
        }
        return buffer;
    }    

    /*
     * FetchCancel
     */
    Bytes& operator<<(Bytes& buffer, const FetchCancel& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.subscribe_id; // (i)  << SubscribeID

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kFetchCancel);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, FetchCancel& msg)
    {
        buffer = buffer >> msg.subscribe_id; // (i)  >> SubscribeID
        return buffer;
    }


    /*
     * FetchOk
     */
    Bytes& operator<<(Bytes& buffer, const FetchOk& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.subscribe_id; // (i)  << SubscribeID
        payload << msg.group_order; // (8)  << GroupOrder
        payload << msg.end_of_track; // (8)  << EndOfTrack
        payload << msg.largest_group_id; // (i)  << LargestGroupID
        payload << msg.largest_object_id; // (i)  << LargestObjectID
        payload << msg.subscribe_parameters; // (..) ... << SubscribeParameters

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kFetchOk);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, FetchOk& msg)
    {
        buffer = buffer >> msg.subscribe_id; // (i)  >> SubscribeID
        buffer = buffer >> msg.group_order; // (8)  >> GroupOrder
        buffer = buffer >> msg.end_of_track; // (8)  >> EndOfTrack
        buffer = buffer >> msg.largest_group_id; // (i)  >> LargestGroupID
        buffer = buffer >> msg.largest_object_id; // (i)  >> LargestObjectID
        buffer = buffer >> msg.subscribe_parameters; // (..) ... >> SubscribeParameters
        return buffer;
    }


    /*
     * FetchError
     */
    Bytes& operator<<(Bytes& buffer, const FetchError& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.subscribe_id; // (i)  << SubscribeID
        payload << msg.error_code; // (i)  << ErrorCode
        payload << msg.reason_phrase; // (..)  << ReasonPhrase

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kFetchError);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, FetchError& msg)
    {
        buffer = buffer >> msg.subscribe_id; // (i)  >> SubscribeID
        buffer = buffer >> msg.error_code; // (i)  >> ErrorCode
        buffer = buffer >> msg.reason_phrase; // (..)  >> ReasonPhrase
        return buffer;
    }


    /*
     * SubscribesBlocked
     */
    Bytes& operator<<(Bytes& buffer, const SubscribesBlocked& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.maximum_subscribe_id; // (i)  << MaximumSubscribeID

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kSubscribesBlocked);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, SubscribesBlocked& msg)
    {
        buffer = buffer >> msg.maximum_subscribe_id; // (i)  >> MaximumSubscribeID
        return buffer;
    }


    /*
     * ClientSetup
     */
    Bytes& operator<<(Bytes& buffer, const ClientSetup& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.supported_versions; // (i) ... << SupportedVersions
        payload << msg.setup_parameters; // (..) ... << SetupParameters

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kClientSetup);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, ClientSetup& msg)
    {
        buffer = buffer >> msg.supported_versions; // (i) ... >> SupportedVersions
        buffer = buffer >> msg.setup_parameters; // (..) ... >> SetupParameters
        return buffer;
    }


    /*
     * ServerSetup
     */
    Bytes& operator<<(Bytes& buffer, const ServerSetup& msg)
    {
        Bytes payload;

        // fill out payload
        payload << msg.selected_version; // (i)  << SelectedVersion
        payload << msg.setup_parameters; // (..) ... << SetupParameters

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kServerSetup);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, ServerSetup& msg)
    {
        buffer = buffer >> msg.selected_version; // (i)  >> SelectedVersion
        buffer = buffer >> msg.setup_parameters; // (..) ... >> SetupParameters
        return buffer;
    }


    Bytes &operator<<(Bytes &buffer, ControlMessageType message_type)
    {
        UintVar varint = static_cast<std::uint64_t>(message_type);
        buffer << varint;
        return buffer;
    }
  
    Bytes& operator<<(Bytes& buffer, const TrackNamespace& ns)
    {
        const auto& entries = ns.GetEntries();

        buffer << UintVar(entries.size());
        for (const auto& entry : entries) {
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

    Bytes& operator<<(Bytes& buffer, const std::vector<quicr::ctrl_messages::Parameter>& params)
    {
        buffer << params.size();
        for (const auto& param : params) {
            buffer << param;
        }
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, std::vector<quicr::ctrl_messages::Parameter>& params)
    {
        uint64_t size = 0;
        buffer = buffer >> size;

        for (int i = 0; i < size; ++i) {
            quicr::ctrl_messages::Parameter param;
            buffer = buffer >> param;
            params.push_back(param);
        }

        return buffer;
    }

    Bytes& operator<<(Bytes& buffer, const std::vector<std::uint64_t>& vec)
    {
        buffer << vec.size();
        for (const auto& elem : vec) {
            buffer << elem;
        }
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, std::vector<std::uint64_t>& vec)
    {
        uint64_t size = 0;
        buffer = buffer >> size;

        for (int i = 0; i < size; ++i) {
            std::uint64_t elem;;
            buffer = buffer >> elem;
            vec.push_back(elem);
        }

        return buffer;
    }        

    Bytes& operator<<(Bytes& buffer, const quicr::ctrl_messages::Parameter& param)
    {
        buffer << param.type;
        buffer << param.value;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, quicr::ctrl_messages::Parameter& param)
    {
        buffer = buffer >> param.type;
        buffer = buffer >> param.value;
        return buffer;
    }    

} // namespace

