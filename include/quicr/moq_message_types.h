#pragma once

// TODO(Suhas): This needs to merge into one file.
// Hacking it here for hacking purpose


#include <string>
#include <vector>

#include "uvarint.h"
#include "quicr_common.h"
#include "message_buffer.h"

namespace quicr::messages {

    // Handy defines
    using TrackNamespace = std::string;
    using TrackName = std::string;
    using TrackId = uintVar_t;
    using ErrorCode = uintVar_t;
    using ReasonPhrase = std::string;
    using GroupSequence = uintVar_t;
    using ObjectSequence = uintVar_t;
    using ObjectPriority = uintVar_t;
    using Version = uintVar_t;

    // Message Type Constants
    constexpr u_int8_t MESSAGE_TYPE_OBJECT           = 0x0;
    constexpr u_int8_t MESSAGE_TYPE_OBJECT_WITH_LEN  = 0x2;
    constexpr u_int8_t MESSAGE_TYPE_SUBSCRIBE        = 0x3;
    constexpr u_int8_t MESSAGE_TYPE_SUBSCRIBE_OK     = 0x4;
    constexpr u_int8_t MESSAGE_TYPE_SUBSCRIBE_ERROR  = 0x5;
    constexpr u_int8_t MESSAGE_TYPE_ANNOUNCE         = 0x6;
    constexpr u_int8_t MESSAGE_TYPE_ANNOUNCE_OK      = 0x7;
    constexpr u_int8_t MESSAGE_TYPE_ANNOUNCE_ERROR   = 0x8;
    constexpr u_int8_t MESSAGE_TYPE_UNANNOUNCE       = 0x9;
    constexpr u_int8_t MESSAGE_TYPE_UNSUBSCRIBE      = 0xA;
    constexpr u_int8_t MESSAGE_TYPE_SUBSCRIBE_FIN    = 0xB;
    constexpr u_int8_t MESSAGE_TYPE_SUBSCRIBE_RST    = 0xC;
    constexpr u_int8_t MESSAGE_TYPE_GOAWAY           = 0x10;
    constexpr u_int8_t MESSAGE_TYPE_CLIENT_SETUP     = 0x40;
    constexpr u_int8_t MESSAGE_TYPE_SERVER_SETUP     = 0x41;

    // Not specified in the draft yet
    enum ForwardingPreference : uint8_t {
        StreamPerGroup = 0,
        StreamPerObject,
        StreamPerPriority,
        StreamPerTrack,
        Datagram
    };

    // Track Definitions
    struct FullTrackName {
       TrackNamespace track_namespace;
       TrackName  track_name;
       bool operator==(const FullTrackName& rhs) const
       {
         return (track_namespace == rhs.track_namespace)
                && (track_name == rhs.track_name);
       }
    };
    MessageBuffer& operator<<(MessageBuffer &buffer, const FullTrackName &msg);
    MessageBuffer& operator>>(MessageBuffer &buffer, FullTrackName &msg);


    //
    // Setup types
    //
    struct Parameter {
        uintVar_t param_type;
        uintVar_t length;
        bytes     value;
    };
    MessageBuffer& operator<<(MessageBuffer &buffer, const Parameter &msg);
    MessageBuffer& operator>>(MessageBuffer &buffer, Parameter &msg);


    struct ClientSetup {
        std::vector<Version> supported_versions;
        std::vector<Parameter> parameters;
    };
    MessageBuffer& operator<<(MessageBuffer &buffer, const ClientSetup &msg);
    MessageBuffer& operator>>(MessageBuffer &buffer, ClientSetup &msg);


    struct ServerSetup {
        Version selected_version;
        std::vector<Parameter> parameters;
    };
    MessageBuffer& operator<<(MessageBuffer &buffer, const ServerSetup &msg);
    MessageBuffer& operator>>(MessageBuffer &buffer, ServerSetup &msg);


    //
    // Subscribe Types
    //
    enum LocationMode : uint8_t {
        None = 0,
        Absolute,
        RelativePrevious,
        RelativeNext,
    };

    struct Location {
       uintVar_t mode;
       std::optional<uintVar_t> value;
       bool operator==(const Location& rhs) const
       {
         return (mode == rhs.mode) && (value == rhs.value);
       }
    };
    MessageBuffer& operator<<(MessageBuffer &buffer, const Location &msg);
    MessageBuffer& operator>>(MessageBuffer &buffer, Location &msg);


    struct TrackRequestParameters {

    };

    struct MoqSubscribe {
        FullTrackName track;
        Location start_group;
        Location start_object;
        Location end_group;
        Location end_object;
        TrackRequestParameters parameters;
    };
    MessageBuffer& operator<<(MessageBuffer &buffer, const MoqSubscribe &msg);
    MessageBuffer& operator>>(MessageBuffer &buffer, MoqSubscribe &msg);


    struct MoqSubscribeOk {
        FullTrackName track;
        TrackId track_id;
        uintVar_t expires;
    };
    MessageBuffer& operator<<(MessageBuffer &buffer, const MoqSubscribeOk &msg);
    MessageBuffer& operator>>(MessageBuffer &buffer, MoqSubscribeOk &msg);


    struct MoqSubscribeError {
        FullTrackName track;
        ErrorCode err_code;
        ReasonPhrase reason_phrase;
    };
    MessageBuffer& operator<<(MessageBuffer &buffer, const MoqSubscribeError &msg);
    MessageBuffer& operator>>(MessageBuffer &buffer, MoqSubscribeError &msg);


    struct MoqUnsubscribe {
        FullTrackName track;
    };
    MessageBuffer& operator<<(MessageBuffer &buffer, const MoqUnsubscribe &msg);
    MessageBuffer& operator>>(MessageBuffer &buffer, MoqUnsubscribe &msg);


    struct MoqSubscribeFin {
        FullTrackName track;
        GroupSequence final_group;
        ObjectSequence  final_object;
    };
    MessageBuffer& operator<<(MessageBuffer &buffer, const MoqSubscribeFin &msg);
    MessageBuffer& operator>>(MessageBuffer &buffer, MoqSubscribeFin &msg);


    struct MoqSubscribeRst {
        FullTrackName track;
        ErrorCode err_code;
        ReasonPhrase reason_phrase;
        GroupSequence final_group;
        ObjectSequence  final_object;
    };
    MessageBuffer& operator<<(MessageBuffer &buffer, const MoqSubscribeRst &msg);
    MessageBuffer& operator>>(MessageBuffer &buffer, MoqSubscribeRst &msg);


    ///
    /// Announce types
    ///
    struct MoqAnnounce {
        TrackNamespace track_namespace;
        std::vector<Parameter> parameters;
    };
    MessageBuffer& operator<<(MessageBuffer& buffer, const MoqAnnounce& msg);
    MessageBuffer& operator>>(MessageBuffer &buffer, MoqAnnounce &msg);

    struct MoqAnnounceOk {
        TrackNamespace track_namespace;
    };
    MessageBuffer &operator<<(MessageBuffer &buffer, const MoqAnnounceOk &msg);
    MessageBuffer &operator>>(MessageBuffer &buffer, MoqAnnounceOk &msg);


    struct MoqAnnounceError {
        TrackNamespace track_namespace;
        ErrorCode err_code;
        ReasonPhrase reason_phrase;
    };
    MessageBuffer &operator<<(MessageBuffer &buffer, const MoqAnnounceError &msg);
    MessageBuffer &operator>>(MessageBuffer &buffer, MoqAnnounceError &msg);


    struct MoqUnannounce {
        TrackNamespace track_namespace;
    };
    MessageBuffer &operator<<(MessageBuffer &buffer, const MoqUnannounce &msg);
    MessageBuffer &operator>>(MessageBuffer &buffer, MoqUnannounce &msg);


    ///
    /// GoAway
    ///

    struct MoqGoaway {
        std::string new_session_uri;
    };

    MessageBuffer &operator<<(MessageBuffer &buffer, const MoqGoaway &msg);
    MessageBuffer &operator>>(MessageBuffer &buffer, MoqGoaway &msg);


    ///
    /// Object
    /// Note: By default, all our objects are length delimited


    struct MoqObject {
        TrackId track_id;
        GroupSequence group_sequence;
        ObjectSequence object_sequence;
        ObjectPriority priority;
        quicr::bytes payload;
    };

}