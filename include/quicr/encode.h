#pragma once

#include <quicr/message_buffer.h>
#include <quicr/quicr_common.h>
#include <quicr/quicr_name.h>
#include <quicr/quicr_namespace.h>

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

MessageBuffer&
operator<<(MessageBuffer& buffer, const Unsubscribe& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, Unsubscribe& msg);

MessageBuffer&
operator<<(MessageBuffer& buffer, const Subscribe& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, Subscribe& msg);

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

MessageBuffer&
operator<<(MessageBuffer& buffer, const SubscribeResponse& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, SubscribeResponse& msg);

struct SubscribeEnd
{
  quicr::Namespace quicr_namespace;
  SubscribeResult::SubscribeStatus reason;
};

MessageBuffer&
operator<<(MessageBuffer& buffer, const SubscribeEnd& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, SubscribeEnd& msg);

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

MessageBuffer&
operator<<(MessageBuffer& buffer, const PublishIntent& msg);
MessageBuffer&
operator<<(MessageBuffer& buffer, PublishIntent&& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, PublishIntent& msg);

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

MessageBuffer&
operator<<(MessageBuffer& buffer, const PublishIntentResponse& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, PublishIntentResponse& msg);

struct Header
{
  uintVar_t media_id;
  quicr::Name name;
  uintVar_t group_id;
  uintVar_t object_id;
  uintVar_t offset_and_fin;
  uint8_t flags;
};

MessageBuffer&
operator<<(MessageBuffer& buffer, const Header& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, Header& msg);

struct PublishDatagram
{
  Header header;
  MediaType media_type;
  uintVar_t media_data_length;
  std::vector<uint8_t> media_data;
};

MessageBuffer&
operator<<(MessageBuffer& buffer, const PublishDatagram& msg);
MessageBuffer&
operator<<(MessageBuffer& buffer, PublishDatagram&& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, PublishDatagram& msg);

struct PublishStream
{
  uintVar_t media_data_length;
  std::vector<uint8_t> media_data;
};

MessageBuffer&
operator<<(MessageBuffer& buffer, const PublishStream& msg);
MessageBuffer&
operator<<(MessageBuffer& buffer, PublishStream&& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, PublishStream& msg);

struct PublishIntentEnd
{
  MessageType message_type;
  quicr::Namespace quicr_namespace;
  //  * relay_auth_token_length(i),
  //  * relay_token(…),
  std::vector<uint8_t> payload;
};

MessageBuffer&
operator<<(MessageBuffer& buffer, const PublishIntentEnd& msg);
MessageBuffer&
operator<<(MessageBuffer& buffer, PublishIntentEnd&& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, PublishIntentEnd& msg);

MessageBuffer&
operator<<(MessageBuffer& msg, const Name& ns);
MessageBuffer&
operator>>(MessageBuffer& msg, Name& ns);

MessageBuffer&
operator<<(MessageBuffer& msg, const Namespace& ns);
MessageBuffer&
operator>>(MessageBuffer& msg, Namespace& ns);


/*===========================================================================*/
// Fetch Message Types
/*===========================================================================*/

struct Fetch
{
  uint64_t transaction_id;
  quicr::Name name; // resource to retrieve
  // TODO - Add authz
};

MessageBuffer&
operator<<(MessageBuffer& buffer, const Fetch& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, Fetch& msg);

}
