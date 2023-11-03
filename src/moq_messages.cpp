#include "quicr/moq_message_types.h"
#include "quicr/message_buffer.h"
#include "quicr/encode.h"

namespace quicr::messages {

    // String encode/decode
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


    // FullTrackName encode and decode
    MessageBuffer&
    operator<<(MessageBuffer &buffer, const FullTrackName &value) {
        buffer << value.track_namespace;
        buffer << value.track_name;
        return buffer;
    }

    MessageBuffer&
    operator>>(MessageBuffer &buffer, FullTrackName &value) {
        buffer >> value.track_namespace;
        buffer >> value.track_name;
        return buffer;
    }


    // Location encode and decode
    MessageBuffer&
    operator<<(MessageBuffer &buffer, const Location &location) {
        uintVar_t mode = static_cast<uintVar_t>(location.mode);
        buffer << mode;

        if (location.value.has_value()) {
            buffer << location.value.value();
        } else {
            if(location.mode != LocationMode::None) {
              throw std::runtime_error("Malformed location: Missing Value");
            }
        }
        return buffer;
    }

    MessageBuffer&
    operator>>(MessageBuffer &buffer, Location& location) {
        buffer >> location.mode;
        if (location.mode == (uintVar_t) 0) {
            return buffer;
        }
        uintVar_t value {0};
        buffer >> value;
        location.value = value;
        return buffer;
    }



    // Subscribe encode and decode
    MessageBuffer&
    operator<<(MessageBuffer &buffer, const MoqSubscribe& msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE);
        buffer << msg.track;
        buffer << msg.start_group;
        buffer << msg.start_object;
        buffer << msg.end_group;
        buffer << msg.end_object;
        //TODO: add params
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqSubscribe& msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE)) {
            throw MessageTypeException(msg_type, MessageType::Subscribe);
        }
        buffer >> msg.track;
        buffer >> msg.start_group;
        buffer >> msg.start_object;
        buffer >> msg.end_group;
        buffer >> msg.end_object;
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
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_OK);
        buffer << msg.track;
        buffer << msg.track_id;
        buffer << msg.expires;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqSubscribeOk &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_OK)) {
            throw std::runtime_error("MoqSubscribeOk");
        }
        buffer >> msg.track;
        buffer >> msg.track_id;
        buffer >> msg.expires;
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
            throw std::runtime_error("MoqSubscribeError");
        }
        buffer >> msg.track;
        buffer >> msg.err_code;
        buffer >> msg.reason_phrase;
        return buffer;
    }

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqSubscribeFin &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_FIN);
        buffer << msg.track;
        buffer << msg.final_group;
        buffer << msg.final_object;
        return buffer;
    }

    MessageBuffer&
    operator>>(MessageBuffer &buffer, MoqSubscribeFin &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_FIN)) {
            throw std::runtime_error("MoqSubscribeFin");
        }
        buffer >> msg.track;
        buffer >> msg.final_group;
        buffer >> msg.final_object;
        return buffer;
    }

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqSubscribeRst &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_RST);
        buffer << msg.track;
        buffer << msg.err_code;
        buffer << msg.reason_phrase;
        buffer << msg.final_group;
        buffer << msg.final_object;
        return buffer;
    }

    MessageBuffer&
    operator>>(MessageBuffer &buffer, MoqSubscribeRst &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_RST)) {
            throw std::runtime_error("MoqSubscribeRst");
        }
        buffer >> msg.track;
        buffer >> msg.err_code;
        buffer >> msg.reason_phrase;
        buffer >> msg.final_group;
        buffer >> msg.final_object;
        return buffer;
    }



// Announce Encode & Decode

    MessageBuffer&operator<<(MessageBuffer &buffer, const MoqAnnounce &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_ANNOUNCE);
        buffer << msg.track_namespace;
        return buffer;
    }

    MessageBuffer& operator>>(MessageBuffer &buffer, MoqAnnounce &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != MESSAGE_TYPE_ANNOUNCE) {
            throw std::runtime_error("MoqAnnounce");
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
            throw std::runtime_error("MoqAnnounceOk");
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
            throw std::runtime_error("MoqAnnounceError");
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
            throw std::runtime_error("MoqUnAnnounce");
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
        buffer << static_cast<uintVar_t> (msg.payload.size());
        buffer << msg.payload;
        return buffer;
    }


    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqObject &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != MESSAGE_TYPE_OBJECT) {
            throw std::runtime_error("MoqObject");
        }
        buffer >> msg.track_id;
        buffer >> msg.group_sequence;
        buffer >> msg.object_sequence;
        buffer >> msg.priority;
        uint8_t forwarding_pref;
        buffer >> forwarding_pref;
        uintVar_t data_len{0};
        buffer >> data_len;
        buffer >> msg.payload;
        if (msg.payload.size() != static_cast<size_t>(data_len)) {
            throw MessageBuffer::LengthException(msg.payload.size(),
                                                 data_len);
        }
        return buffer;
    }

    // Setup Encode and Decode

    // vector<varint> encode/decode
    MessageBuffer&
    operator<<(MessageBuffer& buffer, const std::vector<uintVar_t>& val)
    {
        buffer << static_cast<uintVar_t>(val.size());
        // TODO (Suhas): This needs revisiting
        for(uint64_t i = 0; i < val.size(); i++) {
            buffer << val[i];
        }

        return buffer;
    }

    MessageBuffer&
    operator>>(MessageBuffer& msg, std::vector<uintVar_t>& val)
    {
        auto vec_size = uintVar_t(0);
        msg >> vec_size;
        auto version = std::vector<uintVar_t>();
        version.resize((uint64_t) vec_size);
        val.resize((uint64_t) vec_size);

        // TODO (Suhas): This needs revisiting
        for(uint64_t i = 0; i < version.size(); i++) {
            msg >> version[i];
        }
        val = std::move(version);
        return msg;
    }

    MessageBuffer&
    operator<<(MessageBuffer &buffer, const ClientSetup &msg){
        buffer << msg.supported_versions;
        return buffer;
    }

    MessageBuffer& operator>>(MessageBuffer &buffer, ClientSetup &msg){
      buffer >> msg.supported_versions;
      return buffer;
    }

    MessageBuffer&
    operator<<(MessageBuffer &buffer, const ServerSetup &msg){
      buffer << msg.selected_version;
      return buffer;
    }

    MessageBuffer& operator>>(MessageBuffer &buffer, ServerSetup &msg){
      buffer >> msg.selected_version;
      return buffer;
    }


}


