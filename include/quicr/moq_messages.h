#pragma once

#include <string>
#include <vector>
#include <quicr/uvarint.h>
#include <quicr/quicr_common.h>
#include <quicr/message_buffer.h>
#include <transport/stream_buffer.h>

namespace quicr::messages {

using Version = uintVar_t;
using TrackNamespace = quicr::bytes;
using TrackName = quicr::bytes;
using ErrorCode = uintVar_t;
using StatusCode = uintVar_t;
using ReasonPhrase = quicr::bytes;
using GroupId = uintVar_t;
using ObjectId = uintVar_t;
using ObjectPriority = uintVar_t;
using SubscribeId = uintVar_t;
using TrackAlias = uintVar_t;
using ParamType = uintVar_t;

// Ref: https://moq-wg.github.io/moq-transport/draft-ietf-moq-transport.html#name-messages
constexpr uint8_t MESSAGE_TYPE_OBJECT_STREAM            = 0x0;
constexpr uint8_t MESSAGE_TYPE_OBJECT_DATAGRAM          = 0x1;
constexpr uint8_t MESSAGE_TYPE_SUBSCRIBE                = 0x3;
constexpr uint8_t MESSAGE_TYPE_SUBSCRIBE_OK             = 0x4;
constexpr uint8_t MESSAGE_TYPE_SUBSCRIBE_ERROR          = 0x5;
constexpr uint8_t MESSAGE_TYPE_ANNOUNCE                 = 0x6;
constexpr uint8_t MESSAGE_TYPE_ANNOUNCE_OK              = 0x7;
constexpr uint8_t MESSAGE_TYPE_ANNOUNCE_ERROR           = 0x8;
constexpr uint8_t MESSAGE_TYPE_UNANNOUNCE               = 0x9;
constexpr uint8_t MESSAGE_TYPE_UNSUBSCRIBE              = 0xA;
constexpr uint8_t MESSAGE_TYPE_SUBSCRIBE_DONE           = 0xB;
constexpr uint8_t MESSAGE_TYPE_ANNOUNCE_CANCEL          = 0xC;
constexpr uint8_t MESSAGE_TYPE_TRACK_STATUS_REQUEST     = 0xD;
constexpr uint8_t MESSAGE_TYPE_TRACK_STATUS             = 0xE;
constexpr uint8_t MESSAGE_TYPE_GOAWAY                   = 0x10;
constexpr uint8_t MESSAGE_TYPE_CLIENT_SETUP             = 0x40;
constexpr uint8_t MESSAGE_TYPE_SERVER_SETUP             = 0x41;
constexpr uint8_t MESSAGE_TYPE_STREAM_HEADER_TRACK      = 0x50;
constexpr uint8_t MESSAGE_TYPE_STREAM_HEADER_GROUP      = 0x51;

// TODO (Suhas): rename it to StreamMapping
enum ForwardingPreference : uint8_t {
  StreamPerGroup = 0,
  StreamPerObject,
  StreamPerPriority,
  StreamPerTrack,
  Datagram
};


//
// Parameters
//

enum struct ParameterType : uint8_t {
  Role = 0x0,
  Path = 0x1,
  AuthorizationInfo = 0x2, // version specific, unused
  Invalid = 0xFF, // used internally.
};

struct MoqParameter{
  std::optional<ParamType> param_type;
  uintVar_t param_length;
  bytes param_value;
};

//
// Setup
//

struct MoqClientSetup {
  std::vector<Version> supported_versions;
  MoqParameter role_parameter;
  MoqParameter path_parameter;
};

struct MoqServerSetup {
  Version supported_version;
  MoqParameter role_parameter;
  MoqParameter path_parameter;
};

MessageBuffer& operator<<(MessageBuffer &buffer, const MoqParameter &msg);
MessageBuffer& operator>>(MessageBuffer &buffer, MoqParameter &msg);
MessageBuffer& operator<<(MessageBuffer &buffer, const MoqClientSetup &msg);
MessageBuffer& operator>>(MessageBuffer &buffer, MoqClientSetup &msg);
MessageBuffer& operator<<(MessageBuffer &buffer, const MoqServerSetup &msg);
MessageBuffer& operator>>(MessageBuffer &buffer, MoqServerSetup &msg);

//
// Subscribe
//
enum struct LocationMode: uint8_t {
  None = 0x0,
  Absolute,
  RelativePrevious,
  RelativeNext
};

struct Location {
  LocationMode mode;
  std::optional<uintVar_t> value;
};

struct MoqSubscribe {
  SubscribeId subscribe_id;
  TrackAlias track_alias;
  TrackNamespace track_namespace;
  TrackName track_name;
  Location start_group;
  Location start_object;
  Location end_group;
  Location end_object;
  std::vector<MoqParameter> track_params;
};

struct MoqSubscribeOk {
  SubscribeId subscribe_id;
  uintVar_t expires;
  bool content_exists;
  std::optional<GroupId> largest_group;
  std::optional<ObjectId> largest_object;
};


struct MoqSubscribeError {
  SubscribeId subscribe_id;
  ErrorCode err_code;
  ReasonPhrase reason_phrase;
  TrackAlias  track_alias;
};

struct MoqUnsubscribe {
  SubscribeId  subscribe_id;
};

struct MoqSubscribeDone
{
  SubscribeId  subscribe_id;
  StatusCode status_code;
  ReasonPhrase reason_phrase;
  bool content_exists;
  GroupId final_group_id;
  ObjectId final_object_id;
};

MessageBuffer& operator<<(MessageBuffer &buffer, const Location &msg);
MessageBuffer& operator>>(MessageBuffer &buffer, Location &msg);
MessageBuffer& operator<<(MessageBuffer &buffer, const MoqSubscribe &msg);
MessageBuffer& operator>>(MessageBuffer &buffer, MoqSubscribe &msg);
MessageBuffer& operator<<(MessageBuffer &buffer, const MoqUnsubscribe &msg);
MessageBuffer& operator>>(MessageBuffer &buffer, MoqUnsubscribe &msg);
MessageBuffer& operator<<(MessageBuffer &buffer, const MoqSubscribeOk &msg);
MessageBuffer& operator>>(MessageBuffer &buffer, MoqSubscribeOk &msg);
MessageBuffer& operator<<(MessageBuffer &buffer, const MoqSubscribeError &msg);
MessageBuffer& operator>>(MessageBuffer &buffer, MoqSubscribeError &msg);
MessageBuffer& operator<<(MessageBuffer &buffer, const MoqSubscribeDone &msg);
MessageBuffer& operator>>(MessageBuffer &buffer, MoqSubscribeDone &msg);



//
// Announce
//

struct MoqAnnounce {
  TrackNamespace track_namespace;
  std::vector<MoqParameter> params;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqAnnounce &msg);
private:
  uint64_t num_params {0};
  MoqParameter current_param{};
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

struct MoqAnnounceCancel {
  TrackNamespace track_namespace;
};


MessageBuffer& operator<<(MessageBuffer& buffer, const MoqAnnounce& msg);
MessageBuffer& operator>>(MessageBuffer &buffer, MoqAnnounce &msg);
qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer, const MoqAnnounce& msg);
bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqAnnounce &msg);
MessageBuffer& operator<<(MessageBuffer& buffer, const MoqAnnounceOk& msg);
MessageBuffer& operator>>(MessageBuffer &buffer, MoqAnnounceOk &msg);
MessageBuffer& operator<<(MessageBuffer& buffer, const MoqAnnounceError& msg);
MessageBuffer& operator>>(MessageBuffer &buffer, MoqAnnounceError &msg);
MessageBuffer &operator<<(MessageBuffer &buffer, const MoqUnannounce &msg);
MessageBuffer &operator>>(MessageBuffer &buffer, MoqUnannounce &msg);
MessageBuffer &operator<<(MessageBuffer &buffer, const MoqAnnounceCancel &msg);
MessageBuffer &operator>>(MessageBuffer &buffer, MoqAnnounceCancel &msg);


//
// GoAway
//
struct MoqGoaway {
  quicr::bytes new_session_uri;
};

MessageBuffer& operator<<(MessageBuffer& buffer, const MoqGoaway& msg);
MessageBuffer& operator>>(MessageBuffer &buffer, MoqGoaway &msg);


//
// Object
//
struct MoqObjectStream {
  SubscribeId subscribe_id;
  TrackAlias track_alias;
  GroupId group_id;
  ObjectId object_id;
  ObjectPriority priority;
  quicr::bytes payload;
};

struct MoqObjectDatagram {
  SubscribeId subscribe_id;
  TrackAlias track_alias;
  GroupId group_id;
  ObjectId object_id;
  ObjectPriority priority;
  quicr::bytes payload;
};


struct MoqStreamHeaderTrack {
  SubscribeId subscribe_id;
  TrackAlias track_alias;
  ObjectPriority priority;
};

struct MoqStreamTrackObject {
  GroupId  group_id;
  ObjectId object_id;
  quicr::bytes payload;
};


struct MoqStreamHeaderGroup {
  SubscribeId subscribe_id;
  TrackAlias track_alias;
  GroupId group_id;
  ObjectPriority priority;
};

struct MoqStreamGroupObject {
  ObjectId object_id;
  quicr::bytes payload;
};

MessageBuffer& operator<<(MessageBuffer& buffer, const MoqObjectStream& msg);
MessageBuffer& operator>>(MessageBuffer &buffer, MoqObjectStream &msg);
MessageBuffer& operator<<(MessageBuffer& buffer, const MoqObjectDatagram& msg);
MessageBuffer& operator>>(MessageBuffer &buffer, MoqObjectDatagram &msg);
MessageBuffer& operator<<(MessageBuffer& buffer, const MoqStreamHeaderTrack& msg);
MessageBuffer& operator>>(MessageBuffer &buffer, MoqStreamHeaderTrack &msg);
MessageBuffer& operator<<(MessageBuffer& buffer, const MoqStreamTrackObject& msg);
MessageBuffer& operator>>(MessageBuffer &buffer, MoqStreamTrackObject &msg);
MessageBuffer& operator<<(MessageBuffer& buffer, const MoqStreamHeaderGroup& msg);
MessageBuffer& operator>>(MessageBuffer &buffer, MoqStreamHeaderGroup &msg);
MessageBuffer& operator<<(MessageBuffer& buffer, const MoqStreamGroupObject& msg);
MessageBuffer& operator>>(MessageBuffer &buffer, MoqStreamGroupObject &msg);

// utility
std::tuple<Location, Location, Location, Location>  to_locations(const SubscribeIntent& intent);
MessageBuffer& operator<<(MessageBuffer& buffer, const std::vector<uintVar_t>& val);
MessageBuffer& operator>>(MessageBuffer& msg, std::vector<uintVar_t>& val);
MessageBuffer& operator>>(MessageBuffer& msg, std::vector<uintVar_t>& val);

template <typename T>
MessageBuffer& operator<<(MessageBuffer& buffer, const std::optional<T>& val);
template <typename T>
MessageBuffer& operator>>(MessageBuffer& msg, std::optional<T>& val);
}