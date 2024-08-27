#include "messages.h"

namespace moq::messages {

//
// Utility
//
static bool parse_uintV_field(qtransport::StreamBuffer<uint8_t> &buffer, uint64_t& field) {
  auto val = buffer.DecodeUintV();
  if (!val) {
    return false;
  }
  field = val.value();
  return true;
}


static bool parse_bytes_field(qtransport::StreamBuffer<uint8_t> &buffer, Bytes& field) {
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
           const MoqtParameter& param){

  buffer.Push(qtransport::ToUintV(param.type));
  buffer.Push(qtransport::ToUintV(param.length));
  if (param.length) {
    buffer.PushLv(param.value);
  }
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtParameter &param) {

  if(!parse_uintV_field(buffer, param.type)) {
    return false;
  }

  if(!parse_uintV_field(buffer, param.length)) {
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


MessageBuffer& operator<<(MessageBuffer &buffer, const MoqtParameter &param) {
  buffer <<  param.type;
  buffer << param.length;
  if (param.length) {
    buffer << param.value;
  }

  return buffer;
}

MessageBuffer& operator>>(MessageBuffer &buffer, MoqtParameter &param) {
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
                                              const MoqtTrackStatus& msg) {
    buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqtMessageType::TRACK_STATUS)));
    buffer.PushLv(msg.track_namespace);
    buffer.PushLv(msg.track_name);
    buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(msg.status_code)));
    buffer.Push(qtransport::ToUintV(msg.last_group_id));
    buffer.Push(qtransport::ToUintV(msg.last_object_id));

    return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtTrackStatus &msg) {

    switch (msg.current_pos) {
        case 0: {
            if(!parse_bytes_field(buffer, msg.track_namespace)) {
                return false;
            }
            msg.current_pos += 1;
            [[fallthrough]];
        }
        case 1: {
            if(!parse_bytes_field(buffer, msg.track_name)) {
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
            if (!parse_uintV_field(buffer, msg.last_group_id)) {
                return false;
            }
            msg.current_pos += 1;

            [[fallthrough]];
        }
        case 4: {
            if (!parse_uintV_field(buffer, msg.last_object_id)) {
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
                                              const MoqtTrackStatusRequest& msg){
    buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqtMessageType::TRACK_STATUS_REQUEST)));
    buffer.PushLv(msg.track_namespace);
    buffer.PushLv(msg.track_name);

    return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtTrackStatusRequest &msg) {

    switch (msg.current_pos) {
        case 0: {
            if (!parse_bytes_field(buffer, msg.track_namespace)) {
                return false;
            }
            msg.current_pos += 1;
            [[fallthrough]];
        }
        case 1: {
            if (!parse_bytes_field(buffer, msg.track_name)) {
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
           const MoqtSubscribe& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqtMessageType::SUBSCRIBE)));
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

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtSubscribe &msg) {

  switch (msg.current_pos) {
    case 0: {
      if(!parse_uintV_field(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      if(!parse_uintV_field(buffer, msg.track_alias)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 2: {
      if(!parse_bytes_field(buffer, msg.track_namespace)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 3: {
      if(!parse_bytes_field(buffer, msg.track_name)) {
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
        if (!parse_uintV_field(buffer, msg.start_group)) {
          return false;
        }
        msg.current_pos += 1;
      }
      [[fallthrough]];
    }
    case 6: {
      if (msg.filter_type == FilterType::AbsoluteStart
          || msg.filter_type == FilterType::AbsoluteRange) {
        if (!parse_uintV_field(buffer, msg.start_object)) {
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
        if (!parse_uintV_field(buffer, msg.end_group)) {
          return false;
        }
        msg.current_pos += 1;
      }

      [[fallthrough]];
    }
    case 8: {
      if (msg.filter_type == FilterType::AbsoluteRange) {
        if (!parse_uintV_field(buffer, msg.end_object)) {
          return false;
        }
        msg.current_pos += 1;
      }
      [[fallthrough]];
    }
    case 9: {
      if (!msg.num_params.has_value()) {
        uint64_t num = 0;
        if (!parse_uintV_field(buffer, num)) {
          return false;
        }

        msg.num_params = num;
      }
      // parse each param
      while (*msg.num_params > 0) {
        if (!msg.current_param.has_value()) {
          uint64_t type {0};
          if (!parse_uintV_field(buffer, type)) {
            return false;
          }

          msg.current_param = MoqtParameter{};
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
           const MoqtUnsubscribe& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqtMessageType::UNSUBSCRIBE)));
  buffer.Push(qtransport::ToUintV(msg.subscribe_id));
  return buffer;
}


bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtUnsubscribe &msg) {
  return parse_uintV_field(buffer, msg.subscribe_id);
}

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqtSubscribeDone& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqtMessageType::SUBSCRIBE_DONE)));
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


bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtSubscribeDone &msg) {

  switch (msg.current_pos) {
    case 0: {
      if(!parse_uintV_field(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      if(!parse_uintV_field(buffer, msg.status_code)) {
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
      if(!parse_uintV_field(buffer, msg.final_group_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 5: {
      if(!parse_uintV_field(buffer, msg.final_object_id)) {
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
           const MoqtSubscribeOk& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqtMessageType::SUBSCRIBE_OK)));
  buffer.Push(qtransport::ToUintV(msg.subscribe_id));
  buffer.Push(qtransport::ToUintV(msg.expires));
  msg.content_exists ? buffer.Push(static_cast<uint8_t>(1)) : buffer.Push(static_cast<uint8_t>(0));
  if(msg.content_exists) {
    buffer.Push(qtransport::ToUintV(msg.largest_group));
    buffer.Push(qtransport::ToUintV(msg.largest_object));
  }
  return buffer;
}


bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtSubscribeOk &msg) {

  switch (msg.current_pos) {
    case 0:
    {
      if(!parse_uintV_field(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      if(!parse_uintV_field(buffer, msg.expires)) {
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
      if(!parse_uintV_field(buffer, msg.largest_group)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 4: {
      if(!parse_uintV_field(buffer, msg.largest_object)) {
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
           const MoqtSubscribeError& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqtMessageType::SUBSCRIBE_ERROR)));
  buffer.Push(qtransport::ToUintV(msg.subscribe_id));
  buffer.Push(qtransport::ToUintV(msg.err_code));
  buffer.PushLv(msg.reason_phrase);
  buffer.Push(qtransport::ToUintV(msg.track_alias));
  return buffer;
}


bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtSubscribeError &msg) {

  switch (msg.current_pos) {
    case 0:
    {
      if(!parse_uintV_field(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      if(!parse_uintV_field(buffer, msg.err_code)) {
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
      if(!parse_uintV_field(buffer, msg.track_alias)) {
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
           const MoqtAnnounce& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqtMessageType::ANNOUNCE)));
  buffer.PushLv(msg.track_namespace);
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(0)));
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer,
           MoqtAnnounce &msg) {

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
      if (!parse_uintV_field(buffer, type)) {
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
           const MoqtAnnounceOk& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqtMessageType::ANNOUNCE_OK)));
  buffer.PushLv(msg.track_namespace);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtAnnounceOk &msg) {

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
           const MoqtAnnounceError& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqtMessageType::ANNOUNCE_ERROR)));
  buffer.PushLv(msg.track_namespace.value());
  buffer.Push(qtransport::ToUintV(msg.err_code.value()));
  buffer.PushLv(msg.reason_phrase.value());
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtAnnounceError &msg) {

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
           const MoqtUnannounce& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqtMessageType::UNANNOUNCE)));
  buffer.PushLv(msg.track_namespace);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtUnannounce &msg) {

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
           const MoqtAnnounceCancel& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqtMessageType::ANNOUNCE_CANCEL)));
  buffer.PushLv(msg.track_namespace);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtAnnounceCancel &msg) {

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
           const MoqtGoaway& msg){
  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqtMessageType::GOAWAY)));
  buffer.PushLv(msg.new_session_uri);
  return buffer;
}


bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtGoaway &msg) {

  const auto val = buffer.DecodeBytes();
  if (!val) {
    return false;
  }
  msg.new_session_uri = std::move(val.value());
  return true;
}

MessageBuffer &
operator<<(MessageBuffer &buffer, const MoqtGoaway &msg) {
  buffer << static_cast<uint8_t>(MoqtMessageType::GOAWAY);
  buffer << msg.new_session_uri;
  return buffer;
}

MessageBuffer &
operator>>(MessageBuffer &buffer, MoqtGoaway &msg) {
  buffer >> msg.new_session_uri;
  return buffer;
}

//
// Object
//

qtransport::StreamBuffer<uint8_t>& operator<<(qtransport::StreamBuffer<uint8_t>& buffer,
           const MoqtObjectStream& msg){

  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqtMessageType::OBJECT_STREAM)));
  buffer.Push(qtransport::ToUintV(msg.subscribe_id));
  buffer.Push(qtransport::ToUintV(msg.track_alias));
  buffer.Push(qtransport::ToUintV(msg.group_id));
  buffer.Push(qtransport::ToUintV(msg.object_id));
  buffer.Push(qtransport::ToUintV(msg.priority));
  buffer.PushLv(msg.payload);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtObjectStream &msg) {

  switch (msg.current_pos) {
    case 0: {
      if(!parse_uintV_field(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      if(!parse_uintV_field(buffer, msg.track_alias)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 2: {
      if(!parse_uintV_field(buffer, msg.group_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 3: {
      if(!parse_uintV_field(buffer, msg.object_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 4: {
      if(!parse_uintV_field(buffer, msg.priority)) {
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
           const MoqtObjectDatagram& msg){

  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqtMessageType::OBJECT_DATAGRAM)));
  buffer.Push(qtransport::ToUintV(msg.subscribe_id));
  buffer.Push(qtransport::ToUintV(msg.track_alias));
  buffer.Push(qtransport::ToUintV(msg.group_id));
  buffer.Push(qtransport::ToUintV(msg.object_id));
  buffer.Push(qtransport::ToUintV(msg.priority));
  buffer.PushLv(msg.payload);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtObjectDatagram &msg) {

  switch (msg.current_pos) {
    case 0: {
      if(!parse_uintV_field(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      if(!parse_uintV_field(buffer, msg.track_alias)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 2: {
      if(!parse_uintV_field(buffer, msg.group_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 3: {
      if(!parse_uintV_field(buffer, msg.object_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 4: {
      if(!parse_uintV_field(buffer, msg.priority)) {
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
           const MoqtStreamHeaderTrack& msg){

  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqtMessageType::STREAM_HEADER_TRACK)));
  buffer.Push(qtransport::ToUintV(msg.subscribe_id));
  buffer.Push(qtransport::ToUintV(msg.track_alias));
  buffer.Push(qtransport::ToUintV(msg.priority));
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtStreamHeaderTrack &msg) {

  switch (msg.current_pos) {
    case 0: {
      if(!parse_uintV_field(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      if(!parse_uintV_field(buffer, msg.track_alias)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 2: {
      if(!parse_uintV_field(buffer, msg.priority)) {
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
           const MoqtStreamTrackObject& msg){

  buffer.Push(qtransport::ToUintV(msg.group_id));
  buffer.Push(qtransport::ToUintV(msg.object_id));
  buffer.PushLv(msg.payload);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtStreamTrackObject &msg) {

  switch (msg.current_pos) {
    case 0: {
      if(!parse_uintV_field(buffer, msg.group_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      if(!parse_uintV_field(buffer, msg.object_id)) {
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
           const MoqtStreamHeaderGroup& msg){

  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqtMessageType::STREAM_HEADER_GROUP)));
  buffer.Push(qtransport::ToUintV(msg.subscribe_id));
  buffer.Push(qtransport::ToUintV(msg.track_alias));
  buffer.Push(qtransport::ToUintV(msg.group_id));
  buffer.Push(qtransport::ToUintV(msg.priority));
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtStreamHeaderGroup &msg) {
  switch (msg.current_pos) {
    case 0: {
      if(!parse_uintV_field(buffer, msg.subscribe_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      if(!parse_uintV_field(buffer, msg.track_alias)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 2: {
      if(!parse_uintV_field(buffer, msg.group_id)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 3: {
      if(!parse_uintV_field(buffer, msg.priority)) {
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
           const MoqtStreamGroupObject& msg){

  buffer.Push(qtransport::ToUintV(msg.object_id));
  buffer.PushLv(msg.payload);
  return buffer;
}

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtStreamGroupObject &msg) {

  switch (msg.current_pos) {
    case 0: {
      if(!parse_uintV_field(buffer, msg.object_id)) {
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
           const MoqtClientSetup& msg){

  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqtMessageType::CLIENT_SETUP)));
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

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtClientSetup &msg) {
  switch (msg.current_pos) {
    case 0: {
      if(!parse_uintV_field(buffer, msg.num_versions)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      while (msg.num_versions > 0) {
        uint64_t version{ 0 };
        if (!parse_uintV_field(buffer, version)) {
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
        if (!parse_uintV_field(buffer, params)) {
          return false;
        }
        msg.num_params = params;
      }
      while (msg.num_params > 0) {
          if (!msg.current_param.has_value()) {
              uint64_t type{ 0 };
              if (!parse_uintV_field(buffer, type)) {
                  return false;
              }

              msg.current_param = MoqtParameter{};
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
           const MoqtServerSetup& msg){

  buffer.Push(qtransport::ToUintV(static_cast<uint64_t>(MoqtMessageType::SERVER_SETUP)));
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

bool operator>>(qtransport::StreamBuffer<uint8_t> &buffer, MoqtServerSetup &msg) {
  switch (msg.current_pos) {
    case 0: {
      if(!parse_uintV_field(buffer, msg.selection_version)) {
        return false;
      }
      msg.current_pos += 1;
      [[fallthrough]];
    }
    case 1: {
      if(!msg.num_params.has_value()) {
        auto params = uint64_t {0};
        if (!parse_uintV_field(buffer, params)) {
          return false;
        }
        msg.num_params = params;
      }
      while (msg.num_params > 0) {
        if (!msg.current_param.has_value()) {
          uint64_t type {0};
          if (!parse_uintV_field(buffer, type)) {
              return false;
          }

          msg.current_param = MoqtParameter{};
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