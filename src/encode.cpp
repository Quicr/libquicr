#include <array>
#include <ctime>
#include <string>

#include <quicr/encode.h>

using quicr::bytes;

namespace quicr::messages {
/*===========================================================================*/
// Subscribe Encode & Decode
/*===========================================================================*/

uint64_t
transaction_id()
{
  std::default_random_engine engine(time(0));
  std::uniform_int_distribution distribution(1, 9);
  return distribution(engine);
}

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
    throw MessageBuffer::MessageTypeException("Message type for Subscribe object must "
                                              "be MessageType::Subscribe");
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
    throw MessageBuffer::MessageTypeException("Message type for Unsubscribe object "
                                              "must be MessageType::Unsubscribe");
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
    throw MessageBuffer::MessageTypeException("Message type for SubscribeResponse object "
                                              "must be MessageType::SubscribeResponse");
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
    throw MessageBuffer::MessageTypeException("Message type for SubscribeEnd object "
                                       "must be MessageType::SubscribeEnd");
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
  buffer << msg.mask;
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
  buffer << msg.mask;
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
  buffer >> msg.mask;
  buffer >> msg.payload;
  buffer >> msg.media_id;
  buffer >> msg.datagram_capable;

  return buffer;
}

MessageBuffer&
operator<<(MessageBuffer& buffer, const PublishIntentResponse& msg)
{
  buffer << static_cast<uint8_t>(msg.message_type);
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

  uint8_t response;
  buffer >> response;
  msg.response = static_cast<Response>(response);

  buffer >> msg.transaction_id;

  return buffer;
}

MessageBuffer&
operator<<(MessageBuffer& buffer, const Header& msg)
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
operator>>(MessageBuffer& buffer, Header& msg)
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
  buffer << msg.name;
  buffer << msg.payload;

  return buffer;
}

MessageBuffer&
operator<<(MessageBuffer& buffer, PublishIntentEnd&& msg)
{
  buffer << static_cast<uint8_t>(msg.message_type);
  buffer << msg.name;
  buffer << std::move(msg.payload);

  return buffer;
}

MessageBuffer&
operator>>(MessageBuffer& buffer, PublishIntentEnd& msg)
{
  uint8_t msg_type;
  buffer >> msg_type;
  msg.message_type = static_cast<MessageType>(msg_type);

  buffer >> msg.name;
  buffer >> msg.payload;

  return buffer;
}

messages::MessageBuffer&
operator<<(messages::MessageBuffer& msg, const quicr::Name& val)
{
  constexpr uint8_t size = quicr::Name::size();
  for (size_t i = 0; i < size; ++i)
    msg << val[i];

  return msg;
}

messages::MessageBuffer&
operator>>(messages::MessageBuffer& msg, quicr::Name& val)
{
  constexpr uint8_t size = quicr::Name::size();
  std::array<uint8_t, size> bytes;
  for (int i = 0; i < size; ++i)
    msg >> bytes[i];

  val = Name{ bytes.data(), size };

  return msg;
}

messages::MessageBuffer&
operator<<(messages::MessageBuffer& msg, const quicr::Namespace& val)
{
  msg << val.name() << val.length();
  return msg;
}

messages::MessageBuffer&
operator>>(messages::MessageBuffer& msg, quicr::Namespace& val)
{
  quicr::Name name_mask;
  uint8_t sig_bits;
  msg >> name_mask >> sig_bits;
  val = Namespace{ name_mask, sig_bits };

  return msg;
}

}
