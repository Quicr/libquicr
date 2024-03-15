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
        value.resize(data.size());
        value = {data.begin(), data.end()};
        return msg;
    }


    //
    // Subscribe
    //


    MessageBuffer& operator<<(MessageBuffer &buffer, const Location &msg) {
        if (msg.mode == LocationMode::None) {
            buffer << static_cast<uint8_t >(msg.mode);
            return buffer;
        }
        buffer << static_cast<uint8_t >(msg.mode);
        buffer << msg.value;
        return buffer;
    }

    MessageBuffer& operator>>(MessageBuffer &buffer, Location &msg) {
        uint8_t mode {0};
        buffer >> mode;
        msg.mode = static_cast<LocationMode>(mode);
        if (static_cast<LocationMode>(mode) != LocationMode::None) {
            buffer >> msg.value;
        }
        return buffer;
    }

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqSubscribe &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE);
        buffer << msg.subscribe_id;
        buffer << msg.track_alias;
        buffer << msg.track;
        buffer << msg.start_group;
        buffer << msg.start_object;
        buffer << msg.end_group;
        buffer << msg.end_object;
        //buffer << msg.track_params;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqSubscribe &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE)) {
            throw MessageTypeException(msg_type, MessageType::Subscribe);
        }
        buffer >> msg.subscribe_id;
        buffer >> msg.track_alias;
        buffer >> msg.track;
        buffer >> msg.start_group;
        buffer >> msg.start_object;
        buffer >> msg.end_group;
        buffer >> msg.end_object;
        //buffer >> msg.track_params;
        return buffer;
    }

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqUnsubscribe &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_UNSUBSCRIBE);
        buffer << msg.subscribe_id;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqUnsubscribe &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != static_cast<uint8_t>(MESSAGE_TYPE_UNSUBSCRIBE)) {
            throw MessageBuffer::ReadException("MoqUnsubscribe");
        }
        buffer >> msg.subscribe_id;
        return buffer;
    }

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqSubscribeOk &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_OK);
        buffer << msg.subscribe_id;
        buffer << msg.expires;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqSubscribeOk &msg) {
        uint8_t msg_type;
        // type
        buffer >> msg_type;
        if (msg_type != static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_OK)) {
            throw MessageBuffer::ReadException("MoqSubscribeOk");
        }
        buffer >> msg.subscribe_id;
        buffer >> msg.expires;
        return buffer;
    }

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqSubscribeError &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_ERROR);
        buffer << msg.subscribe_id;
        buffer << msg.track_alias;
        buffer << msg.err_code;
        buffer << msg.reason_phrase;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqSubscribeError &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_ERROR)) {
            throw MessageBuffer::ReadException("MoqSubscribeError");
        }
        buffer >> msg.subscribe_id;
        buffer >> msg.track_alias;
        buffer >> msg.err_code;
        buffer >> msg.reason_phrase;
        return buffer;
    }


    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqSubscribeFin &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_FIN);
        buffer << msg.subscribe_id;
        buffer << msg.final_group_id;
        buffer << msg.final_object_id;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqSubscribeFin &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_FIN)) {
            throw MessageBuffer::ReadException("MoqSubscribeFin");
        }
        buffer >> msg.subscribe_id;
        buffer >> msg.final_group_id;
        buffer >> msg.final_object_id;
        return buffer;
    }

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqSubscribeRst &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_RST);
        buffer << msg.subscribe_id;
        buffer << msg.err_code;
        buffer << msg.reason_phrase;
        buffer << msg.final_group_id;
        buffer << msg.final_object_id;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqSubscribeRst &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_RST)) {
            throw MessageBuffer::ReadException("MoqSubscribeRst");
        }
        buffer >> msg.subscribe_id;
        buffer >> msg.err_code;
        buffer >> msg.reason_phrase;
        buffer >> msg.final_group_id;
        buffer >> msg.final_object_id;
        return buffer;
    }


    //
    // Announce
    //

    MessageBuffer&operator<<(MessageBuffer &buffer, const MoqAnnounce &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_ANNOUNCE);
        buffer << msg.track_namespace;
        return buffer;
    }

    MessageBuffer& operator>>(MessageBuffer &buffer, MoqAnnounce &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != MESSAGE_TYPE_ANNOUNCE) {
            throw MessageBuffer::ReadException("MoqAnnounce");
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
            throw MessageBuffer::ReadException("MoqAnnounceOk");
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
            throw MessageBuffer::ReadException("MoqAnnounceError");
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
            throw MessageBuffer::ReadException("MoqUnannounce");
        }
        buffer >> msg.track_namespace;
        return buffer;
    }


    //
    // Goaway
    //

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqGoaway &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_GOAWAY);
        buffer << msg.new_session_uri;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqGoaway &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != MESSAGE_TYPE_GOAWAY) {
            throw MessageBuffer::ReadException("MoqGoaway");
        }
        buffer >> msg.new_session_uri;
        return buffer;
    }

    //
    // Object
    //

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqObjectStream &msg) {
        buffer << MESSAGE_TYPE_OBJECT_STREAM;
        buffer << msg.subscribe_id;
        buffer << msg.track_alias;
        buffer << msg.group_id;
        buffer << msg.object_id;
        buffer << msg.priority;
        buffer << msg.payload;
        return buffer;
    }


    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqObjectStream &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != MESSAGE_TYPE_OBJECT_STREAM) {
            throw MessageBuffer::ReadException("MoqObjectStream");
        }
        buffer >> msg.subscribe_id;
        buffer >> msg.track_alias;
        buffer >> msg.group_id;
        buffer >> msg.object_id;
        buffer >> msg.priority;
        buffer >> msg.payload;
        return buffer;
    }

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqObjectDatagram &msg) {
        buffer << MESSAGE_TYPE_OBJECT_PREFER_DATAGRAM;
        buffer << msg.subscribe_id;
        buffer << msg.track_alias;
        buffer << msg.group_id;
        buffer << msg.object_id;
        buffer << msg.priority;
        buffer << msg.payload;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqObjectDatagram &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != MESSAGE_TYPE_OBJECT_PREFER_DATAGRAM) {
            throw MessageBuffer::ReadException("MoqObjectDatagram");
        }
        buffer >> msg.subscribe_id;
        buffer >> msg.track_alias;
        buffer >> msg.group_id;
        buffer >> msg.object_id;
        buffer >> msg.priority;
        buffer >> msg.payload;
        return buffer;
    }

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqStreamTrackObject &msg) {
        buffer << msg.group_id;
        buffer << msg.object_id;
        buffer << msg.payload;
        return buffer;
    }

    MessageBuffer&
    operator>>(MessageBuffer &buffer, MoqStreamTrackObject &msg) {
        buffer >> msg.group_id;
        buffer >> msg.object_id;
        buffer >> msg.payload;
        return buffer;
    }

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqStreamHeaderTrack &msg) {
        buffer << MESSAGE_TYPE_STREAM_HEADER_TRACK;
        buffer << msg.subscribe_id;
        buffer << msg.track_alias;
        buffer << msg.priority;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqStreamHeaderTrack &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != MESSAGE_TYPE_STREAM_HEADER_TRACK) {
            throw MessageBuffer::ReadException("MoqStreamHeaderTrack");
        }
        buffer >> msg.subscribe_id;
        buffer >> msg.track_alias;
        buffer >> msg.priority;
        return buffer;
    }



    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqStreamHeaderGroup &msg) {
        buffer << MESSAGE_TYPE_STREAM_HEADER_GROUP;
        buffer << msg.subscribe_id;
        buffer << msg.track_alias;
        buffer << msg.group_id;
        buffer << msg.priority;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqStreamHeaderGroup &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != MESSAGE_TYPE_STREAM_HEADER_GROUP) {
            throw MessageBuffer::ReadException("MoqStreamHeaderGroup");
        }
        buffer >> msg.subscribe_id;
        buffer >> msg.track_alias;
        buffer >> msg.group_id;
        buffer >> msg.priority;
        return buffer;
    }


    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqStreamGroupObject &msg) {
        buffer << msg.object_id;
        buffer << msg.payload;
        return buffer;
    }

    MessageBuffer&
    operator>>(MessageBuffer &buffer, MoqStreamGroupObject &msg) {
        buffer >> msg.object_id;
        buffer >> msg.payload;
        return buffer;
    }

    std::tuple<Location, Location, Location, Location> to_locations(const SubscribeIntent& intent) {
        auto none_location = Location {.mode = LocationMode::None};

        /*
         * Sequence:                0    1    2    3    4   [5]  [6] ...
                                                      ^
                                                 Largest Sequence
         RelativePrevious Value:  4    3    2    1    0
         RelativeNext Value:                               0    1  ...
       */
        switch (intent.mode) {

            case SubscribeIntent::Mode::immediate:
                return std::make_tuple(Location{.mode=LocationMode::RelativePrevious, .value=0}, //StartGroup
                                       none_location, // EndGroup
                                       Location{.mode=LocationMode::RelativePrevious, .value=0}, //StartObject
                                       none_location); // EndObject
            case SubscribeIntent::Mode::sync_up:
                throw std::runtime_error("Intent Unsupported for Subscribe");
            case SubscribeIntent::Mode::wait_up:
                throw std::runtime_error("Intent Unsupported for Subscribe");
        }
    }


    // Setup common
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
    operator<<(MessageBuffer &buffer, const MoqParameter &param) {
        buffer << (uint8_t) param.key;
        buffer << static_cast<std::vector<uint8_t>>(param.value);
        return buffer;
    }

    MessageBuffer&
    operator>>(MessageBuffer &buffer, MoqParameter &param) {
        uint8_t  key = 0;
        buffer >> key;
        param.key = (ParameterType) key;
        buffer >> param.value;
        return buffer;
    }


    // Client Setup message
    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqClientSetup &msg) {
        buffer << static_cast<uintVar_t>(MESSAGE_TYPE_CLIENT_SETUP);
        // versions
        buffer << static_cast<uintVar_t>(msg.supported_versions.size());
        for (const auto& ver: msg.supported_versions) {
            buffer << static_cast<uintVar_t>(ver);
        }

        // parameters (0)
        uintVar_t num_params {0};
        buffer << num_params;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqClientSetup &msg) {
        uintVar_t msg_type;
        buffer >> msg_type;
        if (msg_type != (uintVar_t) MESSAGE_TYPE_CLIENT_SETUP) {
            throw MessageBuffer::ReadException("MoqClientSetup");
        }
        uintVar_t num_versions {0};
        buffer >> num_versions;
        msg.supported_versions.resize(num_versions);
        for(int i = 0; i < num_versions; i++) {
            uintVar_t version{0};
            buffer >> version;
            msg.supported_versions.push_back(version);
        }

        uintVar_t num_params {0};
        buffer >> num_params;
        for(int i = 0; i < num_params; i++) {
            MoqParameter parameter;
            buffer >> parameter;
            msg.setup_parameters.push_back(parameter);
        }

        return buffer;
    }

    // Server Setup message
    MessageBuffer&
    operator<<(MessageBuffer &buffer, const MoqServerSetup &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_SERVER_SETUP);
        buffer << msg.supported_version;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqServerSetup &msg) {
        uint8_t msg_type;
        buffer >> msg_type;
        if (msg_type != MESSAGE_TYPE_SERVER_SETUP) {
            throw MessageBuffer::ReadException("MoqServerSetup");
        }
        buffer >> msg.supported_version;
        return buffer;
    }
}