#include <quicr/encode.h>

#include <array>
#include <ctime>
#include <map>
#include <random>
#include <string>

using quicr::bytes;

namespace quicr::messages {

namespace {
std::string
to_string(MessageType type)
{
// clang-format off
#define ENUM_MAP_ENTRY(n) { n, #n }
  // clang-format on
  static std::map<MessageType, std::string> message_type_name = {
    ENUM_MAP_ENTRY(MessageType::Unknown),
    ENUM_MAP_ENTRY(MessageType::Subscribe),
    ENUM_MAP_ENTRY(MessageType::SubscribeResponse),
    ENUM_MAP_ENTRY(MessageType::SubscribeEnd),
    ENUM_MAP_ENTRY(MessageType::Unsubscribe),
    ENUM_MAP_ENTRY(MessageType::Publish),
    ENUM_MAP_ENTRY(MessageType::PublishIntent),
    ENUM_MAP_ENTRY(MessageType::PublishIntentResponse),
    ENUM_MAP_ENTRY(MessageType::PublishIntentEnd),
    ENUM_MAP_ENTRY(MessageType::Fetch),
  };
#undef ENUM_MAP_ENTRY
  return message_type_name[type];
}
}

MessageTypeException::MessageTypeException(MessageType type,
                                           MessageType expected_type)
  : MessageBuffer::ReadException(
      to_string(type) +
      " does not match expected message type: " + to_string(expected_type))
{
}

MessageTypeException::MessageTypeException(uint8_t type,
                                           MessageType expected_type)
  : MessageTypeException(static_cast<MessageType>(type), expected_type)
{
}

uint64_t
create_transaction_id()
{
  std::default_random_engine engine(time(0));
  std::uniform_int_distribution distribution(1, 9);
  return distribution(engine);
}

/*===========================================================================*/
// Common Encode & Decode
/*===========================================================================*/

MessageBuffer&
operator<<(MessageBuffer& msg, const Namespace& value)
{
  // TODO: These should be swapped around.
  return msg << value.name() << value.length();
}

MessageBuffer&
operator>>(MessageBuffer& msg, Namespace& value)
{
  Name name;
  uint8_t length;
  // TODO: These should be swapped around.
  msg >> name >> length;

  value = { name, length };
  return msg;
}

MessageBuffer&
operator<<(MessageBuffer& msg, const uintVar_t& v)
{
  uint64_t val = v;

  if (val < ((uint64_t)1 << 7)) {
    msg.push(uint8_t(((val >> 0) & 0x7F)) | 0x00);
    return msg;
  }

  if (val < ((uint64_t)1 << 14)) {
    msg.push(uint8_t(((val >> 8) & 0x3F) | 0x80));
    msg.push(uint8_t((val >> 0) & 0xFF));
    return msg;
  }

  if (val < ((uint64_t)1 << 29)) {
    msg.push(uint8_t(((val >> 24) & 0x1F) | 0x80 | 0x40));
    msg.push(uint8_t((val >> 16) & 0xFF));
    msg.push(uint8_t((val >> 8) & 0xFF));
    msg.push(uint8_t((val >> 0) & 0xFF));
    return msg;
  }

  msg.push(uint8_t(((val >> 56) & 0x0F) | 0x80 | 0x40 | 0x20));
  msg.push(uint8_t((val >> 48) & 0xFF));
  msg.push(uint8_t((val >> 40) & 0xFF));
  msg.push(uint8_t((val >> 32) & 0xFF));
  msg.push(uint8_t((val >> 24) & 0xFF));
  msg.push(uint8_t((val >> 16) & 0xFF));
  msg.push(uint8_t((val >> 8) & 0xFF));
  msg.push(uint8_t((val >> 0) & 0xFF));

  return msg;
}

MessageBuffer&
operator>>(MessageBuffer& msg, uintVar_t& v)
{
  uint8_t byte[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
  uint8_t first = msg.front();

  if ((first & (0x80)) == 0) {
    msg >> byte[0];
    uint8_t val = ((byte[0] & 0x7F) << 0);
    v = val;
    return msg;
  }

  if ((first & (0x80 | 0x40)) == 0x80) {
    msg >> byte[1];
    msg >> byte[0];
    uint16_t val = ((uint16_t(byte[1]) & 0x3F) << 8) + (uint16_t(byte[0]) << 0);
    v = val;
    return msg;
  }

  if ((first & (0x80 | 0x40 | 0x20)) == (0x80 | 0x40)) {
    msg >> byte[3];
    msg >> byte[2];
    msg >> byte[1];
    msg >> byte[0];
    uint32_t val = ((uint32_t)(byte[3] & 0x1F) << 24) +
                   ((uint32_t)byte[2] << 16) + ((uint32_t)byte[1] << 8) +
                   ((uint32_t)byte[0] << 0);
    v = val;
    return msg;
  }

  msg >> byte[7];
  msg >> byte[6];
  msg >> byte[5];
  msg >> byte[4];
  msg >> byte[3];
  msg >> byte[2];
  msg >> byte[1];
  msg >> byte[0];
  uint64_t val = ((uint64_t)(byte[7] & 0x0F) << 56) +
                 ((uint64_t)(byte[6]) << 48) + ((uint64_t)(byte[5]) << 40) +
                 ((uint64_t)(byte[4]) << 32) + ((uint64_t)(byte[3]) << 24) +
                 ((uint64_t)(byte[2]) << 16) + ((uint64_t)(byte[1]) << 8) +
                 ((uint64_t)(byte[0]) << 0);
  v = val;
  return msg;
}

MessageBuffer&
operator<<(MessageBuffer& msg, std::span<const uint8_t> val)
{
  msg << static_cast<uintVar_t>(val.size());
  msg.push(val);
  return msg;
}

MessageBuffer&
operator<<(MessageBuffer& msg, std::vector<uint8_t>&& val)
{
  msg << static_cast<uintVar_t>(val.size());
  msg.push(std::move(val));
  return msg;
}

MessageBuffer&
operator>>(MessageBuffer& msg, std::vector<uint8_t>& val)
{
  uintVar_t vec_size = 0;
  msg >> vec_size;

  val = msg.pop_front(vec_size);
  return msg;
}

/*===========================================================================*/
// Subscribe Encode & Decode
/*===========================================================================*/

MessageBuffer&
operator<<(MessageBuffer& buffer, const Subscribe& msg)
{
  buffer << static_cast<uint8_t>(MessageType::Subscribe);
  buffer << msg.transaction_id;
  buffer << msg.quicr_namespace;
  buffer << static_cast<uint8_t>(msg.intent);

  return buffer;
}

MessageBuffer&
operator>>(MessageBuffer& buffer, Subscribe& msg)
{
  uint8_t msg_type;
  buffer >> msg_type;
  if (msg_type != static_cast<uint8_t>(MessageType::Subscribe)) {
    throw MessageTypeException(msg_type, MessageType::Subscribe);
  }

  buffer >> msg.transaction_id;
  buffer >> msg.quicr_namespace;
  uint8_t intent = 0;
  buffer >> intent;
  msg.intent = static_cast<SubscribeIntent>(intent);

  return buffer;
}

MessageBuffer&
operator<<(MessageBuffer& buffer, const Unsubscribe& msg)
{
  buffer << static_cast<uint8_t>(MessageType::Unsubscribe);
  buffer << msg.quicr_namespace;

  return buffer;
}

MessageBuffer&
operator>>(MessageBuffer& buffer, Unsubscribe& msg)
{
  uint8_t msg_type;
  buffer >> msg_type;
  if (msg_type != static_cast<uint8_t>(MessageType::Unsubscribe)) {
    throw MessageTypeException(msg_type, MessageType::Unsubscribe);
  }

  buffer >> msg.quicr_namespace;

  return buffer;
}

MessageBuffer&
operator<<(MessageBuffer& buffer, const SubscribeResponse& msg)
{
  buffer << static_cast<uint8_t>(MessageType::SubscribeResponse);
  buffer << static_cast<uint8_t>(msg.response);
  buffer << msg.transaction_id;
  buffer << msg.quicr_namespace;

  return buffer;
}

MessageBuffer&
operator>>(MessageBuffer& buffer, SubscribeResponse& msg)
{
  uint8_t msg_type;
  buffer >> msg_type;
  if (msg_type != static_cast<uint8_t>(MessageType::SubscribeResponse)) {
    throw MessageTypeException(msg_type, MessageType::SubscribeResponse);
  }

  uint8_t response;
  buffer >> response;
  msg.response = static_cast<SubscribeResult::SubscribeStatus>(response);

  buffer >> msg.transaction_id;
  buffer >> msg.quicr_namespace;

  return buffer;
}

MessageBuffer&
operator<<(MessageBuffer& buffer, const SubscribeEnd& msg)
{
  buffer << static_cast<uint8_t>(MessageType::SubscribeEnd);
  buffer << static_cast<uint8_t>(msg.reason);
  buffer << msg.quicr_namespace;

  return buffer;
}

MessageBuffer&
operator>>(MessageBuffer& buffer, SubscribeEnd& msg)
{
  uint8_t msg_type;
  buffer >> msg_type;
  if (msg_type != static_cast<uint8_t>(MessageType::SubscribeEnd)) {
    throw MessageTypeException(msg_type, MessageType::SubscribeEnd);
  }

  uint8_t reason;
  buffer >> reason;
  msg.reason = static_cast<SubscribeResult::SubscribeStatus>(reason);

  buffer >> msg.quicr_namespace;

  return buffer;
}

/*===========================================================================*/
// Publish Encode & Decode
/*===========================================================================*/

MessageBuffer&
operator<<(MessageBuffer& buffer, const PublishIntent& msg)
{
  buffer << static_cast<uint8_t>(msg.message_type);
  buffer << msg.transaction_id;
  buffer << msg.quicr_namespace;
  buffer << msg.payload;
  buffer << msg.media_id;
  buffer << msg.datagram_capable;
  return buffer;
}

MessageBuffer&
operator<<(MessageBuffer& buffer, PublishIntent&& msg)
{
  buffer << static_cast<uint8_t>(msg.message_type);
  buffer << msg.transaction_id;
  buffer << msg.quicr_namespace;
  buffer << std::move(msg.payload);
  buffer << msg.media_id;
  buffer << msg.datagram_capable;
  return buffer;
}

MessageBuffer&
operator>>(MessageBuffer& buffer, PublishIntent& msg)
{
  uint8_t msg_type;
  buffer >> msg_type;
  msg.message_type = static_cast<MessageType>(msg_type);

  buffer >> msg.transaction_id;
  buffer >> msg.quicr_namespace;
  buffer >> msg.payload;
  buffer >> msg.media_id;
  buffer >> msg.datagram_capable;

  return buffer;
}

MessageBuffer&
operator<<(MessageBuffer& buffer, const PublishIntentResponse& msg)
{
  buffer << static_cast<uint8_t>(msg.message_type);
  buffer << msg.quicr_namespace;
  buffer << static_cast<uint8_t>(msg.response);
  buffer << msg.transaction_id;

  return buffer;
}

MessageBuffer&
operator>>(MessageBuffer& buffer, PublishIntentResponse& msg)
{
  uint8_t msg_type;
  buffer >> msg_type;
  msg.message_type = static_cast<MessageType>(msg_type);

  buffer >> msg.quicr_namespace;

  uint8_t response;
  buffer >> response;
  msg.response = static_cast<Response>(response);

  buffer >> msg.transaction_id;

  return buffer;
}

MessageBuffer&
operator<<(MessageBuffer& buffer, const PublishDatagram::Header& msg)
{
  buffer << msg.name;
  buffer << msg.media_id;
  buffer << msg.group_id;
  buffer << msg.object_id;
  buffer << msg.offset_and_fin;
  buffer << msg.flags;

  return buffer;
}

MessageBuffer&
operator>>(MessageBuffer& buffer, PublishDatagram::Header& msg)
{
  buffer >> msg.name;
  buffer >> msg.media_id;
  buffer >> msg.group_id;
  buffer >> msg.object_id;
  buffer >> msg.offset_and_fin;
  buffer >> msg.flags;

  return buffer;
}

MessageBuffer&
operator<<(MessageBuffer& buffer, const PublishDatagram& msg)
{
  buffer << static_cast<uint8_t>(MessageType::Publish);
  buffer << msg.header;
  buffer << static_cast<uint8_t>(msg.media_type);
  buffer << msg.media_data_length;
  buffer << msg.media_data;

  return buffer;
}

MessageBuffer&
operator<<(MessageBuffer& buffer, PublishDatagram&& msg)
{
  buffer << static_cast<uint8_t>(MessageType::Publish);
  buffer << std::move(msg.header);
  buffer << static_cast<uint8_t>(msg.media_type);
  buffer << msg.media_data_length;
  buffer << std::move(msg.media_data);

  return buffer;
}

MessageBuffer&
operator>>(MessageBuffer& buffer, PublishDatagram& msg)
{
  uint8_t msg_type;
  buffer >> msg_type;
  if (msg_type != static_cast<uint8_t>(MessageType::Publish)) {
    throw MessageTypeException(msg_type, MessageType::Publish);
  }

  buffer >> msg.header;

  uint8_t media_type;
  buffer >> media_type;
  msg.media_type = static_cast<MediaType>(media_type);

  buffer >> msg.media_data_length;
  buffer >> msg.media_data;

  if (msg.media_data.size() != static_cast<size_t>(msg.media_data_length)) {
    throw MessageBuffer::LengthException(msg.media_data.size(),
                                         msg.media_data_length);
  }

  return buffer;
}

MessageBuffer&
operator<<(MessageBuffer& buffer, const PublishStream& msg)
{
  buffer << msg.media_data_length;
  buffer << msg.media_data;
  return buffer;
}

MessageBuffer&
operator<<(MessageBuffer& buffer, PublishStream&& msg)
{
  buffer << msg.media_data_length;
  buffer << std::move(msg.media_data);
  return buffer;
}

MessageBuffer&
operator>>(MessageBuffer& buffer, PublishStream& msg)
{
  buffer >> msg.media_data_length;
  buffer >> msg.media_data;
  if (msg.media_data.size() != static_cast<size_t>(msg.media_data_length)) {
    throw MessageBuffer::LengthException(msg.media_data.size(),
                                         msg.media_data_length);
  }

  return buffer;
}

MessageBuffer&
operator<<(MessageBuffer& buffer, const PublishIntentEnd& msg)
{
  buffer << static_cast<uint8_t>(msg.message_type);
  buffer << msg.quicr_namespace;
  buffer << msg.payload;

  return buffer;
}

MessageBuffer&
operator<<(MessageBuffer& buffer, PublishIntentEnd&& msg)
{
  buffer << static_cast<uint8_t>(msg.message_type);
  buffer << msg.quicr_namespace;
  buffer << std::move(msg.payload);

  return buffer;
}

MessageBuffer&
operator>>(MessageBuffer& buffer, PublishIntentEnd& msg)
{
  uint8_t msg_type;
  buffer >> msg_type;
  msg.message_type = static_cast<MessageType>(msg_type);

  buffer >> msg.quicr_namespace;
  buffer >> msg.payload;

  return buffer;
}

///
/// Fetch
///

MessageBuffer&
operator<<(MessageBuffer& buffer, const Fetch& msg)
{
  buffer << static_cast<uint8_t>(MessageType::Fetch);
  buffer << msg.transaction_id;
  buffer << msg.name;
  return buffer;
}

MessageBuffer&
operator>>(MessageBuffer& buffer, Fetch& msg)
{
  uint8_t msg_type;
  buffer >> msg_type;
  if (msg_type != static_cast<uint8_t>(MessageType::Fetch)) {
    throw MessageTypeException(msg_type, MessageType::Fetch);
  }

  buffer >> msg.transaction_id;
  buffer >> msg.name;
  return buffer;
}

}
