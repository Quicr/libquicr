// Generated from: draft-ietf-moq-transport-14_edited.txt

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
    Bytes& operator<<(Bytes& buffer, const std::vector<quicr::messages::SetupParameter>& vec)
    {
        // write vector size
        buffer << static_cast<std::uint64_t>(vec.size());

        // write elements of vector
        for (const auto& item : vec) {
            buffer << item;
        }
        
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, std::vector<quicr::messages::SetupParameter>& vec)
    {
        std::uint64_t size = 0;
        buffer = buffer >> size;

        for (uint64_t i=0; i<size; i++) {
            quicr::messages::SetupParameter item;    
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
        buffer = buffer >> msg.subscription_request_id; // (i)  >> SubscriptionRequestID
        buffer = buffer >> msg.start_location; // (Location)  >> StartLocation
        buffer = buffer >> msg.end_group; // (i)  >> EndGroup
        buffer = buffer >> msg.subscriber_priority; // (8)  >> SubscriberPriority
        buffer = buffer >> msg.forward; // (8)  >> Forward
        buffer = buffer >> msg.parameters; // (..) ... >> Parameters
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
        payload << msg.subscription_request_id; // (i)  << SubscriptionRequestID
        payload << msg.start_location; // (Location)  << StartLocation
        payload << msg.end_group; // (i)  << EndGroup
        payload << msg.subscriber_priority; // (8)  << SubscriberPriority
        payload << msg.forward; // (8)  << Forward
        payload << msg.parameters; // (..) ... << Parameters

        // fill out buffer
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kSubscribeUpdate);
        message.payload = payload;
        buffer << message;
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
        if (group_0_cb == nullptr) {
            throw std::invalid_argument("Callbacks must be specified");
        }
        if (group_1_cb == nullptr) {
            throw std::invalid_argument("Callbacks must be specified");
        }
    }


    /*
     * Subscribe stream in
     */
    BytesSpan operator>>(BytesSpan buffer, Subscribe& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
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
        buffer = buffer >> msg.parameters; // (..) ... >> Parameters
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
        payload << msg.track_namespace; // (tuple)  << TrackNamespace
        payload << msg.track_name; // (..)  << TrackName
        payload << msg.subscriber_priority; // (8)  << SubscriberPriority
        payload << msg.group_order; // (8)  << GroupOrder
        payload << msg.forward; // (8)  << Forward
        payload << msg.filter_type; // (i)  << FilterType
        payload << msg.group_0; // (optional group)  << Subscribe::Group_0
        payload << msg.group_1; // (optional group)  << Subscribe::Group_1
        payload << msg.parameters; // (..) ... << Parameters

        // fill out buffer
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kSubscribe);
        message.payload = payload;
        buffer << message;
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
        if (group_0_cb == nullptr) {
            throw std::invalid_argument("Callbacks must be specified");
        }
    }


    /*
     * SubscribeOk stream in
     */
    BytesSpan operator>>(BytesSpan buffer, SubscribeOk& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.track_alias; // (i)  >> TrackAlias
        buffer = buffer >> msg.expires; // (i)  >> Expires
        buffer = buffer >> msg.group_order; // (8)  >> GroupOrder
        buffer = buffer >> msg.content_exists; // (8)  >> ContentExists
        if (msg.group_0_cb) { msg.group_0_cb(msg); }
        buffer = buffer >> msg.group_0; // (optional group)  >> SubscribeOk::Group_0
        buffer = buffer >> msg.parameters; // (..) ... >> Parameters
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
        payload << msg.track_alias; // (i)  << TrackAlias
        payload << msg.expires; // (i)  << Expires
        payload << msg.group_order; // (8)  << GroupOrder
        payload << msg.content_exists; // (8)  << ContentExists
        payload << msg.group_0; // (optional group)  << SubscribeOk::Group_0
        payload << msg.parameters; // (..) ... << Parameters

        // fill out buffer
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kSubscribeOk);
        message.payload = payload;
        buffer << message;
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

        // fill out buffer
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kSubscribeError);
        message.payload = payload;
        buffer << message;
        return buffer;
    }


    /*
     * PublishNamespace stream in
     */
    BytesSpan operator>>(BytesSpan buffer, PublishNamespace& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.track_namespace; // (tuple)  >> TrackNamespace
        buffer = buffer >> msg.parameters; // (..) ... >> Parameters
        return buffer;
    }

    /*
     * PublishNamespace stream out
     */
    Bytes& operator<<(Bytes& buffer, const PublishNamespace& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.track_namespace; // (tuple)  << TrackNamespace
        payload << msg.parameters; // (..) ... << Parameters

        // fill out buffer
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kPublishNamespace);
        message.payload = payload;
        buffer << message;
        return buffer;
    }


    /*
     * PublishNamespaceOk stream in
     */
    BytesSpan operator>>(BytesSpan buffer, PublishNamespaceOk& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        return buffer;
    }

    /*
     * PublishNamespaceOk stream out
     */
    Bytes& operator<<(Bytes& buffer, const PublishNamespaceOk& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID

        // fill out buffer
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kPublishNamespaceOk);
        message.payload = payload;
        buffer << message;
        return buffer;
    }


    /*
     * PublishNamespaceError stream in
     */
    BytesSpan operator>>(BytesSpan buffer, PublishNamespaceError& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.error_code; // (i)  >> PublishNamespaceErrorErrorCode
        buffer = buffer >> msg.error_reason; // (Reason Phrase)  >> ErrorReason
        return buffer;
    }

    /*
     * PublishNamespaceError stream out
     */
    Bytes& operator<<(Bytes& buffer, const PublishNamespaceError& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.error_code; // (i)  << PublishNamespaceErrorErrorCode
        payload << msg.error_reason; // (Reason Phrase)  << ErrorReason

        // fill out buffer
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kPublishNamespaceError);
        message.payload = payload;
        buffer << message;
        return buffer;
    }


    /*
     * PublishNamespaceDone stream in
     */
    BytesSpan operator>>(BytesSpan buffer, PublishNamespaceDone& msg)
    {
        buffer = buffer >> msg.track_namespace; // (tuple)  >> TrackNamespace
        return buffer;
    }

    /*
     * PublishNamespaceDone stream out
     */
    Bytes& operator<<(Bytes& buffer, const PublishNamespaceDone& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.track_namespace; // (tuple)  << TrackNamespace

        // fill out buffer
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kPublishNamespaceDone);
        message.payload = payload;
        buffer << message;
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
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kUnsubscribe);
        message.payload = payload;
        buffer << message;
        return buffer;
    }


    /*
     * PublishDone stream in
     */
    BytesSpan operator>>(BytesSpan buffer, PublishDone& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.status_code; // (i)  >> PublishDoneStatusCode
        buffer = buffer >> msg.stream_count; // (i)  >> StreamCount
        buffer = buffer >> msg.error_reason; // (Reason Phrase)  >> ErrorReason
        return buffer;
    }

    /*
     * PublishDone stream out
     */
    Bytes& operator<<(Bytes& buffer, const PublishDone& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.status_code; // (i)  << PublishDoneStatusCode
        payload << msg.stream_count; // (i)  << StreamCount
        payload << msg.error_reason; // (Reason Phrase)  << ErrorReason

        // fill out buffer
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kPublishDone);
        message.payload = payload;
        buffer << message;
        return buffer;
    }


    /*
     * PublishNamespaceCancel stream in
     */
    BytesSpan operator>>(BytesSpan buffer, PublishNamespaceCancel& msg)
    {
        buffer = buffer >> msg.track_namespace; // (tuple)  >> TrackNamespace
        buffer = buffer >> msg.error_code; // (i)  >> ErrorCode
        buffer = buffer >> msg.error_reason; // (Reason Phrase)  >> ErrorReason
        return buffer;
    }

    /*
     * PublishNamespaceCancel stream out
     */
    Bytes& operator<<(Bytes& buffer, const PublishNamespaceCancel& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.track_namespace; // (tuple)  << TrackNamespace
        payload << msg.error_code; // (i)  << ErrorCode
        payload << msg.error_reason; // (Reason Phrase)  << ErrorReason

        // fill out buffer
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kPublishNamespaceCancel);
        message.payload = payload;
        buffer << message;
        return buffer;
    }

    /*
     * TrackStatus stream in constructor
     */
    TrackStatus::TrackStatus(
            std::function<void (TrackStatus&)> group_0_cb,
            std::function<void (TrackStatus&)> group_1_cb
        ):
            group_0_cb(group_0_cb),
            group_1_cb(group_1_cb)
    {
        if (group_0_cb == nullptr) {
            throw std::invalid_argument("Callbacks must be specified");
        }
        if (group_1_cb == nullptr) {
            throw std::invalid_argument("Callbacks must be specified");
        }
    }


    /*
     * TrackStatus stream in
     */
    BytesSpan operator>>(BytesSpan buffer, TrackStatus& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.track_namespace; // (tuple)  >> TrackNamespace
        buffer = buffer >> msg.track_name; // (..)  >> TrackName
        buffer = buffer >> msg.subscriber_priority; // (8)  >> SubscriberPriority
        buffer = buffer >> msg.group_order; // (8)  >> GroupOrder
        buffer = buffer >> msg.forward; // (8)  >> Forward
        buffer = buffer >> msg.filter_type; // (i)  >> FilterType
        if (msg.group_0_cb) { msg.group_0_cb(msg); }
        buffer = buffer >> msg.group_0; // (optional group)  >> TrackStatus::Group_0
        if (msg.group_1_cb) { msg.group_1_cb(msg); }
        buffer = buffer >> msg.group_1; // (optional group)  >> TrackStatus::Group_1
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
        payload << msg.track_namespace; // (tuple)  << TrackNamespace
        payload << msg.track_name; // (..)  << TrackName
        payload << msg.subscriber_priority; // (8)  << SubscriberPriority
        payload << msg.group_order; // (8)  << GroupOrder
        payload << msg.forward; // (8)  << Forward
        payload << msg.filter_type; // (i)  << FilterType
        payload << msg.group_0; // (optional group)  << TrackStatus::Group_0
        payload << msg.group_1; // (optional group)  << TrackStatus::Group_1
        payload << msg.parameters; // (..) ... << Parameters

        // fill out buffer
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kTrackStatus);
        message.payload = payload;
        buffer << message;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, std::optional<TrackStatus::Group_0>& grp)
    {
        if (grp.has_value()) {
            buffer = buffer >> grp->start_location; // (Location) >> StartLocation
        }
        return buffer;
    }  

    Bytes& operator<<(Bytes& buffer, const std::optional<TrackStatus::Group_0>& grp)
    {
        if (grp.has_value()) {
            buffer << grp->start_location; // (Location) << StartLocation
        }
        return buffer;
    }
    BytesSpan operator>>(BytesSpan buffer, std::optional<TrackStatus::Group_1>& grp)
    {
        if (grp.has_value()) {
            buffer = buffer >> grp->end_group; // (i) >> EndGroup
        }
        return buffer;
    }  

    Bytes& operator<<(Bytes& buffer, const std::optional<TrackStatus::Group_1>& grp)
    {
        if (grp.has_value()) {
            buffer << grp->end_group; // (i) << EndGroup
        }
        return buffer;
    }
    /*
     * TrackStatusOk stream in constructor
     */
    TrackStatusOk::TrackStatusOk(
            std::function<void (TrackStatusOk&)> group_0_cb
        ):
            group_0_cb(group_0_cb)
    {
        if (group_0_cb == nullptr) {
            throw std::invalid_argument("Callbacks must be specified");
        }
    }


    /*
     * TrackStatusOk stream in
     */
    BytesSpan operator>>(BytesSpan buffer, TrackStatusOk& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.track_alias; // (i)  >> TrackAlias
        buffer = buffer >> msg.expires; // (i)  >> Expires
        buffer = buffer >> msg.group_order; // (8)  >> GroupOrder
        buffer = buffer >> msg.content_exists; // (8)  >> ContentExists
        if (msg.group_0_cb) { msg.group_0_cb(msg); }
        buffer = buffer >> msg.group_0; // (optional group)  >> TrackStatusOk::Group_0
        buffer = buffer >> msg.parameters; // (..) ... >> Parameters
        return buffer;
    }

    /*
     * TrackStatusOk stream out
     */
    Bytes& operator<<(Bytes& buffer, const TrackStatusOk& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.track_alias; // (i)  << TrackAlias
        payload << msg.expires; // (i)  << Expires
        payload << msg.group_order; // (8)  << GroupOrder
        payload << msg.content_exists; // (8)  << ContentExists
        payload << msg.group_0; // (optional group)  << TrackStatusOk::Group_0
        payload << msg.parameters; // (..) ... << Parameters

        // fill out buffer
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kTrackStatusOk);
        message.payload = payload;
        buffer << message;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, std::optional<TrackStatusOk::Group_0>& grp)
    {
        if (grp.has_value()) {
            buffer = buffer >> grp->largest_location; // (Location) >> LargestLocation
        }
        return buffer;
    }  

    Bytes& operator<<(Bytes& buffer, const std::optional<TrackStatusOk::Group_0>& grp)
    {
        if (grp.has_value()) {
            buffer << grp->largest_location; // (Location) << LargestLocation
        }
        return buffer;
    }

    /*
     * TrackStatusError stream in
     */
    BytesSpan operator>>(BytesSpan buffer, TrackStatusError& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.error_code; // (i)  >> TrackStatusErrorErrorCode
        buffer = buffer >> msg.error_reason; // (Reason Phrase)  >> ErrorReason
        return buffer;
    }

    /*
     * TrackStatusError stream out
     */
    Bytes& operator<<(Bytes& buffer, const TrackStatusError& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.error_code; // (i)  << TrackStatusErrorErrorCode
        payload << msg.error_reason; // (Reason Phrase)  << ErrorReason

        // fill out buffer
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kTrackStatusError);
        message.payload = payload;
        buffer << message;
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
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kGoaway);
        message.payload = payload;
        buffer << message;
        return buffer;
    }


    /*
     * SubscribeNamespace stream in
     */
    BytesSpan operator>>(BytesSpan buffer, SubscribeNamespace& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.track_namespace_prefix; // (tuple)  >> TrackNamespacePrefix
        buffer = buffer >> msg.parameters; // (..) ... >> Parameters
        return buffer;
    }

    /*
     * SubscribeNamespace stream out
     */
    Bytes& operator<<(Bytes& buffer, const SubscribeNamespace& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.track_namespace_prefix; // (tuple)  << TrackNamespacePrefix
        payload << msg.parameters; // (..) ... << Parameters

        // fill out buffer
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kSubscribeNamespace);
        message.payload = payload;
        buffer << message;
        return buffer;
    }


    /*
     * SubscribeNamespaceOk stream in
     */
    BytesSpan operator>>(BytesSpan buffer, SubscribeNamespaceOk& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        return buffer;
    }

    /*
     * SubscribeNamespaceOk stream out
     */
    Bytes& operator<<(Bytes& buffer, const SubscribeNamespaceOk& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID

        // fill out buffer
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kSubscribeNamespaceOk);
        message.payload = payload;
        buffer << message;
        return buffer;
    }


    /*
     * SubscribeNamespaceError stream in
     */
    BytesSpan operator>>(BytesSpan buffer, SubscribeNamespaceError& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.error_code; // (i)  >> SubscribeNamespaceErrorErrorCode
        buffer = buffer >> msg.error_reason; // (Reason Phrase)  >> ErrorReason
        return buffer;
    }

    /*
     * SubscribeNamespaceError stream out
     */
    Bytes& operator<<(Bytes& buffer, const SubscribeNamespaceError& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.error_code; // (i)  << SubscribeNamespaceErrorErrorCode
        payload << msg.error_reason; // (Reason Phrase)  << ErrorReason

        // fill out buffer
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kSubscribeNamespaceError);
        message.payload = payload;
        buffer << message;
        return buffer;
    }


    /*
     * UnsubscribeNamespace stream in
     */
    BytesSpan operator>>(BytesSpan buffer, UnsubscribeNamespace& msg)
    {
        buffer = buffer >> msg.track_namespace_prefix; // (tuple)  >> TrackNamespacePrefix
        return buffer;
    }

    /*
     * UnsubscribeNamespace stream out
     */
    Bytes& operator<<(Bytes& buffer, const UnsubscribeNamespace& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.track_namespace_prefix; // (tuple)  << TrackNamespacePrefix

        // fill out buffer
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kUnsubscribeNamespace);
        message.payload = payload;
        buffer << message;
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
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kMaxRequestId);
        message.payload = payload;
        buffer << message;
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
        if (group_0_cb == nullptr) {
            throw std::invalid_argument("Callbacks must be specified");
        }
        if (group_1_cb == nullptr) {
            throw std::invalid_argument("Callbacks must be specified");
        }
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
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kFetch);
        message.payload = payload;
        buffer << message;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, std::optional<Fetch::Group_0>& grp)
    {
        if (grp.has_value()) {
            buffer = buffer >> grp->standalone; // (Standalone Fetch) >> Standalone
        }
        return buffer;
    }  

    Bytes& operator<<(Bytes& buffer, const std::optional<Fetch::Group_0>& grp)
    {
        if (grp.has_value()) {
            buffer << grp->standalone; // (Standalone Fetch) << Standalone
        }
        return buffer;
    }
    BytesSpan operator>>(BytesSpan buffer, std::optional<Fetch::Group_1>& grp)
    {
        if (grp.has_value()) {
            buffer = buffer >> grp->joining; // (Joining Fetch) >> Joining
        }
        return buffer;
    }  

    Bytes& operator<<(Bytes& buffer, const std::optional<Fetch::Group_1>& grp)
    {
        if (grp.has_value()) {
            buffer << grp->joining; // (Joining Fetch) << Joining
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
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kFetchCancel);
        message.payload = payload;
        buffer << message;
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
        buffer = buffer >> msg.parameters; // (..) ... >> Parameters
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
        payload << msg.parameters; // (..) ... << Parameters

        // fill out buffer
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kFetchOk);
        message.payload = payload;
        buffer << message;
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
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kFetchError);
        message.payload = payload;
        buffer << message;
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
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kRequestsBlocked);
        message.payload = payload;
        buffer << message;
        return buffer;
    }

    /*
     * Publish stream in constructor
     */
    Publish::Publish(
            std::function<void (Publish&)> group_0_cb
        ):
            group_0_cb(group_0_cb)
    {
        if (group_0_cb == nullptr) {
            throw std::invalid_argument("Callbacks must be specified");
        }
    }


    /*
     * Publish stream in
     */
    BytesSpan operator>>(BytesSpan buffer, Publish& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.track_namespace; // (tuple)  >> TrackNamespace
        buffer = buffer >> msg.track_name; // (..)  >> TrackName
        buffer = buffer >> msg.track_alias; // (i)  >> TrackAlias
        buffer = buffer >> msg.group_order; // (8)  >> GroupOrder
        buffer = buffer >> msg.content_exists; // (8)  >> ContentExists
        if (msg.group_0_cb) { msg.group_0_cb(msg); }
        buffer = buffer >> msg.group_0; // (optional group)  >> Publish::Group_0
        buffer = buffer >> msg.forward; // (8)  >> Forward
        buffer = buffer >> msg.parameters; // (..) ... >> Parameters
        return buffer;
    }

    /*
     * Publish stream out
     */
    Bytes& operator<<(Bytes& buffer, const Publish& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.track_namespace; // (tuple)  << TrackNamespace
        payload << msg.track_name; // (..)  << TrackName
        payload << msg.track_alias; // (i)  << TrackAlias
        payload << msg.group_order; // (8)  << GroupOrder
        payload << msg.content_exists; // (8)  << ContentExists
        payload << msg.group_0; // (optional group)  << Publish::Group_0
        payload << msg.forward; // (8)  << Forward
        payload << msg.parameters; // (..) ... << Parameters

        // fill out buffer
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kPublish);
        message.payload = payload;
        buffer << message;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, std::optional<Publish::Group_0>& grp)
    {
        if (grp.has_value()) {
            buffer = buffer >> grp->largest_location; // (Location) >> LargestLocation
        }
        return buffer;
    }  

    Bytes& operator<<(Bytes& buffer, const std::optional<Publish::Group_0>& grp)
    {
        if (grp.has_value()) {
            buffer << grp->largest_location; // (Location) << LargestLocation
        }
        return buffer;
    }
    /*
     * PublishOk stream in constructor
     */
    PublishOk::PublishOk(
            std::function<void (PublishOk&)> group_0_cb,
            std::function<void (PublishOk&)> group_1_cb
        ):
            group_0_cb(group_0_cb),
            group_1_cb(group_1_cb)
    {
        if (group_0_cb == nullptr) {
            throw std::invalid_argument("Callbacks must be specified");
        }
        if (group_1_cb == nullptr) {
            throw std::invalid_argument("Callbacks must be specified");
        }
    }


    /*
     * PublishOk stream in
     */
    BytesSpan operator>>(BytesSpan buffer, PublishOk& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.forward; // (8)  >> Forward
        buffer = buffer >> msg.subscriber_priority; // (8)  >> SubscriberPriority
        buffer = buffer >> msg.group_order; // (8)  >> GroupOrder
        buffer = buffer >> msg.filter_type; // (i)  >> FilterType
        if (msg.group_0_cb) { msg.group_0_cb(msg); }
        buffer = buffer >> msg.group_0; // (optional group)  >> PublishOk::Group_0
        if (msg.group_1_cb) { msg.group_1_cb(msg); }
        buffer = buffer >> msg.group_1; // (optional group)  >> PublishOk::Group_1
        buffer = buffer >> msg.parameters; // (..) ... >> Parameters
        return buffer;
    }

    /*
     * PublishOk stream out
     */
    Bytes& operator<<(Bytes& buffer, const PublishOk& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.forward; // (8)  << Forward
        payload << msg.subscriber_priority; // (8)  << SubscriberPriority
        payload << msg.group_order; // (8)  << GroupOrder
        payload << msg.filter_type; // (i)  << FilterType
        payload << msg.group_0; // (optional group)  << PublishOk::Group_0
        payload << msg.group_1; // (optional group)  << PublishOk::Group_1
        payload << msg.parameters; // (..) ... << Parameters

        // fill out buffer
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kPublishOk);
        message.payload = payload;
        buffer << message;
        return buffer;
    }

    BytesSpan operator>>(BytesSpan buffer, std::optional<PublishOk::Group_0>& grp)
    {
        if (grp.has_value()) {
            buffer = buffer >> grp->start_location; // (Location) >> StartLocation
        }
        return buffer;
    }  

    Bytes& operator<<(Bytes& buffer, const std::optional<PublishOk::Group_0>& grp)
    {
        if (grp.has_value()) {
            buffer << grp->start_location; // (Location) << StartLocation
        }
        return buffer;
    }
    BytesSpan operator>>(BytesSpan buffer, std::optional<PublishOk::Group_1>& grp)
    {
        if (grp.has_value()) {
            buffer = buffer >> grp->end_group; // (i) >> EndGroup
        }
        return buffer;
    }  

    Bytes& operator<<(Bytes& buffer, const std::optional<PublishOk::Group_1>& grp)
    {
        if (grp.has_value()) {
            buffer << grp->end_group; // (i) << EndGroup
        }
        return buffer;
    }

    /*
     * PublishError stream in
     */
    BytesSpan operator>>(BytesSpan buffer, PublishError& msg)
    {
        buffer = buffer >> msg.request_id; // (i)  >> RequestID
        buffer = buffer >> msg.error_code; // (i)  >> ErrorCode
        buffer = buffer >> msg.error_reason; // (Reason Phrase)  >> ErrorReason
        return buffer;
    }

    /*
     * PublishError stream out
     */
    Bytes& operator<<(Bytes& buffer, const PublishError& msg)
    {
        Bytes payload;
        // fill out payload
        payload << msg.request_id; // (i)  << RequestID
        payload << msg.error_code; // (i)  << ErrorCode
        payload << msg.error_reason; // (Reason Phrase)  << ErrorReason

        // fill out buffer
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kPublishError);
        message.payload = payload;
        buffer << message;
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
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kClientSetup);
        message.payload = payload;
        buffer << message;
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
        ControlMessage message;
        message.type = static_cast<std::uint64_t>(ControlMessageType::kServerSetup);
        message.payload = payload;
        buffer << message;
        return buffer;
    }


   
    Bytes &operator<<(Bytes &buffer, ControlMessageType message_type)
    {
        UintVar varint = static_cast<std::uint64_t>(message_type);
        buffer << varint;
        return buffer;
    }

} // namespace

