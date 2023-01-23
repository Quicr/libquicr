#include <string>

#include "encode.h"

using quicr::bytes;

namespace quicr::messages
{
/*===========================================================================*/
// Subscribe Encode & Decode
/*===========================================================================*/

void
operator<<(MessageBuffer& buffer, const Subscribe& msg)
{
  // TODO: namespace encode and decode needs to be part of its own class
  buffer << static_cast<uint8_t>(msg.intent);
  buffer << static_cast<uint8_t>(msg.quicr_namespace.mask);
  buffer << msg.quicr_namespace.hi;
  buffer << msg.quicr_namespace.low;
  buffer << msg.transaction_id;
  buffer << static_cast<uint8_t>(MessageType::Subscribe);
}

bool
operator>>(MessageBuffer& buffer, Subscribe& msg)
{
  uint8_t msg_type;
  buffer >> msg_type;
  if (msg_type != static_cast<uint8_t>(MessageType::Subscribe))
  {
    return false;
  }

  buffer >> msg.transaction_id;
  buffer >> msg.quicr_namespace.low;
  buffer >> msg.quicr_namespace.hi;
  uint8_t mask = 0;
  buffer >> mask;
  msg.quicr_namespace.mask = mask;
  uint8_t intent = 0;
  buffer >> intent;
  msg.intent = static_cast<SubscribeIntent>(intent);
  return true;
}


void
operator<<(MessageBuffer& buffer, const SubscribeResponse& msg)
{
  buffer << msg.media_id;
  buffer << msg.transaction_id;
  buffer << static_cast<uint8_t>(msg.response);
  buffer << static_cast<uint8_t>(msg.message_type);
}

bool
operator>>(MessageBuffer& buffer, SubscribeResponse& msg)
{
  uint8_t msg_type;
  buffer >> msg_type;
  msg.message_type = static_cast<MessageType>(msg_type);
  
  uint8_t response;
  buffer >> response;
  msg.response = static_cast<Response>(response);

  buffer >> msg.transaction_id;
  buffer >> msg.media_id;

  return true;
}


void
operator<<(MessageBuffer& buffer, const SubscribeEnd& msg)
{
  buffer << msg.payload;
  buffer << msg.media_id;
  buffer << static_cast<uint8_t>(msg.message_type);
}

bool
operator>>(MessageBuffer& buffer, SubscribeEnd& msg)
{
  uint8_t msg_type;
  buffer >> msg_type;
  msg.message_type = static_cast<MessageType>(msg_type);

  buffer >> msg.media_id;
  buffer >> msg.payload;

  return true;
}

/*===========================================================================*/
// Publish Encode & Decode
/*===========================================================================*/

void
operator<<(MessageBuffer& buffer, const DatagramHeader& msg)
{
  buffer << msg.flags;
  buffer << msg.offset_and_fin;
  buffer << msg.object_id;
  buffer << msg.group_id;
  buffer << msg.media_id;
}

bool
operator>>(MessageBuffer& buffer, DatagramHeader& msg)
{
  buffer >> msg.media_id;
  buffer >> msg.group_id;
  buffer >> msg.object_id;
  buffer >> msg.offset_and_fin;
  buffer >> msg.flags;

  return true;
}

void
operator<<(MessageBuffer& buffer, const PublishDatagram& msg)
{
  buffer << msg.media_data;
  buffer << msg.media_data_length;
  buffer << static_cast<uint8_t>(msg.media_type);
  buffer << msg.datagram_header;
  buffer << static_cast<uint8_t>(MessageType::Publish);
}

bool
operator>>(MessageBuffer& buffer, PublishDatagram& msg)
{
  uint8_t msg_type;
  buffer >> msg_type;
  if (msg_type != static_cast<uint8_t>(MessageType::Publish))
  {
    return false;
  }

  buffer >> msg.datagram_header;

  uint8_t media_type;
  buffer >> media_type;
  msg.media_type = static_cast<MediaType>(media_type);

  buffer >> msg.media_data_length;
  buffer >> msg.media_data;

  if (msg.media_data.size() != static_cast<size_t>(msg.media_data_length))
  {
    return false;
  }

  return true;
}

}