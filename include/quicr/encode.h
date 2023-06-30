#pragma once

#include <quicr/message_buffer.h>
#include <quicr/quicr_common.h>
#include <quicr_name>

#include <random>
#include <string>
#include <vector>

/**
 *  Utilties to encode and decode protocol messages
 */
namespace quicr::messages {

/*===========================================================================*/
// Common
/*===========================================================================*/

uint64_t
create_transaction_id();

/*===========================================================================*/
// Subscribe Message Types
/*===========================================================================*/

struct Subscribe
{
  uint8_t version;
  uint64_t transaction_id;
  quicr::Namespace quicr_namespace;
  SubscribeIntent intent;
};

struct Unsubscribe
{
  uint8_t version;
  quicr::Namespace quicr_namespace;
};

struct SubscribeResponse
{
  quicr::Namespace quicr_namespace;
  SubscribeResult::SubscribeStatus response;
  uint64_t transaction_id;
  /* TODO:
   *
   * signature(32)
   * [Reason Phrase Length (i)],
   * [Reason Phrase (..)],
   * [redirect_relay_url_length(i)],
   * [redirect_relay_url(…)..]
   */
};

struct SubscribeEnd
{
  quicr::Namespace quicr_namespace;
  SubscribeResult::SubscribeStatus reason;
};

/*===========================================================================*/
// Publish Message Types
/*===========================================================================*/

struct PublishIntent
{
  MessageType message_type;
  //  *     origin_url_length(i),
  //  *     origin_url(…)…,
  uint64_t transaction_id;
  quicr::Namespace quicr_namespace;
  //  *     relay_auth_token_length(i),
  //  *     relay_token(…),
  std::vector<uint8_t> payload;
  uintVar_t media_id;
  uintVar_t datagram_capable;
};

struct PublishIntentResponse
{
  MessageType message_type;
  quicr::Namespace quicr_namespace;
  Response response;
  uint64_t transaction_id;
  // *  signature(32)
  // *  [Reason Phrase Length (i),
  // *  [Reason Phrase (..)],
};

struct Header
{
  uintVar_t media_id;
  quicr::Name name;
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
  std::vector<uint8_t> media_data;
};

struct PublishStream
{
  uintVar_t media_data_length;
  std::vector<uint8_t> media_data;
};

struct PublishIntentEnd
{
  MessageType message_type;
  quicr::Namespace quicr_namespace;
  //  * relay_auth_token_length(i),
  //  * relay_token(…),
  std::vector<uint8_t> payload;
};

/*===========================================================================*/
// Fetch Message Types
/*===========================================================================*/

struct Fetch
{
  uint64_t transaction_id;
  quicr::Name name; // resource to retrieve
  // TODO - Add authz
};

/*===========================================================================*/
// MessageBuffer operator overloads.
// TODO: These should ideally be removed eventually.
/*===========================================================================*/

MessageBuffer&
operator<<(MessageBuffer& buffer, const PublishIntent& msg);
MessageBuffer&
operator<<(MessageBuffer& buffer, PublishIntent&& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, PublishIntent& msg);

MessageBuffer&
operator<<(MessageBuffer& buffer, const PublishDatagram& msg);
MessageBuffer&
operator<<(MessageBuffer& buffer, PublishDatagram&& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, PublishDatagram& msg);

MessageBuffer&
operator<<(MessageBuffer& buffer, const PublishStream& msg);
MessageBuffer&
operator<<(MessageBuffer& buffer, PublishStream&& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, PublishStream& msg);

MessageBuffer&
operator<<(MessageBuffer& buffer, const PublishIntentEnd& msg);
MessageBuffer&
operator<<(MessageBuffer& buffer, PublishIntentEnd&& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, PublishIntentEnd& msg);
}
