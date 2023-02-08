#include <string>
#include <ctime>

#include "encode.h"

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
  // TODO: namespace encode and decode needs to be part of its own class
  buffer << static_cast<uint8_t>(msg.intent);
  buffer << msg.quicr_namespace;
  buffer << msg.transaction_id;
  buffer << static_cast<uint8_t>(MessageType::Subscribe);

  return buffer;
}

MessageBuffer&
operator>>(MessageBuffer& buffer, Subscribe& msg)
{
  uint8_t msg_type;
  buffer >> msg_type;
  if (msg_type != static_cast<uint8_t>(MessageType::Subscribe))
  {
    throw MessageBufferException("Message type for Subscribe object must be MessageType::Subscribe");
  }

  buffer >> msg.transaction_id;
  buffer >> msg.quicr_namespace;
  uint8_t intent = 0;
  buffer >> intent;
  msg.intent = static_cast<SubscribeIntent>(intent);

  return buffer;
}

MessageBuffer&
operator<<(MessageBuffer& buffer, const SubscribeResponse& msg)
{
  buffer << msg.quicr_namespace;
  buffer << msg.transaction_id;
  buffer << static_cast<uint8_t>(msg.response);
  buffer << static_cast<uint8_t>(MessageType::SubscribeResponse);

  return buffer;
}

MessageBuffer&
operator>>(MessageBuffer& buffer, SubscribeResponse& msg)
{
  uint8_t msg_type;
  buffer >> msg_type;
  if (msg_type != static_cast<uint8_t>(MessageType::SubscribeResponse))
  {
    throw MessageBufferException("Message type for SubscribeResponse object must be MessageType::SubscribeResponse");
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
  buffer << msg.payload;
  buffer << msg.media_id;
  buffer << static_cast<uint8_t>(msg.message_type);
  return buffer;
}

MessageBuffer&
operator>>(MessageBuffer& buffer, SubscribeEnd& msg)
{
  uint8_t msg_type;
  buffer >> msg_type;
  msg.message_type = static_cast<MessageType>(msg_type);

  buffer >> msg.media_id;
  buffer >> msg.payload;

  return buffer;
}

/*===========================================================================*/
// Publish Encode & Decode
/*===========================================================================*/

MessageBuffer&
operator<<(MessageBuffer& buffer, const PublishIntent& msg)
{
  buffer << msg.datagram_capable;
  buffer << msg.media_id;
  buffer << msg.payload;
  buffer << msg.mask;
  buffer << msg.quicr_namespace;
  buffer << msg.transaction_id;
  buffer << static_cast<uint8_t>(msg.message_type);
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
  buffer << msg.transaction_id;
  buffer << static_cast<uint8_t>(msg.response);
  buffer << static_cast<uint8_t>(msg.message_type);

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
  buffer << msg.flags;
  buffer << msg.offset_and_fin;
  buffer << msg.object_id;
  buffer << msg.group_id;
  buffer << msg.media_id;
  buffer << msg.name;

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
  buffer << msg.media_data;
  buffer << msg.media_data_length;
  buffer << static_cast<uint8_t>(msg.media_type);
  buffer << msg.header;
  buffer << static_cast<uint8_t>(MessageType::Publish);

  return buffer;
}

MessageBuffer&
operator>>(MessageBuffer& buffer, PublishDatagram& msg)
{
  uint8_t msg_type;
  buffer >> msg_type;
  if (msg_type != static_cast<uint8_t>(MessageType::Publish))
  {
    throw MessageBufferException("Message type for PublishDatagram object must be MessageType::Publish");
  }

  buffer >> msg.header;

  uint8_t media_type;
  buffer >> media_type;
  msg.media_type = static_cast<MediaType>(media_type);

  buffer >> msg.media_data_length;
  buffer >> msg.media_data;

  if (msg.media_data.size() != static_cast<size_t>(msg.media_data_length))
  {
    throw MessageBufferException("PublishDatagram size of decoded media data must match decoded length");
  }

  return buffer;
}

MessageBuffer&
operator<<(MessageBuffer& buffer, const PublishStream& msg)
{
  buffer << msg.media_data;
  buffer << msg.media_data_length;
  return buffer;
}

MessageBuffer&
operator>>(MessageBuffer& buffer, PublishStream& msg)
{
  buffer >> msg.media_data_length;
  buffer >> msg.media_data;
  if (msg.media_data.size() != static_cast<size_t>(msg.media_data_length))
  {
    throw MessageBufferException("PublishStream size of decoded media data must match decoded length");
  }

  return buffer;
}

MessageBuffer&
operator<<(MessageBuffer& buffer, const PublishIntentEnd& msg)
{
  buffer << msg.payload;
  buffer << msg.name;
  buffer << msg.name_length;
  buffer << static_cast<uint8_t>(msg.message_type);

  return buffer;
}

MessageBuffer&
operator>>(MessageBuffer& buffer, PublishIntentEnd& msg)
{
  uint8_t msg_type;
  buffer >> msg_type;
  msg.message_type = static_cast<MessageType>(msg_type);

  buffer >> msg.name_length;
  buffer >> msg.name;

  if (msg.name.size() != static_cast<size_t>(msg.name_length))
    throw MessageBufferException("PublishIntentEnd size of decoded media data must match decoded length");

  buffer >> msg.payload;

  return buffer;
}

}