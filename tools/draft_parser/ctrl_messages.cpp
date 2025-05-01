
#include "quicr/detail/messages.h"

namespace quicr::messages {
    // usings
    Bytes& operator<<(Bytes& buffer, const std::vector<std::uint64_t>& vec)
    {
        // write vector size
        buffer << static_cast<std::uint64_t>(vec.size());

        // write elements of vector
        for (const auto& item : vec) {
            buffer << item;
        }
        
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, std::vector<std::uint64_t>& vec)
    {
        std::uint64_t size = 0;
        buffer = buffer >> size;

        for (uint64_t i=0; i<size; i++) {
            std::uint64_t item;    
            buffer = buffer >> item;
            vec.push_back(item);
        }

        return buffer;
    }  
    Bytes& operator<<(Bytes& buffer, const std::vector<quicr::messages::Parameter>& vec)
    {
        // write vector size
        buffer << static_cast<std::uint64_t>(vec.size());

        // write elements of vector
        for (const auto& item : vec) {
            buffer << item;
        }
        
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, std::vector<quicr::messages::Parameter>& vec)
    {
        std::uint64_t size = 0;
        buffer = buffer >> size;

        for (uint64_t i=0; i<size; i++) {
            quicr::messages::Parameter item;    
            buffer = buffer >> item;
            vec.push_back(item);
        }

        return buffer;
    }  

    /*
     * SubscribeUpdate stream in
     */
    BytesSpan operator>>(BytesSpan buffer, SubscribeUpdate& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.start_location; // (Location)  >> StartLocation
        buffer = buffer >> msg.end_group; // (i)  >> EndGroup
        buffer = buffer >> msg.subscriber_priority; // (8)  >> SubscriberPriority
        buffer = buffer >> msg.forward; // (8)  >> Forward
        buffer = buffer >> msg.subscribe_parameters; // (..) ... >> SubscribeParameters
        return buffer;
    }

    /*
     * SubscribeUpdate stream out
     */
    Bytes& operator<<(Bytes& buffer, const SubscribeUpdate& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.start_location; // (Location)  << StartLocation
        payload << msg.end_group; // (i)  << EndGroup
        payload << msg.subscriber_priority; // (8)  << SubscriberPriority
        payload << msg.forward; // (8)  << Forward
        payload << msg.subscribe_parameters; // (..) ... << SubscribeParameters

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kSubscribeUpdate);
        buffer << payload;
        return buffer;
    }

    /*
     * Subscribe stream in constructor
     */
    Subscribe::Subscribe(
            std::function<void (Subscribe&)> group_0_cb,
            std::function<void (Subscribe&)> group_1_cb
        ):
            group_0_cb(group_0_cb),
            group_1_cb(group_1_cb)
    {
    }

    /*
     * Subscribe stream in
     */
    BytesSpan operator>>(BytesSpan buffer, Subscribe& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.track_alias; // (i)  >> TrackAlias
        buffer = buffer >> msg.track_namespace; // (tuple)  >> TrackNamespace
        buffer = buffer >> msg.track_name; // (..)  >> TrackName
        buffer = buffer >> msg.subscriber_priority; // (8)  >> SubscriberPriority
        buffer = buffer >> msg.group_order; // (8)  >> GroupOrder
        buffer = buffer >> msg.forward; // (8)  >> Forward
        buffer = buffer >> msg.filter_type; // (i)  >> FilterType
        if (msg.group_0_cb) { msg.group_0_cb(msg); }
        buffer = buffer >> msg.group_0; // (optional group)  >> Subscribe::Group_0
        if (msg.group_1_cb) { msg.group_1_cb(msg); }
        buffer = buffer >> msg.group_1; // (optional group)  >> Subscribe::Group_1
        buffer = buffer >> msg.subscribe_parameters; // (..) ... >> SubscribeParameters
        return buffer;
    }

    /*
     * Subscribe stream out
     */
    Bytes& operator<<(Bytes& buffer, const Subscribe& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.track_alias; // (i)  << TrackAlias
        payload << msg.track_namespace; // (tuple)  << TrackNamespace
        payload << msg.track_name; // (..)  << TrackName
        payload << msg.subscriber_priority; // (8)  << SubscriberPriority
        payload << msg.group_order; // (8)  << GroupOrder
        payload << msg.forward; // (8)  << Forward
        payload << msg.filter_type; // (i)  << FilterType
        payload << msg.group_0; // (optional group)  << Subscribe::Group_0
        payload << msg.group_1; // (optional group)  << Subscribe::Group_1
        payload << msg.subscribe_parameters; // (..) ... << SubscribeParameters

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kSubscribe);
        buffer << payload;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, std::optional<Subscribe::Group_0>& grp)
    {
        if (grp.has_value()) {
            buffer = buffer >> grp->start_location; // (Location) >> StartLocation
        }
        return buffer;
    }  

    Bytes& operator<<(Bytes& buffer, const std::optional<Subscribe::Group_0>& grp)
    {
        if (grp.has_value()) {
            buffer << grp->start_location; // (Location) << StartLocation
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

    Bytes& operator<<(Bytes& buffer, const std::optional<Subscribe::Group_1>& grp)
    {
        if (grp.has_value()) {
            buffer << grp->end_group; // (i) << EndGroup
        }
        return buffer;
    }
    /*
     * SubscribeOk stream in constructor
     */
    SubscribeOk::SubscribeOk(
            std::function<void (SubscribeOk&)> group_0_cb
        ):
            group_0_cb(group_0_cb)
    {
    }

    /*
     * SubscribeOk stream in
     */
    BytesSpan operator>>(BytesSpan buffer, SubscribeOk& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.expires; // (i)  >> Expires
        buffer = buffer >> msg.group_order; // (8)  >> GroupOrder
        buffer = buffer >> msg.content_exists; // (8)  >> ContentExists
        if (msg.group_0_cb) { msg.group_0_cb(msg); }
        buffer = buffer >> msg.group_0; // (optional group)  >> SubscribeOk::Group_0
        buffer = buffer >> msg.subscribe_parameters; // (..) ... >> SubscribeParameters
        return buffer;
    }

    /*
     * SubscribeOk stream out
     */
    Bytes& operator<<(Bytes& buffer, const SubscribeOk& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
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

    BytesSpan operator>>(BytesSpan buffer, std::optional<SubscribeOk::Group_0>& grp)
    {
        if (grp.has_value()) {
            buffer = buffer >> grp->largest_location; // (Location) >> LargestLocation
        }
        return buffer;
    }  

    Bytes& operator<<(Bytes& buffer, const std::optional<SubscribeOk::Group_0>& grp)
    {
        if (grp.has_value()) {
            buffer << grp->largest_location; // (Location) << LargestLocation
        }
        return buffer;
    }

    /*
     * SubscribeError stream in
     */
    BytesSpan operator>>(BytesSpan buffer, SubscribeError& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.error_code; // (i)  >> SubscribeErrorErrorCode
        buffer = buffer >> msg.error_reason; // (Reason Phrase)  >> ErrorReason
        buffer = buffer >> msg.track_alias; // (i)  >> TrackAlias
        return buffer;
    }

    /*
     * SubscribeError stream out
     */
    Bytes& operator<<(Bytes& buffer, const SubscribeError& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.error_code; // (i)  << SubscribeErrorErrorCode
        payload << msg.error_reason; // (Reason Phrase)  << ErrorReason
        payload << msg.track_alias; // (i)  << TrackAlias

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kSubscribeError);
        buffer << payload;
        return buffer;
    }


    /*
     * Announce stream in
     */
    BytesSpan operator>>(BytesSpan buffer, Announce& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.track_namespace; // (tuple)  >> TrackNamespace
        buffer = buffer >> msg.parameters; // (..) ... >> Parameters
        return buffer;
    }

    /*
     * Announce stream out
     */
    Bytes& operator<<(Bytes& buffer, const Announce& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.track_namespace; // (tuple)  << TrackNamespace
        payload << msg.parameters; // (..) ... << Parameters

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kAnnounce);
        buffer << payload;
        return buffer;
    }


    /*
     * AnnounceOk stream in
     */
    BytesSpan operator>>(BytesSpan buffer, AnnounceOk& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        return buffer;
    }

    /*
     * AnnounceOk stream out
     */
    Bytes& operator<<(Bytes& buffer, const AnnounceOk& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kAnnounceOk);
        buffer << payload;
        return buffer;
    }


    /*
     * AnnounceError stream in
     */
    BytesSpan operator>>(BytesSpan buffer, AnnounceError& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.error_code; // (i)  >> AnnounceErrorErrorCode
        buffer = buffer >> msg.error_reason; // (Reason Phrase)  >> ErrorReason
        return buffer;
    }

    /*
     * AnnounceError stream out
     */
    Bytes& operator<<(Bytes& buffer, const AnnounceError& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.error_code; // (i)  << AnnounceErrorErrorCode
        payload << msg.error_reason; // (Reason Phrase)  << ErrorReason

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kAnnounceError);
        buffer << payload;
        return buffer;
    }


    /*
     * Unannounce stream in
     */
    BytesSpan operator>>(BytesSpan buffer, Unannounce& msg)
    {
        buffer = buffer >> msg.track_namespace; // (tuple)  >> TrackNamespace
        return buffer;
    }

    /*
     * Unannounce stream out
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


    /*
     * Unsubscribe stream in
     */
    BytesSpan operator>>(BytesSpan buffer, Unsubscribe& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        return buffer;
    }

    /*
     * Unsubscribe stream out
     */
    Bytes& operator<<(Bytes& buffer, const Unsubscribe& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kUnsubscribe);
        buffer << payload;
        return buffer;
    }


    /*
     * SubscribeDone stream in
     */
    BytesSpan operator>>(BytesSpan buffer, SubscribeDone& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.status_code; // (i)  >> SubscribeDoneStatusCode
        buffer = buffer >> msg.stream_count; // (i)  >> StreamCount
        buffer = buffer >> msg.error_reason; // (Reason Phrase)  >> ErrorReason
        return buffer;
    }

    /*
     * SubscribeDone stream out
     */
    Bytes& operator<<(Bytes& buffer, const SubscribeDone& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.status_code; // (i)  << SubscribeDoneStatusCode
        payload << msg.stream_count; // (i)  << StreamCount
        payload << msg.error_reason; // (Reason Phrase)  << ErrorReason

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kSubscribeDone);
        buffer << payload;
        return buffer;
    }


    /*
     * AnnounceCancel stream in
     */
    BytesSpan operator>>(BytesSpan buffer, AnnounceCancel& msg)
    {
        buffer = buffer >> msg.track_namespace; // (tuple)  >> TrackNamespace
        buffer = buffer >> msg.error_code; // (i)  >> AnnounceCancelErrorCode
        buffer = buffer >> msg.error_reason; // (Reason Phrase)  >> ErrorReason
        return buffer;
    }

    /*
     * AnnounceCancel stream out
     */
    Bytes& operator<<(Bytes& buffer, const AnnounceCancel& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.track_namespace; // (tuple)  << TrackNamespace
        payload << msg.error_code; // (i)  << AnnounceCancelErrorCode
        payload << msg.error_reason; // (Reason Phrase)  << ErrorReason

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kAnnounceCancel);
        buffer << payload;
        return buffer;
    }


    /*
     * TrackStatusRequest stream in
     */
    BytesSpan operator>>(BytesSpan buffer, TrackStatusRequest& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.track_namespace; // (tuple)  >> TrackNamespace
        buffer = buffer >> msg.track_name; // (..)  >> TrackName
        buffer = buffer >> msg.parameters; // (..) ... >> Parameters
        return buffer;
    }

    /*
     * TrackStatusRequest stream out
     */
    Bytes& operator<<(Bytes& buffer, const TrackStatusRequest& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.track_namespace; // (tuple)  << TrackNamespace
        payload << msg.track_name; // (..)  << TrackName
        payload << msg.parameters; // (..) ... << Parameters

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kTrackStatusRequest);
        buffer << payload;
        return buffer;
    }


    /*
     * TrackStatus stream in
     */
    BytesSpan operator>>(BytesSpan buffer, TrackStatus& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.status_code; // (i)  >> StatusCode
        buffer = buffer >> msg.largest_location; // (Location)  >> LargestLocation
        buffer = buffer >> msg.parameters; // (..) ... >> Parameters
        return buffer;
    }

    /*
     * TrackStatus stream out
     */
    Bytes& operator<<(Bytes& buffer, const TrackStatus& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.status_code; // (i)  << StatusCode
        payload << msg.largest_location; // (Location)  << LargestLocation
        payload << msg.parameters; // (..) ... << Parameters

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kTrackStatus);
        buffer << payload;
        return buffer;
    }


    /*
     * Goaway stream in
     */
    BytesSpan operator>>(BytesSpan buffer, Goaway& msg)
    {
        buffer = buffer >> msg.new_session_uri; // (..)  >> NewSessionURI
        return buffer;
    }

    /*
     * Goaway stream out
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


    /*
     * SubscribeAnnounces stream in
     */
    BytesSpan operator>>(BytesSpan buffer, SubscribeAnnounces& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.track_namespace_prefix; // (tuple)  >> TrackNamespacePrefix
        buffer = buffer >> msg.parameters; // (..) ... >> Parameters
        return buffer;
    }

    /*
     * SubscribeAnnounces stream out
     */
    Bytes& operator<<(Bytes& buffer, const SubscribeAnnounces& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.track_namespace_prefix; // (tuple)  << TrackNamespacePrefix
        payload << msg.parameters; // (..) ... << Parameters

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kSubscribeAnnounces);
        buffer << payload;
        return buffer;
    }


    /*
     * SubscribeAnnouncesOk stream in
     */
    BytesSpan operator>>(BytesSpan buffer, SubscribeAnnouncesOk& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        return buffer;
    }

    /*
     * SubscribeAnnouncesOk stream out
     */
    Bytes& operator<<(Bytes& buffer, const SubscribeAnnouncesOk& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kSubscribeAnnouncesOk);
        buffer << payload;
        return buffer;
    }


    /*
     * SubscribeAnnouncesError stream in
     */
    BytesSpan operator>>(BytesSpan buffer, SubscribeAnnouncesError& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.error_code; // (i)  >> SubscribeAnnouncesErrorErrorCode
        buffer = buffer >> msg.error_reason; // (Reason Phrase)  >> ErrorReason
        return buffer;
    }

    /*
     * SubscribeAnnouncesError stream out
     */
    Bytes& operator<<(Bytes& buffer, const SubscribeAnnouncesError& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.error_code; // (i)  << SubscribeAnnouncesErrorErrorCode
        payload << msg.error_reason; // (Reason Phrase)  << ErrorReason

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kSubscribeAnnouncesError);
        buffer << payload;
        return buffer;
    }


    /*
     * UnsubscribeAnnounces stream in
     */
    BytesSpan operator>>(BytesSpan buffer, UnsubscribeAnnounces& msg)
    {
        buffer = buffer >> msg.track_namespace_prefix; // (tuple)  >> TrackNamespacePrefix
        return buffer;
    }

    /*
     * UnsubscribeAnnounces stream out
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


    /*
     * MaxRequestId stream in
     */
    BytesSpan operator>>(BytesSpan buffer, MaxRequestId& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        return buffer;
    }

    /*
     * MaxRequestId stream out
     */
    Bytes& operator<<(Bytes& buffer, const MaxRequestId& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kMaxRequestId);
        buffer << payload;
        return buffer;
    }

    /*
     * Fetch stream in constructor
     */
    Fetch::Fetch(
            std::function<void (Fetch&)> group_0_cb,
            std::function<void (Fetch&)> group_1_cb
        ):
            group_0_cb(group_0_cb),
            group_1_cb(group_1_cb)
    {
    }

    /*
     * Fetch stream in
     */
    BytesSpan operator>>(BytesSpan buffer, Fetch& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.subscriber_priority; // (8)  >> SubscriberPriority
        buffer = buffer >> msg.group_order; // (8)  >> GroupOrder
        buffer = buffer >> msg.fetch_type; // (i)  >> FetchType
        if (msg.group_0_cb) { msg.group_0_cb(msg); }
        buffer = buffer >> msg.group_0; // (optional group)  >> Fetch::Group_0
        if (msg.group_1_cb) { msg.group_1_cb(msg); }
        buffer = buffer >> msg.group_1; // (optional group)  >> Fetch::Group_1
        buffer = buffer >> msg.parameters; // (..) ... >> Parameters
        return buffer;
    }

    /*
     * Fetch stream out
     */
    Bytes& operator<<(Bytes& buffer, const Fetch& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
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
    BytesSpan operator>>(BytesSpan buffer, std::optional<Fetch::Group_1>& grp)
    {
        if (grp.has_value()) {
            buffer = buffer >> grp->joining_subscribe_id; // (i) >> JoiningSubscribeID
            buffer = buffer >> grp->joining_start; // (i) >> JoiningStart
        }
        return buffer;
    }  

    Bytes& operator<<(Bytes& buffer, const std::optional<Fetch::Group_1>& grp)
    {
        if (grp.has_value()) {
            buffer << grp->joining_subscribe_id; // (i) << JoiningSubscribeID
            buffer << grp->joining_start; // (i) << JoiningStart
        }
        return buffer;
    }

    /*
     * FetchCancel stream in
     */
    BytesSpan operator>>(BytesSpan buffer, FetchCancel& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        return buffer;
    }

    /*
     * FetchCancel stream out
     */
    Bytes& operator<<(Bytes& buffer, const FetchCancel& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kFetchCancel);
        buffer << payload;
        return buffer;
    }


    /*
     * FetchOk stream in
     */
    BytesSpan operator>>(BytesSpan buffer, FetchOk& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.group_order; // (8)  >> GroupOrder
        buffer = buffer >> msg.end_of_track; // (8)  >> EndOfTrack
        buffer = buffer >> msg.end_location; // (Location)  >> EndLocation
        buffer = buffer >> msg.subscribe_parameters; // (..) ... >> SubscribeParameters
        return buffer;
    }

    /*
     * FetchOk stream out
     */
    Bytes& operator<<(Bytes& buffer, const FetchOk& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.group_order; // (8)  << GroupOrder
        payload << msg.end_of_track; // (8)  << EndOfTrack
        payload << msg.end_location; // (Location)  << EndLocation
        payload << msg.subscribe_parameters; // (..) ... << SubscribeParameters

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kFetchOk);
        buffer << payload;
        return buffer;
    }


    /*
     * FetchError stream in
     */
    BytesSpan operator>>(BytesSpan buffer, FetchError& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.error_code; // (i)  >> FetchErrorErrorCode
        buffer = buffer >> msg.error_reason; // (Reason Phrase)  >> ErrorReason
        return buffer;
    }

    /*
     * FetchError stream out
     */
    Bytes& operator<<(Bytes& buffer, const FetchError& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.error_code; // (i)  << FetchErrorErrorCode
        payload << msg.error_reason; // (Reason Phrase)  << ErrorReason

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kFetchError);
        buffer << payload;
        return buffer;
    }


    /*
     * RequestsBlocked stream in
     */
    BytesSpan operator>>(BytesSpan buffer, RequestsBlocked& msg)
    {
        buffer = buffer >> msg.maximum_request_id; // (i)  >> MaximumRequestID
        return buffer;
    }

    /*
     * RequestsBlocked stream out
     */
    Bytes& operator<<(Bytes& buffer, const RequestsBlocked& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.maximum_request_id; // (i)  << MaximumRequestID

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kRequestsBlocked);
        buffer << payload;
        return buffer;
    }


    /*
     * ClientSetup stream in
     */
    BytesSpan operator>>(BytesSpan buffer, ClientSetup& msg)
    {
        buffer = buffer >> msg.supported_versions; // (i) ... >> SupportedVersions
        buffer = buffer >> msg.setup_parameters; // (..) ... >> SetupParameters
        return buffer;
    }

    /*
     * ClientSetup stream out
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


    /*
     * ServerSetup stream in
     */
    BytesSpan operator>>(BytesSpan buffer, ServerSetup& msg)
    {
        buffer = buffer >> msg.selected_version; // (i)  >> SelectedVersion
        buffer = buffer >> msg.setup_parameters; // (..) ... >> SetupParameters
        return buffer;
    }

    /*
     * ServerSetup stream out
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


    /*
     * NewGroupRequest stream in
     */
    BytesSpan operator>>(BytesSpan buffer, NewGroupRequest& msg)
    {
        buffer = buffer >> msg.subscribe_id; // (i)  >> SubscribeID
        buffer = buffer >> msg.track_alias; // (i)  >> TrackAlias
        return buffer;
    }

    /*
     * NewGroupRequest stream out
     */
    Bytes& operator<<(Bytes& buffer, const NewGroupRequest& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.subscribe_id; // (i)  << SubscribeID
        payload << msg.track_alias; // (i)  << TrackAlias

        // fill out buffer
        buffer << static_cast<std::uint64_t>(ControlMessageType::kNewGroupRequest);
        buffer << payload;
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

} // namespace

