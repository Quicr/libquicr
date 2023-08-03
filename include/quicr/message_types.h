#pragma once

#include <quicr/name.h>
#include <quicr/namespace.h>
#include <quicr/quicr_common.h>
#include <quicr/uvarint.h>

#include <string>
#include <variant>
#include <vector>

namespace quicr::messages {
/*===========================================================================*/
// Common
/*===========================================================================*/

/**
 * Type of message being sent/received
 */
enum class MessageType : uint8_t
{
  Unknown,
  Subscribe,
  SubscribeResponse,
  SubscribeEnd,
  Unsubscribe,
  Publish,
  PublishIntent,
  PublishIntentResponse,
  PublishIntentEnd,
  Fetch,

  PeerMsg=128

};

/*===========================================================================*/
// Subscribe Message Types
/*===========================================================================*/

struct Subscribe
{
  uint8_t version;
  uint64_t transaction_id;
  Namespace quicr_namespace;
  SubscribeIntent intent;
};

struct Unsubscribe
{
  uint8_t version;
  Namespace quicr_namespace;
};

// TODO: Add missing fields:
struct SubscribeResponse
{
  Namespace quicr_namespace;
  SubscribeResult::SubscribeStatus response;
  uint64_t transaction_id;
  // * - signature(32)
  // * - [Reason Phrase Length (i)],
  // * - [Reason Phrase (..)],
  // * - [redirect_relay_url_length(i)],
  // * - [redirect_relay_url(…)..]
};

struct SubscribeEnd
{
  Namespace quicr_namespace;
  SubscribeResult::SubscribeStatus reason;
};

/*===========================================================================*/
// Publish Message Types
/*===========================================================================*/

// TODO: Add missing fields:
struct PublishIntent
{
  MessageType message_type;
  // * origin_url_length(i),
  // * origin_url(…)…,
  uint64_t transaction_id;
  Namespace quicr_namespace;
  // * relay_auth_token_length(i),
  // * relay_token(…),
  std::variant<bytes, unowned_bytes> payload;
  uintVar_t media_id;
  uintVar_t datagram_capable;
};

struct PublishIntentResponse
{
  MessageType message_type;
  Namespace quicr_namespace;
  Response response;
  uint64_t transaction_id;
  // * signature(32)
  // * [Reason Phrase Length (i),
  // * [Reason Phrase (..)],
};

struct Header
{
  uintVar_t media_id;
  Name name;
  uintVar_t group_id;
  uintVar_t object_id;
  uintVar_t offset_and_fin;
  uint8_t flags;
};

struct PublishDatagram
{
  Header header;
  MediaType media_type;
  uintVar_t media_data_length;
  std::variant<bytes, unowned_bytes> media_data;
};

struct PublishStream
{
  uintVar_t media_data_length;
  std::variant<bytes, unowned_bytes> media_data;
};

// TODO: Add missing fields:
struct PublishIntentEnd
{
  MessageType message_type;
  Namespace quicr_namespace;
  // * relay_auth_token_length(i),
  // * relay_token(…),
  std::variant<bytes, unowned_bytes> payload;
};

/*===========================================================================*/
// Fetch Message Types
/*===========================================================================*/

struct Fetch
{
  uint64_t transaction_id;
  Name name;
  // TODO - Add authz
};
}
