#pragma once

#include <quicr/message_buffer.h>
#include <quicr/message_types.h>
#include <quicr/namespace.h>

#include <ostream>
#include <span>
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

struct MessageTypeException : public MessageBuffer::ReadException
{
  MessageTypeException(MessageType type, MessageType expected_type);
  MessageTypeException(uint8_t type, MessageType expected_type);
};

MessageBuffer&
operator<<(MessageBuffer& msg, const Namespace& value);
MessageBuffer&
operator>>(MessageBuffer& msg, Namespace& value);

MessageBuffer&
operator<<(MessageBuffer& msg, const uintVar_t& val);
MessageBuffer&
operator>>(MessageBuffer& msg, uintVar_t& val);

MessageBuffer&
operator<<(MessageBuffer& msg, std::span<const uint8_t> val);
MessageBuffer&
operator<<(MessageBuffer& msg, std::vector<uint8_t>&& val);
MessageBuffer&
operator>>(MessageBuffer& msg, std::vector<uint8_t>& val);

MessageBuffer&
operator<<(MessageBuffer& msg, const std::string& val);
MessageBuffer&
operator>>(MessageBuffer& msg, std::string& val);

/*===========================================================================*/
// Connection message MessageBuffer operator overloads.
/*===========================================================================*/
MessageBuffer&
operator<<(MessageBuffer& buffer, const Connect& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, Connect& msg);

MessageBuffer&
operator<<(MessageBuffer& buffer, const ConnectResponse& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, ConnectResponse& msg);

/*===========================================================================*/
// Subscription message MessageBuffer operator overloads.
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

/*===========================================================================*/
// Publication message MessageBuffer operator overloads.
/*===========================================================================*/

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

/*===========================================================================*/
// Fetch message MessageBuffer operator overloads.
/*===========================================================================*/

MessageBuffer&
operator<<(MessageBuffer& buffer, const Fetch& msg);
MessageBuffer&
operator>>(MessageBuffer& buffer, Fetch& msg);

}
