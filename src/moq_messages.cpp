#include "quicr/moq_messages.h"
#include "quicr/message_buffer.h"
#include "quicr/encode.h"

namespace quicr::messages {

//
// Optional
//

template <typename T>
MessageBuffer& operator<<(MessageBuffer& buffer, const std::optional<T>& val) {
  if (val.has_value()) {
    buffer << val.value();
  }
  return buffer;
}

template <typename T>
MessageBuffer& operator>>(MessageBuffer& buffer, std::optional<T>& val) {
  T val_in{};
  buffer >> val_in;
  val = val_in;
  return buffer;
}


//
// MoqParameter
//

MessageBuffer& operator<<(MessageBuffer &buffer, const MoqParameter &param) {
  buffer << param.param_type;
  buffer << param.param_length;
  if (param.param_length) {
    buffer << param.param_value;
  }

  return buffer;
}

MessageBuffer& operator>>(MessageBuffer &buffer, MoqParameter &param) {
  buffer >> param.param_type;
  buffer >> param.param_length;
  if (static_cast<uint64_t>(param.param_length) > 0) {
    buffer >> param.param_value;
  }
  return buffer;
}


//
// Subscribe
//

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqSubscribe& msg){
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_SUBSCRIBE)));
  buffer.push(qtransport::to_uintV(msg.subscribe_id.value()));
  buffer.push(qtransport::to_uintV(msg.track_alias.value()));
  buffer.push_lv(msg.track_namespace);
  buffer.push_lv(msg.track_name);
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(msg.filter_type)));

  switch (msg.filter_type) {
    case FilterType::None:
    case FilterType::LatestGroup:
    case FilterType::LatestObject:
      break;
    case FilterType::AbsoluteStart: {
      buffer.push(qtransport::to_uintV(msg.start_group.value()));
      buffer.push(qtransport::to_uintV(msg.start_object.value()));
    }
      break;
    case FilterType::AbsoluteRange:
      buffer.push(qtransport::to_uintV(msg.start_group.value()));
      buffer.push(qtransport::to_uintV(msg.start_object.value()));
      buffer.push(qtransport::to_uintV(msg.end_group.value()));
      buffer.push(qtransport::to_uintV(msg.end_object.value()));
      break;
  }

  buffer.push(qtransport::to_uintV(msg.num_params));
  for (const auto& param: msg.track_params) {
    buffer.push(qtransport::to_uintV(static_cast<uint64_t>(param.param_type.value())));
    buffer.push(qtransport::to_uintV(param.param_length));
    buffer.push(param.param_value);
  }

  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqSubscribe &msg) {

  if (!msg.subscribe_id.has_value()) {
    auto val = buffer.decode_uintV();
    if (!val) {
      return false;
    }
    msg.subscribe_id = val.value();
  }

  if (!msg.track_alias.has_value()) {
    auto val = buffer.decode_uintV();
    if (!val) {
      return false;
    }
    msg.track_alias = val.value();
  }

  if (msg.track_namespace.empty())
  {
    const auto val = buffer.decode_bytes();
    if (!val) {
      return false;
    }
    msg.track_namespace = val.value();
  }

  if (msg.track_name.empty())
  {
    const auto val = buffer.decode_bytes();
    if (!val) {
      return false;
    }
    msg.track_name = val.value();
  }

  if (msg.filter_type == FilterType::None)
  {
    const auto val = buffer.decode_uintV();
    if (!val) {
      return false;
    }

    auto filter = val.value();
    msg.filter_type = static_cast<FilterType>(filter);
  }

  switch (msg.filter_type) {
    case FilterType::None:
    throw std::runtime_error("Malformed Filter Type");
    case FilterType::LatestGroup:
    case FilterType::LatestObject:
      break;
    case FilterType::AbsoluteStart:
    {
      if (!msg.start_group.has_value()) {
        const auto val = buffer.decode_uintV();
        if (!val) {
          return false;
        }
        msg.start_group = val.value();
      }

      if (!msg.start_object.has_value()) {
        const auto val = buffer.decode_uintV();
        if (!val) {
          return false;
        }
        msg.start_object = val.value();
      }
    }
      break;
    case FilterType::AbsoluteRange:
    {
      if (!msg.start_group.has_value()) {
        const auto val = buffer.decode_uintV();
        if (!val) {
          return false;
        }
        msg.start_group = val.value();
      }

      if (!msg.start_object.has_value()) {
        const auto val = buffer.decode_uintV();
        if (!val) {
          return false;
        }
        msg.start_object = val.value();
      }

      if (!msg.end_group.has_value()) {
        const auto val = buffer.decode_uintV();
        if (!val) {
          return false;
        }
        msg.end_group = val.value();
      }

      if (!msg.end_object.has_value()) {
        const auto val = buffer.decode_uintV();
        if (!val) {
          return false;
        }
        msg.end_object = val.value();
      }
    }
      break;
  }

  if (!msg.num_params) {
    const auto val = buffer.decode_uintV();
    if (!val) {
      return false;
    }
    msg.num_params = val.value();
  }

  // parse each param
  while (msg.num_params > 0) {
    if (!msg.current_param.param_type) {
      auto val = buffer.front();
      if (!val) {
        return false;
      }
      msg.current_param.param_type = *val;
      buffer.pop();
    }

    // decode param_len:<bytes>
    auto param = buffer.decode_bytes();
    if (!param) {
      return false;
    }

    msg.current_param.param_length = param->size();
    msg.current_param.param_value = param.value();
    msg.track_params.push_back(msg.current_param);
    msg.current_param = {};
    msg.num_params -= 1;
  }

  return true;
}

MessageBuffer &
operator<<(MessageBuffer &buffer, const MoqUnsubscribe &msg) {
  buffer << static_cast<uint8_t>(MESSAGE_TYPE_UNSUBSCRIBE);
  buffer << msg.subscribe_id;
  return buffer;
}

MessageBuffer &
operator>>(MessageBuffer &buffer, MoqUnsubscribe &msg) {
  buffer >> msg.subscribe_id;
  return buffer;
}

MessageBuffer &
operator<<(MessageBuffer &buffer, const MoqSubscribeOk &msg) {
  buffer << static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_OK);
  buffer << msg.subscribe_id;
  buffer << msg.expires;
  buffer << static_cast<uint8_t>(msg.content_exists);
  if (msg.content_exists) {
    buffer << msg.largest_group;
    buffer << msg.largest_object;
    return buffer;
  }
  return buffer;
}

MessageBuffer &
operator>>(MessageBuffer &buffer, MoqSubscribeOk &msg) {
  buffer >> msg.subscribe_id;
  buffer >> msg.expires;
  uint8_t content_exists {0};
  buffer >> content_exists;
  if(content_exists > 1) {
    throw std::runtime_error("Invalid Context Exists Value");
  }

  if (content_exists == 1) {
    msg.content_exists = true;
    buffer >> msg.largest_group;
    buffer >> msg.largest_object;
    return buffer;
  }

  msg.content_exists = false;
  return buffer;
}

MessageBuffer &
operator<<(MessageBuffer &buffer, const MoqSubscribeError &msg) {
  buffer << static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_ERROR);
  buffer << msg.subscribe_id;
  buffer << msg.err_code;
  buffer << msg.reason_phrase;
  buffer << msg.track_alias;
  return buffer;
}

MessageBuffer &
operator>>(MessageBuffer &buffer, MoqSubscribeError &msg) {
  buffer >> msg.subscribe_id;
  buffer >> msg.err_code;
  buffer >> msg.reason_phrase;
  buffer >> msg.track_alias;
  return buffer;
}

MessageBuffer &
operator<<(MessageBuffer &buffer, const MoqSubscribeDone &msg) {
  buffer << static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_DONE);
  buffer << msg.subscribe_id;
  buffer << msg.status_code;
  buffer << msg.reason_phrase;
  buffer << uint8_t(msg.content_exists);
  if (msg.content_exists) {
    buffer << msg.final_group_id;
    buffer << msg.final_object_id;
  }
  return buffer;
}

MessageBuffer &
operator>>(MessageBuffer &buffer, MoqSubscribeDone&msg) {
  buffer >> msg.subscribe_id;
  buffer >> msg.status_code;
  buffer >> msg.reason_phrase;
  uint8_t  context_exists {0};
  buffer >> context_exists;
  if (context_exists > 1) {
    throw std::runtime_error("Incorrect Context Exists value");
  }

  if (context_exists == 1) {
    msg.content_exists = true;
    buffer >> msg.final_group_id;
    buffer >> msg.final_object_id;
    return buffer;
  }

  msg.content_exists = false;
  return buffer;
}


//
// Announce
//


qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqAnnounce& msg){
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_ANNOUNCE)));
  buffer.push_lv(msg.track_namespace);
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(0)));
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer,
           MoqAnnounce &msg) {

  // read namespace
  if (msg.track_namespace.empty())
  {
    const auto val = buffer.decode_bytes();
    if (!val) {
      return false;
    }
    msg.track_namespace = *val;
  }

  if (!msg.num_params) {
    const auto val = buffer.decode_uintV();
    if (!val) {
      return false;
    }
    msg.num_params = *val;
  }

  // parse each param
  while (msg.num_params > 0) {
    if (!msg.current_param.param_type) {
      auto val = buffer.front();
      if (!val) {
        return false;
      }
      msg.current_param.param_type = *val;
      buffer.pop();
    }

    // decode param_len:<bytes>
    auto param = buffer.decode_bytes();
    if (!param) {
      return false;
    }

    msg.current_param.param_length = param->size();
    msg.current_param.param_value = param.value();
    msg.params.push_back(msg.current_param);
    msg.current_param = {};
    msg.num_params -= 1;
  }

  return true;
}

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqAnnounceOk& msg){
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_ANNOUNCE_OK)));
  buffer.push_lv(msg.track_namespace);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqAnnounceOk &msg) {

  // read namespace
  if (msg.track_namespace.empty())
  {
    const auto val = buffer.decode_bytes();
    if (!val) {
      return false;
    }
    msg.track_namespace = *val;
  }
  return true;
}


qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqAnnounceError& msg){
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_ANNOUNCE_ERROR)));
  buffer.push_lv(msg.track_namespace.value());
  buffer.push(qtransport::to_uintV(msg.err_code.value()));
  buffer.push_lv(msg.reason_phrase.value());
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqAnnounceError &msg) {

  // read namespace
  if (!msg.track_namespace)
  {
    const auto val = buffer.decode_bytes();
    if (!val) {
      return false;
    }
    msg.track_namespace = *val;
  }

  if (!msg.err_code) {
    const auto val = buffer.decode_uintV();
    if (!val) {
      return false;
    }

    msg.err_code = *val;
  }
  while (!msg.reason_phrase > 0) {
    auto reason = buffer.decode_bytes();
    if (!reason) {
      return false;
    }
    msg.reason_phrase = reason;
  }

  return true;
}

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqUnannounce& msg){
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_UNANNOUNCE)));
  buffer.push_lv(msg.track_namespace);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqUnannounce &msg) {

  // read namespace
  if (msg.track_namespace.empty())
  {
    const auto val = buffer.decode_bytes();
    if (!val) {
      return false;
    }
    msg.track_namespace = *val;
  }
  return true;
}

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqAnnounceCancel& msg){
  buffer.push(qtransport::to_uintV(static_cast<uint64_t>(MESSAGE_TYPE_ANNOUNCE_CANCEL)));
  buffer.push_lv(msg.track_namespace);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqAnnounceCancel &msg) {

  // read namespace
  if (msg.track_namespace.empty())
  {
    const auto val = buffer.decode_bytes();
    if (!val) {
      return false;
    }
    msg.track_namespace = *val;
  }
  return true;
}

//
// Goaway
//

MessageBuffer &
operator<<(MessageBuffer &buffer, const MoqGoaway &msg) {
  buffer << static_cast<uint8_t>(MESSAGE_TYPE_GOAWAY);
  buffer << msg.new_session_uri;
  return buffer;
}

MessageBuffer &
operator>>(MessageBuffer &buffer, MoqGoaway &msg) {
  buffer >> msg.new_session_uri;
  return buffer;
}

//
// Object
//

MessageBuffer &
operator<<(MessageBuffer &buffer, const MoqObjectStream &msg) {
  buffer << static_cast<uint8_t>(MESSAGE_TYPE_OBJECT_STREAM);
  buffer << msg.subscribe_id;
  buffer << msg.track_alias;
  buffer << msg.group_id;
  buffer << msg.object_id;
  buffer << msg.priority;
  buffer << msg.payload;
  return buffer;
}

MessageBuffer &
operator>>(MessageBuffer &buffer, MoqObjectStream &msg) {
  buffer >> msg.subscribe_id;
  buffer >> msg.track_alias;
  buffer >> msg.group_id;
  buffer >> msg.object_id;
  buffer >> msg.priority;
  buffer >> msg.payload;
  return buffer;
}

MessageBuffer &
operator<<(MessageBuffer &buffer, const MoqObjectDatagram &msg) {
  buffer << MESSAGE_TYPE_OBJECT_DATAGRAM;
  buffer << msg.subscribe_id;
  buffer << msg.track_alias;
  buffer << msg.group_id;
  buffer << msg.object_id;
  buffer << msg.priority;
  buffer << msg.payload;
  return buffer;
}

MessageBuffer &
operator>>(MessageBuffer &buffer, MoqObjectDatagram &msg) {
  buffer >> msg.subscribe_id;
  buffer >> msg.track_alias;
  buffer >> msg.group_id;
  buffer >> msg.object_id;
  buffer >> msg.priority;
  buffer >> msg.payload;
  return buffer;
}

MessageBuffer &
operator<<(MessageBuffer &buffer, const MoqStreamTrackObject &msg) {
  buffer << msg.group_id;
  buffer << msg.object_id;
  buffer << msg.payload;
  return buffer;
}

MessageBuffer&
operator>>(MessageBuffer &buffer, MoqStreamTrackObject &msg) {
  buffer >> msg.group_id;
  buffer >> msg.object_id;
  buffer >> msg.payload;
  return buffer;
}

MessageBuffer &
operator<<(MessageBuffer &buffer, const MoqStreamHeaderTrack &msg) {
  buffer << MESSAGE_TYPE_STREAM_HEADER_TRACK;
  buffer << msg.subscribe_id;
  buffer << msg.track_alias;
  buffer << msg.priority;
  return buffer;
}

MessageBuffer &
operator>>(MessageBuffer &buffer, MoqStreamHeaderTrack &msg) {
  buffer >> msg.subscribe_id;
  buffer >> msg.track_alias;
  buffer >> msg.priority;
  return buffer;
}

MessageBuffer &
operator<<(MessageBuffer &buffer, const MoqStreamHeaderGroup &msg) {
  buffer << MESSAGE_TYPE_STREAM_HEADER_GROUP;
  buffer << msg.subscribe_id;
  buffer << msg.track_alias;
  buffer << msg.group_id;
  buffer << msg.priority;
  return buffer;
}

MessageBuffer &
operator>>(MessageBuffer &buffer, MoqStreamHeaderGroup &msg) {
  buffer >> msg.subscribe_id;
  buffer >> msg.track_alias;
  buffer >> msg.group_id;
  buffer >> msg.priority;
  return buffer;
}

MessageBuffer &
operator<<(MessageBuffer &buffer, const MoqStreamGroupObject &msg) {
  buffer << msg.object_id;
  buffer << msg.payload;
  return buffer;
}

MessageBuffer&
operator>>(MessageBuffer &buffer, MoqStreamGroupObject &msg) {
  buffer >> msg.object_id;
  buffer >> msg.payload;
  return buffer;
}


// Setup

// vector<varint> encode/decode
MessageBuffer&
operator<<(MessageBuffer& buffer, const std::vector<uintVar_t>& val)
{
  buffer << static_cast<uintVar_t>(val.size());
  for(uint64_t i = 0; i < val.size(); i++) {
    buffer << val[i];
  }

  return buffer;
}

MessageBuffer&
operator>>(MessageBuffer& msg, std::vector<uintVar_t>& val)
{
  auto vec_size = uintVar_t(0);
  msg >> vec_size;
  auto version = std::vector<uintVar_t>();
  version.resize((uint64_t) vec_size);
  val.resize((uint64_t) vec_size);

  // TODO (Suhas): This needs revisiting
  for(uint64_t i = 0; i < version.size(); i++) {
    msg >> version[i];
  }
  val = std::move(version);
  return msg;
}


// Client Setup message
MessageBuffer &
operator<<(MessageBuffer &buffer, const MoqClientSetup &msg) {
  buffer << static_cast<uintVar_t>(MESSAGE_TYPE_CLIENT_SETUP);
  // versions
  buffer << static_cast<uintVar_t>(msg.supported_versions.size());
  for (const auto& ver: msg.supported_versions) {
    buffer << static_cast<uintVar_t>(ver);
  }

  // TODO (Suhas): Add support for PATH Param
  // num params
  buffer << uintVar_t{1};
  // role param
  buffer << static_cast<uint8_t>(msg.role_parameter.param_type.value());
  buffer << uintVar_t(1);
  buffer << msg.role_parameter.param_value;

  return buffer;
}

MessageBuffer &
operator>>(MessageBuffer &buffer, MoqClientSetup &msg) {
  uintVar_t num_versions {0};
  buffer >> num_versions;
  msg.supported_versions.resize(num_versions);
  for(size_t i = 0; i < num_versions; i++) {
    uintVar_t version{0};
    buffer >> version;
    msg.supported_versions.push_back(version);
  }

  uintVar_t num_params {0};
  buffer >> num_params;
  if (static_cast<uint64_t> (num_params) == 0) {
    return buffer;
  }

  while (static_cast<uint64_t>(num_params) > 0) {
    uint8_t param_type {0};
    buffer >> param_type;
    auto param_type_enum = static_cast<ParameterType> (param_type);
    switch(param_type_enum) {
      case ParameterType::Role: {
        msg.role_parameter.param_type = param_type;
        buffer >> msg.role_parameter.param_length;
        buffer >> msg.role_parameter.param_value;
      }
      break;
      case ParameterType::Path: {
        msg.path_parameter.param_type = param_type;
        buffer >> msg.path_parameter.param_length;
        buffer >> msg.path_parameter.param_value;
      }
      break;
      default:
        throw std::runtime_error("Unsupported Parameter Type for ClientSetup");
    }
    num_params = num_params - 1;
  }


  return buffer;
}

// Server Setup message
MessageBuffer&
operator<<(MessageBuffer &buffer, const MoqServerSetup &msg) {
  buffer << static_cast<uintVar_t>(MESSAGE_TYPE_SERVER_SETUP);
  buffer << msg.supported_version;
  return buffer;
}

MessageBuffer &
operator>>(MessageBuffer &buffer, MoqServerSetup &msg) {
  buffer >> msg.supported_version;
  return buffer;
}
}