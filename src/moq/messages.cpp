#include "messages.h"

namespace moq::messages {

//
// Utility
//
static bool ParseUintVField(qtransport::StreamBuffer<uint8_t> &buffer, uint64_t& field) {
  auto val = buffer.DecodeUintV();
  if (!val) {
    return false;
  }
  field = val.value();
  return true;
}


static bool ParseBytesField(qtransport::StreamBuffer<uint8_t> &buffer, Bytes& field) {
  auto val = buffer.DecodeBytes();
  if (!val) {
    return false;
  }
  field = std::move(val.value());
  return true;
}

MessageBuffer&
operator<<(MessageBuffer& msg, Span<const uint8_t> val)
{
  msg.push(qtransport::ToUintV(val.size()));
  msg.push(val);
  return msg;
}

MessageBuffer&
operator<<(MessageBuffer& msg, std::vector<uint8_t>&& val)
{
  msg.push(qtransport::ToUintV(val.size()));
  msg.push(std::move(val));
  return msg;
}

MessageBuffer&
operator>>(MessageBuffer& msg, std::vector<uint8_t>& val)
{
  std::size_t size = qtransport::UintVSize(msg.front());
  std::size_t vec_size = qtransport::ToUint64(msg.pop_front(size));

  val = msg.pop_front(vec_size);
  return msg;
}

MessageBuffer&
operator<<(MessageBuffer& msg, const std::string& val)
{
  std::vector<uint8_t> v(val.begin(), val.end());
  msg << v;
  return msg;
}

MessageBuffer&
operator>>(MessageBuffer& msg, std::string& val)
{
  std::size_t size = qtransport::UintVSize(msg.front());
  std::size_t vec_size = qtransport::ToUint64(msg.pop_front(size));

  const auto val_vec = msg.pop_front(vec_size);
  val.assign(val_vec.begin(), val_vec.end());

  return msg;
}

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
// MoqtParameter
//

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqParameter& param){

  buffer.Push(qtransport::ToUintV(param.type));
  buffer.Push(qtransport::ToUintV(param.length));
  if (param.length) {
    buffer.PushLv(param.value);
  }
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqParameter &param) {

  if(!ParseUintVField(buffer, param.type)) {
    return false;
  }

  if(!ParseUintVField(buffer, param.length)) {
    return false;
  }

  if(param.length) {
    const auto val = buffer.DecodeBytes();
    if (!val) {
      return false;
    }
    param.value = std::move(val.value());
  }

  return true;
}


MessageBuffer& operator<<(MessageBuffer &buffer, const MoqParameter &param) {
  buffer <<  param.type;
  buffer << param.length;
  if (param.length) {
    buffer << param.value;
  }

  return buffer;
}

MessageBuffer& operator>>(MessageBuffer &buffer, MoqParameter &param) {
  buffer >> param.type;
  buffer >> param.length;
  if (static_cast<uint64_t>(param.length) > 0) {
    buffer >> param.value;
  }
  return buffer;
}

//
// Track Status
//
qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                              const MoqTrackStatus& msg) {
    buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqMessageType::TRACK_STATUS)));
    buffer.PushLv(msg.track_namespace);
    buffer.PushLv(msg.track_name);
    buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(msg.status_code)));
    buffer.Push(qtransport::ToUintV(msg.last_group_id));
    buffer.Push(qtransport::ToUintV(msg.last_object_id));

    return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqTrackStatus &msg) {

    switch (msg.current_pos) {
        case 0: {
            if(!ParseBytesField(buffer, msg.track_namespace)) {
                return false;
            }
            msg.current_pos += 1;
            [[fallthrough]];
        }
        case 1: {
            if(!ParseBytesField(buffer, msg.track_name)) {
                return false;
            }
            msg.current_pos += 1;
            [[fallthrough]];
        }
        case 2: {
            const auto val = buffer.DecodeUintV();
            if (!val) {
                return false;
            }
            msg.status_code = static_cast<TrackStatus>(*val);
            msg.current_pos += 1;
            [[fallthrough]];
        }
        case 3: {
            if (!ParseUintVField(buffer, msg.last_group_id)) {
                return false;
            }
            msg.current_pos += 1;

            [[fallthrough]];
        }
        case 4: {
            if (!ParseUintVField(buffer, msg.last_object_id)) {
                return false;
            }
            msg.current_pos += 1;

            msg.parsing_completed = true;

            [[fallthrough]];
        }
        default:
            break;
    }

    if (!msg.parsing_completed ) {
        return false;
    }

    return true;
}

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
                                              const MoqTrackStatusRequest& msg){
    buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqMessageType::TRACK_STATUS_REQUEST)));
    buffer.PushLv(msg.track_namespace);
    buffer.PushLv(msg.track_name);

    return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqTrackStatusRequest &msg) {

    switch (msg.current_pos) {
        case 0: {
            if (!ParseBytesField(buffer, msg.track_namespace)) {
                return false;
            }
            msg.current_pos += 1;
            [[fallthrough]];
        }
        case 1: {
            if (!ParseBytesField(buffer, msg.track_name)) {
                return false;
            }
            msg.current_pos += 1;
            msg.parsing_completed = true;
            [[fallthrough]];
        }
        default:
            break;
    }

    if (!msg.parsing_completed ) {
        return false;
    }

    return true;
}


//
// Subscribe
//

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqSubscribe& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqMessageType::SUBSCRIBE)));
  buffer.Push(qtransport::ToUintV(msg.subscribe_id));
  buffer.Push(qtransport::ToUintV(msg.track_alias));
  buffer.PushLv(msg.track_namespace);
  buffer.PushLv(msg.track_name);
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(msg.filter_type)));

  switch (msg.filter_type) {
    case FilterType::None:
    case FilterType::LatestGroup:
    case FilterType::LatestObject:
      break;
    case FilterType::AbsoluteStart: {
      buffer.Push(qtransport::ToUintV(msg.start_group));
      buffer.Push(qtransport::ToUintV(msg.start_object));
    }
      break;
    case FilterType::AbsoluteRange:
      buffer.Push(qtransport::ToUintV(msg.start_group));
      buffer.Push(qtransport::ToUintV(msg.start_object));
      buffer.Push(qtransport::ToUintV(msg.end_group));
      buffer.Push(qtransport::ToUintV(msg.end_object));
      break;
  }

  buffer.Push(qtransport::ToUintV(msg.track_params.size()));
  for (const auto& param: msg.track_params) {
    buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(param.type)));
    buffer.PushLv(param.value);
  }

  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqSubscribe &msg) {

  switch (msg.current_pos) {
    case 0: {
      if(!ParseUintVField(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      if(!ParseUintVField(buffer, msg.track_alias)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 2: {
      if(!ParseBytesField(buffer, msg.track_namespace)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 3: {
      if(!ParseBytesField(buffer, msg.track_name)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 4: {
      const auto val = buffer.DecodeUintV();
      if (!val) {
        return false;
      }
      auto filter = val.value();
      msg.filter_type = static_cast<FilterType>(filter);
      if (msg.filter_type == FilterType::LatestGroup
          || msg.filter_type == FilterType::LatestObject) {
        // we don't get further fields until parameters
        msg.current_pos = 9;
      } else {
        msg.current_pos += 1;
      }
      [[fallthrough]];
    }
    case 5: {
      if (msg.filter_type == FilterType::AbsoluteStart
          || msg.filter_type == FilterType::AbsoluteRange) {
        if (!ParseUintVField(buffer, msg.start_group)) {
          return false;
        }
        msg.current_pos += 1;
      }
      [[fallthrough]];
    }
    case 6: {
      if (msg.filter_type == FilterType::AbsoluteStart
          || msg.filter_type == FilterType::AbsoluteRange) {
        if (!ParseUintVField(buffer, msg.start_object)) {
          return false;
        }

        if (msg.filter_type == FilterType::AbsoluteStart) {
          msg.current_pos = 9;
        } else {
          msg.current_pos += 1;
        }
      }
      [[fallthrough]];
    }
    case 7: {
      if (msg.filter_type == FilterType::AbsoluteRange) {
        if (!ParseUintVField(buffer, msg.end_group)) {
          return false;
        }
        msg.current_pos += 1;
      }

      [[fallthrough]];
    }
    case 8: {
      if (msg.filter_type == FilterType::AbsoluteRange) {
        if (!ParseUintVField(buffer, msg.end_object)) {
          return false;
        }
        msg.current_pos += 1;
      }
      [[fallthrough]];
    }
    case 9: {
      if (!msg.num_params.has_value()) {
        uint64_t num = 0;
        if (!ParseUintVField(buffer, num)) {
          return false;
        }

        msg.num_params = num;
      }
      // parse each param
      while (*msg.num_params > 0) {
        if (!msg.current_param.has_value()) {
          uint64_t type {0};
          if (!ParseUintVField(buffer, type)) {
            return false;
          }

          msg.current_param = MoqParameter{};
          msg.current_param->type = type;
        }

        // decode param_len:<bytes>
        auto param = buffer.DecodeBytes();
        if (!param) {
          return false;
        }
        msg.current_param.value().length = param->size();
        msg.current_param.value().value = param.value();
        msg.track_params.push_back(msg.current_param.value());
        msg.current_param = std::nullopt;
        *msg.num_params -= 1;
      }

      msg.parsing_completed = true;
      [[fallthrough]];
    }

    default:
      break;
  }

  if (!msg.parsing_completed ) {
    return false;
  }

  return true;
}


qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqUnsubscribe& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqMessageType::UNSUBSCRIBE)));
  buffer.Push(qtransport::ToUintV(msg.subscribe_id));
  return buffer;
}


bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqUnsubscribe &msg) {
  return ParseUintVField(buffer, msg.subscribe_id);
}

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqSubscribeDone& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqMessageType::SUBSCRIBE_DONE)));
  buffer.Push(qtransport::ToUintV(msg.subscribe_id));
  buffer.Push(qtransport::ToUintV(msg.status_code));
  buffer.PushLv(msg.reason_phrase);
  msg.content_exists ? buffer.Push(static_cast<uint8_t>(1)) : buffer.Push(static_cast<uint8_t>(0));
  if(msg.content_exists) {
    buffer.Push(qtransport::ToUintV(msg.final_group_id));
    buffer.Push(qtransport::ToUintV(msg.final_object_id));
  }

  return buffer;
}


bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqSubscribeDone &msg) {

  switch (msg.current_pos) {
    case 0: {
      if(!ParseUintVField(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      if(!ParseUintVField(buffer, msg.status_code)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 2: {
      const auto val = buffer.DecodeBytes();
      if (!val) {
        return false;
      }
      msg.reason_phrase = std::move(val.value());
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 3: {
      const auto val = buffer.Front();
      if (!val) {
        return false;
      }
      buffer.Pop();
      msg.content_exists = (val.value()) == 1;
      msg.current_pos += 1;
      if (!msg.content_exists) {
        // nothing more to process.
        return true;
      }
      [[fallthrough]];
    }
    case 4: {
      if(!ParseUintVField(buffer, msg.final_group_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 5: {
      if(!ParseUintVField(buffer, msg.final_object_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    default:
      break;
  }

  if (msg.current_pos < msg.MAX_FIELDS) {
    return false;
  }
  return true;
}

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqSubscribeOk& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqMessageType::SUBSCRIBE_OK)));
  buffer.Push(qtransport::ToUintV(msg.subscribe_id));
  buffer.Push(qtransport::ToUintV(msg.expires));
  msg.content_exists ? buffer.Push(static_cast<uint8_t>(1)) : buffer.Push(static_cast<uint8_t>(0));
  if(msg.content_exists) {
    buffer.Push(qtransport::ToUintV(msg.largest_group));
    buffer.Push(qtransport::ToUintV(msg.largest_object));
  }
  return buffer;
}


bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqSubscribeOk &msg) {

  switch (msg.current_pos) {
    case 0:
    {
      if(!ParseUintVField(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      if(!ParseUintVField(buffer, msg.expires)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 2: {
      const auto val = buffer.Front();
      if (!val) {
        return false;
      }
      buffer.Pop();
      msg.content_exists = (val.value()) == 1;
      msg.current_pos += 1;
      if (!msg.content_exists) {
        // nothing more to process.
        return true;
      }
      [[fallthrough]];
    }
    case 3: {
      if(!ParseUintVField(buffer, msg.largest_group)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 4: {
      if(!ParseUintVField(buffer, msg.largest_object)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    default:
      break;
  }

  if (msg.current_pos < msg.MAX_FIELDS) {
    return false;
  }
  return true;
}


qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqSubscribeError& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqMessageType::SUBSCRIBE_ERROR)));
  buffer.Push(qtransport::ToUintV(msg.subscribe_id));
  buffer.Push(qtransport::ToUintV(msg.err_code));
  buffer.PushLv(msg.reason_phrase);
  buffer.Push(qtransport::ToUintV(msg.track_alias));
  return buffer;
}


bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqSubscribeError &msg) {

  switch (msg.current_pos) {
    case 0:
    {
      if(!ParseUintVField(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      if(!ParseUintVField(buffer, msg.err_code)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 2: {
      const auto val = buffer.DecodeBytes();
      if (!val) {
        return false;
      }
      msg.reason_phrase = std::move(val.value());
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 3: {
      if(!ParseUintVField(buffer, msg.track_alias)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    default:
      break;
  }

  if (msg.current_pos < msg.MAX_FIELDS) {
    return false;
  }
  return true;
}





//
// Announce
//


qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqAnnounce& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqMessageType::ANNOUNCE)));
  buffer.PushLv(msg.track_namespace);
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(0)));
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer,
           MoqAnnounce &msg) {

  // read namespace
  if (msg.track_namespace.empty())
  {
    const auto val = buffer.DecodeBytes();
    if (!val) {
      return false;
    }
    msg.track_namespace = *val;
  }

  if (!msg.num_params) {
    const auto val = buffer.DecodeUintV();
    if (!val) {
      return false;
    }
    msg.num_params = *val;
  }

  // parse each param
  while (msg.num_params > 0) {
    if (!msg.current_param.type) {
      uint64_t type {0};
      if (!ParseUintVField(buffer, type)) {
        return false;
      }

      msg.current_param = {};
      msg.current_param.type = type;
    }

    // decode param_len:<bytes>
    auto param = buffer.DecodeBytes();
    if (!param) {
      return false;
    }

    msg.current_param.length = param->size();
    msg.current_param.value = param.value();
    msg.params.push_back(msg.current_param);
    msg.current_param = {};
    msg.num_params -= 1;
  }

  return true;
}

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqAnnounceOk& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqMessageType::ANNOUNCE_OK)));
  buffer.PushLv(msg.track_namespace);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqAnnounceOk &msg) {

  // read namespace
  if (msg.track_namespace.empty())
  {
    const auto val = buffer.DecodeBytes();
    if (!val) {
      return false;
    }
    msg.track_namespace = *val;
  }
  return true;
}


qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqAnnounceError& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqMessageType::ANNOUNCE_ERROR)));
  buffer.PushLv(msg.track_namespace.value());
  buffer.Push(qtransport::ToUintV(msg.err_code.value()));
  buffer.PushLv(msg.reason_phrase.value());
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqAnnounceError &msg) {

  // read namespace
  if (!msg.track_namespace)
  {
    const auto val = buffer.DecodeBytes();
    if (!val) {
      return false;
    }
    msg.track_namespace = *val;
  }

  if (!msg.err_code) {
    const auto val = buffer.DecodeUintV();
    if (!val) {
      return false;
    }

    msg.err_code = *val;
  }
  while (!msg.reason_phrase > 0) {
    auto reason = buffer.DecodeBytes();
    if (!reason) {
      return false;
    }
    msg.reason_phrase = reason;
  }

  return true;
}

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqUnannounce& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqMessageType::UNANNOUNCE)));
  buffer.PushLv(msg.track_namespace);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqUnannounce &msg) {

  // read namespace
  if (msg.track_namespace.empty())
  {
    const auto val = buffer.DecodeBytes();
    if (!val) {
      return false;
    }
    msg.track_namespace = *val;
  }
  return true;
}

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqAnnounceCancel& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqMessageType::ANNOUNCE_CANCEL)));
  buffer.PushLv(msg.track_namespace);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqAnnounceCancel &msg) {

  // read namespace
  if (msg.track_namespace.empty())
  {
    const auto val = buffer.DecodeBytes();
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

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqGoaway& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqMessageType::GOAWAY)));
  buffer.PushLv(msg.new_session_uri);
  return buffer;
}


bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqGoaway &msg) {

  const auto val = buffer.DecodeBytes();
  if (!val) {
    return false;
  }
  msg.new_session_uri = std::move(val.value());
  return true;
}

MessageBuffer &
operator<<(MessageBuffer &buffer, const MoqGoaway &msg) {
  buffer << static_cast<uint8_t>(MoqMessageType::GOAWAY);
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

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqObjectStream& msg){

  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqMessageType::OBJECT_STREAM)));
  buffer.Push(qtransport::ToUintV(msg.subscribe_id));
  buffer.Push(qtransport::ToUintV(msg.track_alias));
  buffer.Push(qtransport::ToUintV(msg.group_id));
  buffer.Push(qtransport::ToUintV(msg.object_id));
  buffer.Push(qtransport::ToUintV(msg.priority));
  buffer.PushLv(msg.payload);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqObjectStream &msg) {

  switch (msg.current_pos) {
    case 0: {
      if(!ParseUintVField(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      if(!ParseUintVField(buffer, msg.track_alias)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 2: {
      if(!ParseUintVField(buffer, msg.group_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 3: {
      if(!ParseUintVField(buffer, msg.object_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 4: {
      if(!ParseUintVField(buffer, msg.priority)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 5: {
      const auto val = buffer.DecodeBytes();
      if (!val) {
        return false;
      }
      msg.payload = std::move(val.value());
      msg.parse_completed = true;
      [[fallthrough]];
    }
    default:
      break;
  }

  if(!msg.parse_completed) {
    return false;
  }

  return true;
}


qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqObjectDatagram& msg){

  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqMessageType::OBJECT_DATAGRAM)));
  buffer.Push(qtransport::ToUintV(msg.subscribe_id));
  buffer.Push(qtransport::ToUintV(msg.track_alias));
  buffer.Push(qtransport::ToUintV(msg.group_id));
  buffer.Push(qtransport::ToUintV(msg.object_id));
  buffer.Push(qtransport::ToUintV(msg.priority));
  buffer.PushLv(msg.payload);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqObjectDatagram &msg) {

  switch (msg.current_pos) {
    case 0: {
      if(!ParseUintVField(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      if(!ParseUintVField(buffer, msg.track_alias)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 2: {
      if(!ParseUintVField(buffer, msg.group_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 3: {
      if(!ParseUintVField(buffer, msg.object_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 4: {
      if(!ParseUintVField(buffer, msg.priority)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 5: {
      const auto val = buffer.DecodeBytes();
      if (!val) {
        return false;
      }
      msg.payload = std::move(val.value());
      msg.parse_completed = true;
      [[fallthrough]];
    }
    default:
      break;
  }

  if(!msg.parse_completed) {
    return false;
  }

  return true;
}


qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqStreamHeaderTrack& msg){

  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqMessageType::STREAM_HEADER_TRACK)));
  buffer.Push(qtransport::ToUintV(msg.subscribe_id));
  buffer.Push(qtransport::ToUintV(msg.track_alias));
  buffer.Push(qtransport::ToUintV(msg.priority));
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqStreamHeaderTrack &msg) {

  switch (msg.current_pos) {
    case 0: {
      if(!ParseUintVField(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      if(!ParseUintVField(buffer, msg.track_alias)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 2: {
      if(!ParseUintVField(buffer, msg.priority)) {
        return false;
      }
      msg.current_pos += 1;
      msg.parse_completed = true;
      [[fallthrough]];
    }
    default:
      break;
  }

  if(!msg.parse_completed) {
    return false;
  }
  return true;
}

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqStreamTrackObject& msg){

  buffer.Push(qtransport::ToUintV(msg.group_id));
  buffer.Push(qtransport::ToUintV(msg.object_id));
  buffer.PushLv(msg.payload);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqStreamTrackObject &msg) {

  switch (msg.current_pos) {
    case 0: {
      if(!ParseUintVField(buffer, msg.group_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      if(!ParseUintVField(buffer, msg.object_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 2: {
      const auto val = buffer.DecodeBytes();
      if(!val) {
        return false;
      }
      msg.payload = std::move(val.value());
      msg.parse_completed = true;
      [[fallthrough]];
    }
    default:
      break;
  }

  if(!msg.parse_completed) {
    return false;
  }
  return true;
}



qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqStreamHeaderGroup& msg){

  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqMessageType::STREAM_HEADER_GROUP)));
  buffer.Push(qtransport::ToUintV(msg.subscribe_id));
  buffer.Push(qtransport::ToUintV(msg.track_alias));
  buffer.Push(qtransport::ToUintV(msg.group_id));
  buffer.Push(qtransport::ToUintV(msg.priority));
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqStreamHeaderGroup &msg) {
  switch (msg.current_pos) {
    case 0: {
      if(!ParseUintVField(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      if(!ParseUintVField(buffer, msg.track_alias)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 2: {
      if(!ParseUintVField(buffer, msg.group_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 3: {
      if(!ParseUintVField(buffer, msg.priority)) {
        return false;
      }
      msg.current_pos += 1;
      msg.parse_completed = true;
      [[fallthrough]];
    }
    default:
      break;
  }

  if(!msg.parse_completed) {
    return false;
  }

  return true;
}

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqStreamGroupObject& msg){

  buffer.Push(qtransport::ToUintV(msg.object_id));
  buffer.PushLv(msg.payload);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqStreamGroupObject &msg) {

  switch (msg.current_pos) {
    case 0: {
      if(!ParseUintVField(buffer, msg.object_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      const auto val = buffer.DecodeBytes();
      if (!val) {
        return false;
      }
      msg.payload = std::move(val.value());
      msg.parse_completed = true;
      [[fallthrough]];
    }
    default:
      break;
  }

  if(!msg.parse_completed) {
    return false;
  }

  return true;
}

// Client Setup message
qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqClientSetup& msg){

  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqMessageType::CLIENT_SETUP)));
  buffer.Push(qtransport::ToUintV(msg.supported_versions.size()));
  // versions
  for (const auto& ver: msg.supported_versions) {
    buffer.Push(qtransport::ToUintV(ver));
  }

  /// num params
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(2)));
  // role param
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(msg.role_parameter.type)));
  buffer.PushLv(msg.role_parameter.value);
  // endpoint_id param
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(ParameterType::EndpointId)));
  buffer.PushLv(msg.endpoint_id_parameter.value);

  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqClientSetup &msg) {
  switch (msg.current_pos) {
    case 0: {
      if(!ParseUintVField(buffer, msg.num_versions)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      while (msg.num_versions > 0) {
        uint64_t version{ 0 };
        if (!ParseUintVField(buffer, version)) {
          return false;
        }
        msg.supported_versions.push_back(version);
        msg.num_versions -= 1;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 2: {
      if(!msg.num_params.has_value()) {
        auto params = uint64_t {0};
        if (!ParseUintVField(buffer, params)) {
          return false;
        }
        msg.num_params = params;
      }
      while (msg.num_params > 0) {
          if (!msg.current_param.has_value()) {
              uint64_t type{ 0 };
              if (!ParseUintVField(buffer, type)) {
                  return false;
              }

              msg.current_param = MoqParameter{};
              msg.current_param->type = type;
          }

          auto param = buffer.DecodeBytes();
          if (!param) {
              return false;
          }
          msg.current_param->length = param->size();
          msg.current_param->value = param.value();

          switch (static_cast<ParameterType>(msg.current_param->type)) {
              case ParameterType::Role:
                  msg.role_parameter = std::move(msg.current_param.value());
                  break;
              case ParameterType::Path:
                  msg.path_parameter = std::move(msg.current_param.value());
                  break;
              case ParameterType::EndpointId:
                  msg.endpoint_id_parameter = std::move(msg.current_param.value());
                  break;
              default:
                  break;
          }

          msg.current_param = std::nullopt;
          msg.num_params.value() -= 1;
      }

      msg.parse_completed = true;
      [[fallthrough]];
    }
    default:
      break;
  }

  if (!msg.parse_completed) {
    return false;
  }

  return true;
}



// Server Setup message

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqServerSetup& msg){

  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqMessageType::SERVER_SETUP)));
  buffer.Push(qtransport::ToUintV(msg.selection_version));

  /// num params
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(2)));
  // role param
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(msg.role_parameter.type)));
  buffer.PushLv(msg.role_parameter.value);

  // endpoint_id param
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(ParameterType::EndpointId)));
  buffer.PushLv(msg.endpoint_id_parameter.value);

  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqServerSetup &msg) {
  switch (msg.current_pos) {
    case 0: {
      if(!ParseUintVField(buffer, msg.selection_version)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      if(!msg.num_params.has_value()) {
        auto params = uint64_t {0};
        if (!ParseUintVField(buffer, params)) {
          return false;
        }
        msg.num_params = params;
      }
      while (msg.num_params > 0) {
        if (!msg.current_param.has_value()) {
          uint64_t type {0};
          if (!ParseUintVField(buffer, type)) {
              return false;
          }

          msg.current_param = MoqParameter{};
          msg.current_param->type = type;
        }

        auto param = buffer.DecodeBytes();
        if (!param) {
          return false;
        }
        msg.current_param->length = param->size();
        msg.current_param->value = param.value();

        switch (static_cast<ParameterType>(msg.current_param->type)) {
            case ParameterType::Role:
                msg.role_parameter = std::move(msg.current_param.value());
                break;
            case ParameterType::Path:
                msg.path_parameter = std::move(msg.current_param.value());
                break;
            case ParameterType::EndpointId:
                msg.endpoint_id_parameter = std::move(msg.current_param.value());
                break;
            default:
                break;
        }

        msg.current_param = std::nullopt;
        msg.num_params.value() -= 1;
      }
      msg.parse_completed = true;
      [[fallthrough]];
    }
    default:
      break;
  }

  if (!msg.parse_completed) {
    return false;
  }

  return true;
}

}
