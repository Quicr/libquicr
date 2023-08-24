#pragma once

#include <quicr/common.h>
#include <quicr/name.h>
#include <quicr/namespace.h>
#include <quicr/uvarint.h>

#include <string>
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

  PeerMsg = 128
};

struct Message
{
  const uint8_t version = 1;
};

/*===========================================================================*/
// Subscribe Message Types
/*===========================================================================*/

struct Subscribe : public Message
{
  uint64_t transaction_id;
  quicr::Namespace quicr_namespace;
  SubscribeIntent intent;
};

struct Unsubscribe : public Message
{
  quicr::Namespace quicr_namespace;
};

// TODO: Add missing fields:
struct SubscribeResponse : public Message
{
  quicr::Namespace quicr_namespace;
  SubscribeResult::SubscribeStatus response;
  uint64_t transaction_id;
  // * - signature(32)
  // * - [Reason Phrase Length (i)],
  // * - [Reason Phrase (..)],
  // * - [redirect_relay_url_length(i)],
  // * - [redirect_relay_url(…)..]
};

struct SubscribeEnd : public Message
{
  quicr::Namespace quicr_namespace;
  SubscribeResult::SubscribeStatus reason;
};

/*===========================================================================*/
// Publish Message Types
/*===========================================================================*/

// TODO: Add missing fields:
struct PublishIntent : public Message
{
  MessageType message_type;
  // * origin_url_length(i),
  // * origin_url(…)…,
  uint64_t transaction_id;
  quicr::Namespace quicr_namespace;
  // * relay_auth_token_length(i),
  // * relay_token(…),
  std::vector<uint8_t> payload;
  uintVar_t media_id;
  uintVar_t datagram_capable;
};

// TODO: Add missing fields:
struct PublishIntentResponse : public Message
{
  MessageType message_type;
  quicr::Namespace quicr_namespace;
  Response response;
  uint64_t transaction_id;
  // * signature(32)
  // * [Reason Phrase Length (i),
  // * [Reason Phrase (..)],
};

struct PublishDatagram : public Message
{
  struct Header
  {
    uintVar_t media_id;
    quicr::Name name;
    uintVar_t group_id;
    uintVar_t object_id;
    uintVar_t offset_and_fin;
    uint8_t flags;
  } header;
  MediaType media_type;
  uintVar_t media_data_length;
  std::vector<uint8_t> media_data;
};

struct PublishStream : public Message
{
  uintVar_t media_data_length;
  std::vector<uint8_t> media_data;
};

// TODO: Add missing fields:
struct PublishIntentEnd : public Message
{
  MessageType message_type;
  quicr::Namespace quicr_namespace;
  // * relay_auth_token_length(i),
  // * relay_token(…),
  std::vector<uint8_t> payload;
};

/*===========================================================================*/
// Fetch Message Types
/*===========================================================================*/

// TODO: Add missing fields:
struct Fetch : public Message
{
  uint64_t transaction_id;
  quicr::Name name;
  // * authz
};
}
