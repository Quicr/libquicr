#include "quicr/moq_message_types.h"
#include "quicr/message_buffer.h"
#include "quicr/encode.h"

namespace quicr::messages {

    //
    // Optional
    //

    template <typename T>
    MessageBuffer& operator<<(MessageBuffer& buffer, const std::optional<T>& val) {
      if (val.has_value()) {
       buffer << val.value();
      }
      return buffer;
    }

    template <typename T>
    MessageBuffer& operator>>(MessageBuffer& buffer, std::optional<T>& val) {
      T val_in{};
      buffer >> val_in;
      val = val_in;
      return buffer;
    }


    //
    // MoqParameter
    //

    MessageBuffer& operator<<(MessageBuffer &buffer, const MoqParameter &param) {
        buffer << param.param_type;
        buffer << param.param_length;
        if (param.param_length) {
          buffer << param.param_value;
        }

        return buffer;
    }

    MessageBuffer& operator>>(MessageBuffer &buffer, MoqParameter &param) {
        buffer >> param.param_type;
        buffer >> param.param_length;
        if (static_cast<uint64_t>(param.param_length) > 0) {
          buffer >> param.param_value;
        }
        return buffer;
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
        buffer << msg.value.value();
        return buffer;
    }

    MessageBuffer& operator>>(MessageBuffer &buffer, Location &msg) {
        uint8_t mode {0};
        buffer >> mode;
        msg.mode = static_cast<LocationMode>(mode);
        if (static_cast<LocationMode>(mode) != LocationMode::None) {
            uintVar_t loc_val{0};
            buffer >> loc_val;
            msg.value = loc_val;
        }
        return buffer;
    }

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqSubscribe &msg) {
        buffer << static_cast<uintVar_t>(MESSAGE_TYPE_SUBSCRIBE);
        buffer << msg.subscribe_id;
        buffer << msg.track_alias;
        buffer << msg.track_namespace;
        buffer << msg.track_name;
        buffer << msg.start_group;
        buffer << msg.start_object;
        buffer << msg.end_group;
        buffer << msg.end_object;
        buffer << static_cast<uintVar_t>(msg.track_params.size());
        for (const auto& param: msg.track_params) {
            buffer << param.param_type;
            buffer << param.param_length;
            buffer << param.param_value;
        }
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqSubscribe &msg) {
        buffer >> msg.subscribe_id;
        buffer >> msg.track_alias;
        buffer >> msg.track_namespace;
        buffer >> msg.track_name;
        buffer >> msg.start_group;
        buffer >> msg.start_object;
        buffer >> msg.end_group;
        buffer >> msg.end_object;
        uintVar_t num_params {0};
        buffer >> num_params;
        auto track_params = std::vector<MoqParameter>{};
        while(static_cast<uint64_t>(num_params) > 0) {
            auto param = MoqParameter{};
            buffer >> param.param_type;
            buffer >> param.param_length;
            buffer >> param.param_value;
            track_params.push_back(std::move(param));
            num_params = num_params - 1;
        }
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
        buffer >> msg.subscribe_id;
        return buffer;
    }

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqSubscribeOk &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_OK);
        buffer << msg.subscribe_id;
        buffer << msg.expires;
        buffer << static_cast<uint8_t>(msg.content_exists);
        if (msg.content_exists) {
            buffer << msg.largest_group;
            buffer << msg.largest_object;
            return buffer;
        }
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqSubscribeOk &msg) {
        buffer >> msg.subscribe_id;
        buffer >> msg.expires;
        uint8_t content_exists {0};
        buffer >> content_exists;
        if(content_exists > 1) {
            throw std::runtime_error("Invalid Context Exists Value");
        }

        if (content_exists == 1) {
            msg.content_exists = true;
            buffer >> msg.largest_group;
            buffer >> msg.largest_object;
            return buffer;
        }

        msg.content_exists = false;
        return buffer;
    }

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqSubscribeError &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_ERROR);
        buffer << msg.subscribe_id;
        buffer << msg.err_code;
        buffer << msg.reason_phrase;
        buffer << msg.track_alias;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqSubscribeError &msg) {
        buffer >> msg.subscribe_id;
        buffer >> msg.err_code;
        buffer >> msg.reason_phrase;
        buffer >> msg.track_alias;
        return buffer;
    }

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqSubscribeDone &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_SUBSCRIBE_DONE);
        buffer << msg.subscribe_id;
        buffer << msg.status_code;
        buffer << msg.reason_phrase;
        buffer << uint8_t(msg.content_exists);
        if (msg.content_exists) {
            buffer << msg.final_group_id;
            buffer << msg.final_object_id;
        }
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqSubscribeDone&msg) {
        buffer >> msg.subscribe_id;
        buffer >> msg.status_code;
        buffer >> msg.reason_phrase;
        uint8_t  context_exists {0};
        buffer >> context_exists;
        if (context_exists > 1) {
            throw std::runtime_error("Incorrect Context Exists value");
        }

        if (context_exists == 1) {
            msg.content_exists = true;
            buffer >> msg.final_group_id;
            buffer >> msg.final_object_id;
            return buffer;
        }

        msg.content_exists = false;
        return buffer;
    }


    //
    // Announce
    //

    MessageBuffer&operator<<(MessageBuffer &buffer, const MoqAnnounce &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_ANNOUNCE);
        buffer << msg.track_namespace;
        // TODO(Suhas): Fix this once we have params
        buffer << uintVar_t{0};
        return buffer;
    }

    MessageBuffer& operator>>(MessageBuffer &buffer, MoqAnnounce &msg) {
        buffer >> msg.track_namespace;
        uintVar_t num_params {0};
        buffer >> num_params;
        auto track_params = std::vector<MoqParameter>{};
        while(static_cast<uint64_t>(num_params) > 0) {
            auto param = MoqParameter{};
            buffer >> param.param_type;
            buffer >> param.param_length;
            buffer >> param.param_value;
            track_params.push_back(std::move(param));
            num_params = num_params - 1;
        }
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
        buffer >> msg.track_namespace;
        return buffer;
    }


    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqAnnounceCancel &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_ANNOUNCE_CANCEL);
        buffer << msg.track_namespace;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqAnnounceCancel &msg) {
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
        buffer >> msg.new_session_uri;
        return buffer;
    }

    //
    // Object
    //

    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqObjectStream &msg) {
        buffer << static_cast<uint8_t>(MESSAGE_TYPE_OBJECT_STREAM);
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
        buffer << MESSAGE_TYPE_OBJECT_DATAGRAM;
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
        auto none_location = Location {.mode = LocationMode::None, .value = std::nullopt};

        /*
         * Sequence:                0    1    2    3    4   [5]  [6] ...
                                                      ^
                                                 Largest Sequence
         RelativePrevious Value:  4    3    2    1    0
         RelativeNext Value:                               0    1  ...
       */
        switch (intent) {
            case SubscribeIntent::immediate:
                return std::make_tuple(Location{.mode=LocationMode::RelativePrevious, .value=0}, //StartGroup
                                       none_location, // EndGroup
                                       Location{.mode=LocationMode::RelativePrevious, .value=0}, //StartObject
                                       none_location); // EndObject
            case SubscribeIntent::sync_up:
                throw std::runtime_error("Intent Unsupported for Subscribe");
            case SubscribeIntent::wait_up:
                throw std::runtime_error("Intent Unsupported for Subscribe");
            default:
                throw std::runtime_error("Bad Intent  for Subscribe");
        }

    }


    // Setup

    // vector<varint> encode/decode
    MessageBuffer&
    operator<<(MessageBuffer& buffer, const std::vector<uintVar_t>& val)
    {
        buffer << static_cast<uintVar_t>(val.size());
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


    // Client Setup message
    MessageBuffer &
    operator<<(MessageBuffer &buffer, const MoqClientSetup &msg) {
        buffer << static_cast<uintVar_t>(MESSAGE_TYPE_CLIENT_SETUP);
        // versions
        buffer << static_cast<uintVar_t>(msg.supported_versions.size());
        for (const auto& ver: msg.supported_versions) {
            buffer << static_cast<uintVar_t>(ver);
        }

        // TODO (Suhas): Add support for PATH Param
        // num params
        buffer << uintVar_t{1};
        // role param
        buffer << static_cast<uint8_t>(msg.role_parameter.param_type);
        buffer << uintVar_t(1);
        buffer << msg.role_parameter.param_value;

        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqClientSetup &msg) {
        uintVar_t num_versions {0};
        buffer >> num_versions;
        msg.supported_versions.resize(num_versions);
        for(size_t i = 0; i < num_versions; i++) {
            uintVar_t version{0};
            buffer >> version;
            msg.supported_versions.push_back(version);
        }

        uintVar_t num_params {0};
        buffer >> num_params;
        if (static_cast<uint64_t> (num_params) == 0) {
            return buffer;
        }

        while (static_cast<uint64_t>(num_params) > 0) {
            uint8_t param_type {0};
            buffer >> param_type;
            auto param_type_enum = static_cast<ParameterType> (param_type);
            switch(param_type_enum) {
                case ParameterType::Role: {
                    msg.role_parameter.param_type = param_type;
                    buffer >> msg.role_parameter.param_length;
                    buffer >> msg.role_parameter.param_value;
                }
                break;
                case ParameterType::Path: {
                    msg.path_parameter.param_type = param_type;
                    buffer >> msg.path_parameter.param_length;
                    buffer >> msg.path_parameter.param_value;
                }
                break;
                default:
                    throw std::runtime_error("Unsupported Parameter Type for ClientSetup");
            }
            num_params = num_params - 1;
        }


        return buffer;
    }

    // Server Setup message
    MessageBuffer&
    operator<<(MessageBuffer &buffer, const MoqServerSetup &msg) {
        buffer << static_cast<uintVar_t>(MESSAGE_TYPE_SERVER_SETUP);
        buffer << msg.supported_version;
        return buffer;
    }

    MessageBuffer &
    operator>>(MessageBuffer &buffer, MoqServerSetup &msg) {
        buffer >> msg.supported_version;
        return buffer;
    }
}