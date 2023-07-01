#include <array>
#include <ctime>
#include <random>
#include <string>

#include <quicr/encode.h>

using quicr::bytes;

namespace quicr::messages {

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
operator<<(MessageBuffer& msg, const quicr::uintVar_t& v)
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
operator>>(MessageBuffer& msg, quicr::uintVar_t& v)
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
    uint16_t val = (((uint16_t)byte[1] & 0x3F) << 8) + ((uint16_t)byte[0] << 0);
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
operator<<(MessageBuffer& msg, const std::vector<uint8_t>& val)
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
    throw MessageBuffer::MessageTypeException(
      "Message type for PublishDatagram object must be MessageType::Publish");
  }

  buffer >> msg.header;

  uint8_t media_type;
  buffer >> media_type;
  msg.media_type = static_cast<MediaType>(media_type);

  buffer >> msg.media_data_length;
  buffer >> msg.media_data;

  if (msg.media_data.size() != static_cast<size_t>(msg.media_data_length)) {
    throw MessageBuffer::LengthException(
      "PublishDatagram size of decoded media data must match decoded length");
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
    throw MessageBuffer::LengthException(
      "PublishStream size of decoded media data must match decoded length");
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

}
