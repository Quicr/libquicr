#pragma once

#include <quicr/message_buffer.h>
#include <quicr/message_types.h>
#include <quicr/quicr_common.h>
#include <quicr_name>

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

MessageBuffer&
operator<<(MessageBuffer& msg, const uintVar_t& val);
MessageBuffer&
operator>>(MessageBuffer& msg, uintVar_t& val);

/*===========================================================================*/
// MessageBuffer operator overloads.
/*===========================================================================*/

MessageBuffer&
operator<<(MessageBuffer& buffer, const Unsubscribe& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, Unsubscribe& msg);

MessageBuffer&
operator<<(MessageBuffer& buffer, const Subscribe& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, Subscribe& msg);

MessageBuffer&
operator<<(MessageBuffer& buffer, const SubscribeResponse& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, SubscribeResponse& msg);

MessageBuffer&
operator<<(MessageBuffer& buffer, const SubscribeEnd& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, SubscribeEnd& msg);

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
