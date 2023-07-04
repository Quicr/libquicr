#pragma once

#include <quicr/message_buffer.h>
#include <quicr/message_types.h>

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
operator<<(MessageBuffer& msg, quicr::Namespace value);
MessageBuffer&
operator>>(MessageBuffer& msg, quicr::Namespace& value);

MessageBuffer&
operator<<(MessageBuffer& msg, const quicr::uintVar_t& val);
MessageBuffer&
operator>>(MessageBuffer& msg, quicr::uintVar_t& val);

MessageBuffer&
operator<<(MessageBuffer& msg, const std::vector<uint8_t>& val);
MessageBuffer&
operator<<(MessageBuffer& msg, std::vector<uint8_t>&& val);
MessageBuffer&
operator>>(MessageBuffer& msg, std::vector<uint8_t>& val);

/*===========================================================================*/
// MessageBuffer operator overloads.
/*===========================================================================*/

MessageBuffer&
operator<<(MessageBuffer& buffer, const Subscribe& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, Subscribe& msg);

MessageBuffer&
operator<<(MessageBuffer& buffer, const Unsubscribe& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, Unsubscribe& msg);

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
operator<<(MessageBuffer& buffer, const PublishIntentResponse& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, PublishIntentResponse& msg);

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

MessageBuffer&
operator<<(MessageBuffer& buffer, const Fetch& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, Fetch& msg);

}
