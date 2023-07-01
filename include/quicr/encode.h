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
//
// Note: These are overloads for message types that contain vectors.
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
