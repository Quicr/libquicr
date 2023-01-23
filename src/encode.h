#pragma once
#include <random>
#include <string>
#include <vector>

#include "message_buffer.h"
#include <quicr/quicr_common.h>

/* Utilties to encode and decode protocol messages */
namespace quicr::messages {

///
/// Common
///
enum struct MessageType : uint8_t
{
  Unknown = 0,
  Subscribe = 1,
};

uint64_t
transaction_id();

///
/// Message Types
///
struct Subscribe
{
  uint8_t version;
  uint64_t transaction_id;
  QUICRNamespace quicr_namespace;
  SubscribeIntent intent;
};

void
operator<<(MessageBuffer& buffer, const Subscribe& msg);
bool
operator>>(MessageBuffer& buffer, Subscribe& msg);

}