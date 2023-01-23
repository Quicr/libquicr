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

/* QuicrNamespace encode and decode is temporarily
 * added here. This needs to be moved to QuicrNameSpace
 * class/struct that offers encode and decode operations
 */

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
operator>>(const MessageBuffer& buffer, Subscribe& msg);

}