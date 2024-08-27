#pragma once

#include <moq/message_buffer.h>
#include <moq/common.h>
#include <string>
#include <transport/stream_buffer.h>
#include <vector>

namespace moq::messages {
using namespace quicr::messages;

using Version = uint64_t;
using TrackNamespace = Bytes;
using TrackName = Bytes;
using ErrorCode = uint64_t;
using StatusCode = uint64_t;
using ReasonPhrase = Bytes;
using GroupId = uint64_t;
using ObjectId = uint64_t;
using ObjectPriority = uint64_t;
using SubscribeId = uint64_t;
using TrackAlias = uint64_t;
using ParamType = uint64_t;


enum class MoqtTerminationReason : uint64_t
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
enum class MoqtMessageType : uint64_t
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

enum class SubscribeError : uint8_t
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

struct MoqtParameter {
  uint64_t type{0};
  uint64_t length{0};
  Bytes value;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtParameter &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqtParameter& msg);
private:
  uint64_t current_pos {0};
};

MessageBuffer& operator<<(MessageBuffer &buffer, const MoqtParameter &param);
MessageBuffer& operator>>(MessageBuffer &buffer, MoqtParameter &param);

//
// Setup
//

struct MoqtClientSetup {
  uint64_t num_versions {0};
  std::vector<Version> supported_versions;
  MoqtParameter role_parameter;
  MoqtParameter path_parameter;
  MoqtParameter endpoint_id_parameter;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtClientSetup &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqtClientSetup& msg);
private:
  size_t current_pos {0};
  std::optional<uint64_t> num_params;
  std::optional<MoqtParameter> current_param {};
  bool parse_completed { false };
};

struct MoqtServerSetup {
  Version selection_version;
  MoqtParameter role_parameter;
  MoqtParameter path_parameter;
  MoqtParameter endpoint_id_parameter;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtServerSetup &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqtServerSetup& msg);
private:
  size_t current_pos {0};
  std::optional<uint64_t> num_params;
  bool parse_completed { false };
  std::optional<MoqtParameter> current_param {};

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

struct MoqtSubscribe {
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
  std::vector<MoqtParameter> track_params;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtSubscribe &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqtSubscribe& msg);
private:
  std::optional<MoqtParameter> current_param{};
  size_t current_pos {0};
  bool parsing_completed { false };
};

struct MoqtSubscribeOk {
  SubscribeId subscribe_id;
  uint64_t expires;
  bool content_exists;
  uint64_t largest_group {0};
  uint64_t largest_object {0};
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtSubscribeOk &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqtSubscribeOk& msg);
private:
  size_t current_pos {0};
  size_t MAX_FIELDS {5};
};


struct MoqtSubscribeError {
  uint64_t subscribe_id;
  ErrorCode err_code;
  ReasonPhrase reason_phrase;
  uint64_t track_alias;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtSubscribeError &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqtSubscribeError& msg);
private:
  size_t current_pos {0};
  size_t MAX_FIELDS {4};
};

struct MoqtUnsubscribe {
  SubscribeId  subscribe_id;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtUnsubscribe &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqtUnsubscribe& msg);
};

struct MoqtSubscribeDone
{
  uint64_t subscribe_id;
  uint64_t status_code;
  ReasonPhrase reason_phrase;
  bool content_exists;
  uint64_t final_group_id;
  uint64_t final_object_id;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtSubscribeDone &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqtSubscribeDone& msg);
private:
  size_t current_pos {0};
  size_t MAX_FIELDS = {6};
};

//
// Track Status
//
struct MoqtTrackStatusRequest {
    TrackNamespace track_namespace;
    TrackName track_name;
    friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtTrackStatusRequest &msg);
    friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                         const MoqtTrackStatusRequest& msg);
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
struct MoqtTrackStatus {
    TrackNamespace track_namespace;
    TrackName track_name;
    TrackStatus status_code;
    uint64_t last_group_id {0};
    uint64_t last_object_id {0};
    friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtTrackStatus &msg);
    friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                         const MoqtTrackStatus& msg);
  private:
    size_t current_pos {0};
    bool parsing_completed { false };
};


//
// Announce
//

struct MoqtAnnounce {
  TrackNamespace track_namespace;
  std::vector<MoqtParameter> params;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtAnnounce &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqtAnnounce& msg);
private:
  uint64_t num_params {0};
  MoqtParameter current_param{};
};

struct MoqtAnnounceOk {
  TrackNamespace track_namespace;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtAnnounceOk &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqtAnnounceOk& msg);
};

struct MoqtAnnounceError {
  std::optional<TrackNamespace> track_namespace;
  std::optional<ErrorCode> err_code;
  std::optional<ReasonPhrase> reason_phrase;

  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtAnnounceError &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqtAnnounceError& msg);
};

struct MoqtUnannounce {
  TrackNamespace track_namespace;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtUnannounce &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqtUnannounce& msg);
};

struct MoqtAnnounceCancel {
  TrackNamespace track_namespace;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtAnnounceCancel &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqtAnnounceCancel& msg);
};


//
// GoAway
//
struct MoqtGoaway {
  Bytes new_session_uri;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtGoaway &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqtGoaway& msg);
};

MessageBuffer& operator<<(MessageBuffer& buffer, const MoqtGoaway& msg);
MessageBuffer& operator>>(MessageBuffer &buffer, MoqtGoaway &msg);


//
// Object
//
struct MoqtObjectStream {
  SubscribeId subscribe_id;
  TrackAlias track_alias;
  GroupId group_id;
  ObjectId object_id;
  ObjectPriority priority;
  Bytes payload;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtObjectStream &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqtObjectStream& msg);
private:
  uint64_t current_pos {0};
  bool parse_completed { false };
};

struct MoqtObjectDatagram {
  SubscribeId subscribe_id;
  TrackAlias track_alias;
  GroupId group_id;
  ObjectId object_id;
  ObjectPriority priority;
  Bytes payload;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtObjectDatagram &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqtObjectDatagram& msg);
private:
  uint64_t current_pos {0};
  bool parse_completed { false };
};


struct MoqtStreamHeaderTrack {
  SubscribeId subscribe_id;
  TrackAlias track_alias;
  ObjectPriority priority;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtStreamHeaderTrack &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqtStreamHeaderTrack& msg);
private:
  uint64_t current_pos {0};
  bool parse_completed { false };
};

struct MoqtStreamTrackObject {
  GroupId  group_id;
  ObjectId object_id;
  Bytes payload;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtStreamTrackObject &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqtStreamTrackObject& msg);
private:
  uint64_t current_pos {0};
  bool parse_completed { false };
};


struct MoqtStreamHeaderGroup {
  SubscribeId subscribe_id;
  TrackAlias track_alias;
  GroupId group_id;
  ObjectPriority priority;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtStreamHeaderGroup &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqtStreamHeaderGroup& msg);
private:
  uint64_t current_pos {0};
  bool parse_completed { false };
};

struct MoqtStreamGroupObject {
  ObjectId object_id;
  Bytes payload;
  friend bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtStreamGroupObject &msg);
  friend qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                                       const MoqtStreamGroupObject& msg);
private:
  uint64_t current_pos {0};
  bool parse_completed { false };
};

} // end of namespace moq::messages