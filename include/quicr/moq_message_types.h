#pragma once

// TODO(Suhas): This needs to merge into one file.
// Hacking it here for hacking purpose


#include <string>
#include <vector>

#include <quicr/uvarint.h>
#include <quicr/quicr_common.h>
#include <quicr/message_buffer.h>

namespace quicr::messages {

    using TrackNamespace = std::string;
    using FullTrackName = std::string;
    using TrackId = uintVar_t;
    using ErrorCode = uintVar_t;
    using ReasonPhrase = std::string;
    using GroupSequence = uintVar_t;
    using ObjectSequence = uintVar_t;
    using ObjectPriority = uintVar_t;

    constexpr u_int8_t MESSAGE_TYPE_SUBSCRIBE       = 0x06;
    constexpr u_int8_t MESSAGE_TYPE_SUBSCRIBE_OK    = 0x07;
    constexpr u_int8_t MESSAGE_TYPE_SUBSCRIBE_ERROR = 0x08;
    constexpr u_int8_t MESSAGE_TYPE_UNSUBSCRIBE     = 0x09;
    constexpr u_int8_t MESSAGE_TYPE_ANNOUNCE        = 0x10;
    constexpr u_int8_t MESSAGE_TYPE_ANNOUNCE_OK     = 0x06;
    constexpr u_int8_t MESSAGE_TYPE_ANNOUNCE_ERROR  = 0x06;
    constexpr u_int8_t MESSAGE_TYPE_UNANNOUNCE      = 0x06;
    constexpr u_int8_t MESSAGE_TYPE_OBJECT          = 0x06;



    enum SubscriptionHint : uint8_t {
        Latest = 0,
        Waitup,
        Catchup
    };


    enum ForwardingPreference : uint8_t {
        StreamPerGroup = 0,
        StreamPerObject,
        StreamPerPriority,
        StreamPerTrack,
        Datagram
    };

    struct TrackRequestParameters {

    };

    struct Setup {

    };

    struct MoqSubscribe {
        FullTrackName track;
        SubscriptionHint hint;
    };


    struct MoqSubscribeOk {
        FullTrackName track;
        TrackId track_id;
    };

    struct MoqSubscribeError {
        FullTrackName track;
        ErrorCode err_code;
        ReasonPhrase reason_phrase;
    };

    struct MoqUnsubscribe {
        FullTrackName track;
    };

    struct MoqAnnounce {
        TrackNamespace track_namespace;

    };

    struct MoqAnnounceOk {
        TrackNamespace track_namespace;
    };

    struct MoqAnnounceError {
        TrackNamespace track_namespace;
        ErrorCode err_code;
        ReasonPhrase reason_phrase;
    };

    struct MoqUnannounce {
        TrackNamespace track_namespace;
    };

    struct MoqGoaway {

    };

    struct MoqObject {
        TrackId track_id;
        GroupSequence group_sequence;
        ObjectSequence object_sequence;
        ObjectPriority priority;
        ForwardingPreference forwarding_preference;
        quicr::bytes payload;
    };

    MessageBuffer&
    operator<<(MessageBuffer& buffer, const MoqAnnounce& msg);

    MessageBuffer& operator>>(MessageBuffer &buffer, MoqAnnounce &msg);

    MessageBuffer &operator<<(MessageBuffer &buffer, const MoqUnannounce &msg);

    MessageBuffer &operator>>(MessageBuffer &buffer, MoqUnannounce &msg);

    MessageBuffer &operator<<(MessageBuffer &buffer, const MoqSubscribe &msg);

    MessageBuffer &operator>>(MessageBuffer &buffer, MoqSubscribe &msg);

    MessageBuffer& operator<<(MessageBuffer &buffer, const MoqUnsubscribe &msg);

    MessageBuffer& operator>>(MessageBuffer &buffer, MoqUnsubscribe &msg);

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqSubscribeOk &msg);

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqSubscribeOk &msg);
}