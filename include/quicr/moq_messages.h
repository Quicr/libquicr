#pragma once

#include <string>
#include <vector>
#include <quicr/uvarint.h>
#include <quicr/quicr_common.h>
#include <quicr/message_buffer.h>
#include <transport/stream_buffer.h>

namespace quicr::messages {

using Version = uint64_t;
using TrackNamespace = quicr::bytes;
using TrackName = quicr::bytes;
using ErrorCode = uint64_t;
using StatusCode = uint64_t;
using ReasonPhrase = quicr::bytes;
using GroupId = uint64_t;
using ObjectId = uint64_t;
using ObjectPriority = uint64_t;
using SubscribeId = uint64_t;
using TrackAlias = uint64_t;
using ParamType = uint64_t;


enum class MoQTerminationReason : uint64_t
{
  NO_ERROR = 0x0,
  INTERNAL_ERROR,
  UNAUTHORIZED,
  PROTOCOL_VIOLATION,
  DUP_TRACK_ALIAS,
  PARAM_LEN_MISMATCH,

  GOAWAY_TIMEOUT = 0x10,
};

// Ref: https://moq-wg.github.io/moq-transport/draft-ietf-moq-transport.html#name-messages
enum class MoQMessageType : uint64_t
{
    OBJECT_STREAM = 0x0,
    OBJECT_DATAGRAM,

    SUBSCRIBE = 0x03,
    SUBSCRIBE_OK,
    SUBSCRIBE_ERROR,
    ANNOUNCE,
    ANNOUNCE_OK,
    ANNOUNCE_ERROR,
    UNANNOUNCE,
    UNSUBSCRIBE,
    SUBSCRIBE_DONE,
    ANNOUNCE_CANCEL,
    TRACK_STATUS_REQUEST,
    TRACK_STATUS,

    GOAWAY = 0x10,

    CLIENT_SETUP = 0x40,
    SERVER_SETUP,

    STREAM_HEADER_TRACK = 0x50,
    STREAM_HEADER_GROUP,
};

enum class MoQSubscribeError : uint8_t
{
  INTERNAL_ERROR=0x0,
  INVALID_RANGE,
  RETRY_TRACK_ALIAS,

  TRACK_NOT_EXIST=0xF0 // Missing in draft
};

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
  EndpointId = 0xF0, // Endpoint ID, using temp value for now
  Invalid = 0xFF, // used internally.
};

struct MoqParameter {
  uint64_t type{0};
  uint64_t length{0};
  bytes value;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqParameter &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqParameter& msg);
private:
  uint64_t current_pos {0};
};

MessageBuffer& operator<<(MessageBuffer &buffer, const MoqParameter &param);
MessageBuffer& operator>>(MessageBuffer &buffer, MoqParameter &param);

//
// Setup
//

struct MoqClientSetup {
  uint64_t num_versions {0};
  std::vector<Version> supported_versions;
  MoqParameter role_parameter;
  MoqParameter path_parameter;
  MoqParameter endpoint_id_parameter;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqClientSetup &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqClientSetup& msg);
private:
  size_t current_pos {0};
  std::optional<uint64_t> num_params;
  std::optional<MoqParameter> current_param {};
  bool parse_completed { false };
};

struct MoqServerSetup {
  Version selection_version;
  MoqParameter role_parameter;
  MoqParameter path_parameter;
  MoqParameter endpoint_id_parameter;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqServerSetup &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqServerSetup& msg);
private:
  size_t current_pos {0};
  std::optional<uint64_t> num_params;
  bool parse_completed { false };
  std::optional<MoqParameter> current_param {};

};

//
// Subscribe
//
enum struct FilterType: uint64_t {
  None = 0x0,
  LatestGroup,
  LatestObject,
  AbsoluteStart,
  AbsoluteRange
};

struct MoqSubscribe {
  uint64_t subscribe_id;
  uint64_t track_alias;
  TrackNamespace track_namespace;
  TrackName track_name;
  FilterType filter_type {FilterType::None};
  uint64_t start_group {0};
  uint64_t end_group {0};
  uint64_t start_object {0};
  uint64_t end_object {0};
  std::optional<uint64_t> num_params;
  std::vector<MoqParameter> track_params;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqSubscribe &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqSubscribe& msg);
private:
  std::optional<MoqParameter> current_param{};
  size_t current_pos {0};
  bool parsing_completed { false };
};

struct MoqSubscribeOk {
  SubscribeId subscribe_id;
  uint64_t expires;
  bool content_exists;
  uint64_t largest_group {0};
  uint64_t largest_object {0};
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqSubscribeOk &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqSubscribeOk& msg);
private:
  size_t current_pos {0};
  size_t MAX_FIELDS {5};
};


struct MoqSubscribeError {
  uint64_t subscribe_id;
  ErrorCode err_code;
  ReasonPhrase reason_phrase;
  uint64_t track_alias;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqSubscribeError &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqSubscribeError& msg);
private:
  size_t current_pos {0};
  size_t MAX_FIELDS {4};
};

struct MoqUnsubscribe {
  SubscribeId  subscribe_id;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqUnsubscribe &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqUnsubscribe& msg);
};

struct MoqSubscribeDone
{
  uint64_t subscribe_id;
  uint64_t status_code;
  ReasonPhrase reason_phrase;
  bool content_exists;
  uint64_t final_group_id;
  uint64_t final_object_id;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqSubscribeDone &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqSubscribeDone& msg);
private:
  size_t current_pos {0};
  size_t MAX_FIELDS = {6};
};

//
// Track Status
//
struct MoqTrackStatusRequest {
    TrackNamespace track_namespace;
    TrackName track_name;
    friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqTrackStatusRequest &msg);
    friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                         const MoqTrackStatusRequest& msg);
  private:
    size_t current_pos {0};
    bool parsing_completed { false };
};

enum class TrackStatus : uint64_t {
    IN_PROGRESS=0x00,
    DOES_NOT_EXIST,
    NOT_STARTED,
    FINISHED,
    UNKNOWN
};
struct MoqTrackStatus {
    TrackNamespace track_namespace;
    TrackName track_name;
    TrackStatus status_code;
    uint64_t last_group_id {0};
    uint64_t last_object_id {0};
    friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqTrackStatus &msg);
    friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                         const MoqTrackStatus& msg);
  private:
    size_t current_pos {0};
    bool parsing_completed { false };
};


//
// Announce
//

struct MoqAnnounce {
  TrackNamespace track_namespace;
  std::vector<MoqParameter> params;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqAnnounce &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqAnnounce& msg);
private:
  uint64_t num_params {0};
  MoqParameter current_param{};
};

struct MoqAnnounceOk {
  TrackNamespace track_namespace;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqAnnounceOk &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqAnnounceOk& msg);
};

struct MoqAnnounceError {
  std::optional<TrackNamespace> track_namespace;
  std::optional<ErrorCode> err_code;
  std::optional<ReasonPhrase> reason_phrase;

  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqAnnounceError &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqAnnounceError& msg);
};

struct MoqUnannounce {
  TrackNamespace track_namespace;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqUnannounce &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqUnannounce& msg);
};

struct MoqAnnounceCancel {
  TrackNamespace track_namespace;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqAnnounceCancel &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqAnnounceCancel& msg);
};


//
// GoAway
//
struct MoqGoaway {
  quicr::bytes new_session_uri;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqGoaway &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqGoaway& msg);
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
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqObjectStream &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqObjectStream& msg);
private:
  uint64_t current_pos {0};
  bool parse_completed { false };
};

struct MoqObjectDatagram {
  SubscribeId subscribe_id;
  TrackAlias track_alias;
  GroupId group_id;
  ObjectId object_id;
  ObjectPriority priority;
  quicr::bytes payload;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqObjectDatagram &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqObjectDatagram& msg);
private:
  uint64_t current_pos {0};
  bool parse_completed { false };
};


struct MoqStreamHeaderTrack {
  SubscribeId subscribe_id;
  TrackAlias track_alias;
  ObjectPriority priority;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqStreamHeaderTrack &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqStreamHeaderTrack& msg);
private:
  uint64_t current_pos {0};
  bool parse_completed { false };
};

struct MoqStreamTrackObject {
  GroupId  group_id;
  ObjectId object_id;
  quicr::bytes payload;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqStreamTrackObject &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqStreamTrackObject& msg);
private:
  uint64_t current_pos {0};
  bool parse_completed { false };
};


struct MoqStreamHeaderGroup {
  SubscribeId subscribe_id;
  TrackAlias track_alias;
  GroupId group_id;
  ObjectPriority priority;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqStreamHeaderGroup &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqStreamHeaderGroup& msg);
private:
  uint64_t current_pos {0};
  bool parse_completed { false };
};

struct MoqStreamGroupObject {
  ObjectId object_id;
  quicr::bytes payload;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqStreamGroupObject &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqStreamGroupObject& msg);
private:
  uint64_t current_pos {0};
  bool parse_completed { false };
};
  
}