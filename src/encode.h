#include <string>
#include <vector>

#include "message_buffer.h"
#include <quicr/quicr_common.h>

/* Utilties to encode and decode protocol messages */
namespace quicr::messages
{

///
/// Common
///
enum class MessageType : uint8_t
{
  Unknown = 0,
  Subscribe = 1,
  Publish = 2,
};

enum class MediaType : uint8_t
{
  Manifest,
  Advertisement,
  Text,
  RealtimeMedia
};

enum class Response : uint8_t
{
  Ok = 0,
  Expired = 1,
  Fail = 2,
  Redirect = 3
};


/*===========================================================================*/
// Subscribe
/*===========================================================================*/

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

struct SubscribeResponse
{
  MessageType message_type;
  Response response;
  uint64_t transaction_id;
  uintVar_t media_id;
  /* TODO:
   * 
   * signature(32)
   * [Reason Phrase Length (i)],
   * [Reason Phrase (..)],
   * [redirect_relay_url_length(i)],
   * [redirect_relay_url(…)..]
   */
};

void
operator<<(MessageBuffer& buffer, const SubscribeResponse& msg);
bool
operator>>(MessageBuffer& buffer, SubscribeResponse& msg);

struct SubscribeEnd
{
  MessageType message_type;
  uintVar_t media_id;
  std::vector<uint8_t> payload;
  /* TODO
   * relay_auth_token_length(i),
   * relay_token(…),
   */
};

void
operator<<(MessageBuffer& buffer, const SubscribeEnd& msg);
bool
operator>>(MessageBuffer& buffer, SubscribeEnd& msg);


/*===========================================================================*/
// Publish
/*===========================================================================*/

struct DatagramHeader
{
  uintVar_t media_id;
  uintVar_t group_id;
  uintVar_t object_id;
  uintVar_t offset_and_fin;
  uint8_t flags;
};

void
operator<<(MessageBuffer& buffer, const DatagramHeader& msg);
bool
operator>>(MessageBuffer& buffer, DatagramHeader& msg);

struct PublishDatagram
{
  DatagramHeader datagram_header;
  MediaType media_type;
  uintVar_t media_data_length;
  std::vector<uint8_t> media_data;
};

void
operator<<(MessageBuffer& buffer, const PublishDatagram& msg);
bool
operator>>(MessageBuffer& buffer, PublishDatagram& msg);

}