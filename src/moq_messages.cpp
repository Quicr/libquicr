#include "quicr/moq_message_types.h"
#include "quicr/message_buffer.h"
#include "quicr/encode.h"

namespace quicr::messages {

    MessageBuffer &
    operator<<(MessageBuffer &msg, const std::string &value) {

        auto bytes = quicr::bytes{std::vector<uint8_t>(value.begin(), value.end())};
        return msg << bytes;
    }

    MessageBuffer &
    operator>>(MessageBuffer &msg, std::string &value) {
        quicr::bytes data{};
        msg >> data;
        value = {data.begin(), data.end()};
        return msg;
    }

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqSubscribe &msg) {
        // type
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE);
        // hint
        buffer << static_cast<uint8_t>(msg.hint);
        // ftn
        buffer << msg.track;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqSubscribe &msg) {
        uint8_t msg_type;
        // type
        buffer >> msg_type;
        if (msg_type != static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE)) {
            throw MessageTypeException(msg_type, MessageType::Subscribe);
        }

        // hint
        uint8_t hint = 0;
        buffer >> hint;
        msg.hint = static_cast<SubscriptionHint>(hint);
        // ftn
        buffer >> msg.track;

        return buffer;
    }

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqUnsubscribe &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_UNSUBSCRIBE);
        buffer << msg.track;

        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqUnsubscribe &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != static_cast<uint8_t>(MESSAGE_TYPE_UNSUBSCRIBE)) {
            throw MessageTypeException(msg_type, MessageType::Unsubscribe);
        }

        buffer >> msg.track;
        return buffer;
    }

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqSubscribeOk &msg) {
        // type
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_OK);
        // track
        buffer << msg.track;
        // trackId
        buffer << msg.track_id;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqSubscribeOk &msg) {
        uint8_t msg_type;
        // type
        buffer >> msg_type;
        if (msg_type != static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_OK)) {
            throw MessageTypeException(MESSAGE_TYPE_SUBSCRIBE_OK);
        }
        // track
        buffer >> msg.track;
        // trackId
        buffer >> msg.track_id;
        return buffer;
    }

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqSubscribeError &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_ERROR);
        buffer << msg.track;
        buffer << msg.err_code;
        buffer << msg.reason_phrase;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqSubscribeError &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_ERROR)) {
            throw MessageTypeException(MESSAGE_TYPE_SUBSCRIBE_ERROR);
        }
        buffer >> msg.track;
        buffer >> msg.err_code;
        buffer >> msg.reason_phrase;
        return buffer;
    }








/*===========================================================================*/
// Annoucne Encode & Decode
/*===========================================================================*/

    MessageBuffer&operator<<(MessageBuffer &buffer, const MoqAnnounce &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_ANNOUNCE);
        buffer << msg.track_namespace;
        return buffer;
    }

    MessageBuffer& operator>>(MessageBuffer &buffer, MoqAnnounce &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != MESSAGE_TYPE_ANNOUNCE) {
            throw MessageTypeException(MESSAGE_TYPE_ANNOUNCE);
        }

        buffer >> msg.track_namespace;
        return buffer;
    }


    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqAnnounceOk &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_ANNOUNCE_OK);
        buffer << msg.track_namespace;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqAnnounceOk &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != MESSAGE_TYPE_ANNOUNCE_OK) {
            throw MessageTypeException(MESSAGE_TYPE_ANNOUNCE_OK);
        }

        buffer >> msg.track_namespace;
        return buffer;
    }

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqAnnounceError &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_ANNOUNCE_ERROR);
        buffer << msg.track_namespace;
        buffer << msg.err_code;
        buffer << msg.reason_phrase;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqAnnounceError &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != MESSAGE_TYPE_ANNOUNCE_ERROR) {
            throw MessageTypeException(MESSAGE_TYPE_ANNOUNCE_ERROR);
        }

        buffer >> msg.track_namespace;
        buffer >> msg.err_code;
        buffer >> msg.reason_phrase;
        return buffer;
    }


    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqUnannounce &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_UNANNOUNCE);
        buffer << msg.track_namespace;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqUnannounce &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != MESSAGE_TYPE_UNANNOUNCE) {
            throw MessageTypeException(MESSAGE_TYPE_UNANNOUNCE);
        }
        buffer >> msg.track_namespace;
        return buffer;
    }


    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqObject &msg) {
        buffer << MESSAGE_TYPE_OBJECT;
        buffer << msg.track_id;
        buffer << msg.group_sequence;
        buffer << msg.object_sequence;
        buffer << msg.priority;
        buffer << msg.forwarding_preference;
        buffer << static_cast<uintVar_t> (msg.payload.size());
        buffer << msg.payload;
        return buffer;
    }


    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqObject &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != MESSAGE_TYPE_OBJECT) {
            throw MessageTypeException(MESSAGE_TYPE_OBJECT);
        }
        buffer >> msg.track_id;
        buffer >> msg.group_sequence;
        buffer >> msg.object_sequence;
        buffer >> msg.priority;
        uint8_t forwarding_pref;
        buffer >> forwarding_pref;
        msg.forwarding_preference = static_cast<ForwardingPreference>(forwarding_pref);
        uintVar_t data_len{0};
        buffer >> data_len;
        buffer >> msg.payload;
        if (msg.payload.size() != static_cast<size_t>(data_len)) {
            throw MessageBuffer::LengthException(msg.payload.size(),
                                                 data_len);
        }

        return buffer;
    }
}