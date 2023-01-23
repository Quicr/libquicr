#include <string>

#include "encode.h"

using quicr::bytes;

namespace quicr::messages {
///
///  Subscribe Encode & Decode
///

uint64_t
transaction_id()
{
  std::default_random_engine engine(time(0));
  std::uniform_int_distribution distribution(1, 9);
  return distribution(engine);
}

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
  if (msg_type != static_cast<uint8_t>(MessageType::Subscribe)) {
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

}