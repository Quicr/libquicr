#include <doctest/doctest.h>

#include <quicr/moq_message_types.h>
#include <quicr/message_buffer.h>
#include <quicr/quicr_common.h>
#include <quicr/uvarint.h>
#include <quicr/encode.h>

#include <memory>
#include <sstream>
#include <string>
#include <iostream>

using namespace quicr;
using namespace quicr::messages;

static bytes from_ascii(const std::string& ascii)
{
  return std::vector<uint8_t>(ascii.begin(), ascii.end());
}

const quicr::bytes TRACK_NAMESPACE_CONF = from_ascii("moqt://conf.example.com/conf/1");
const quicr::bytes TRACK_NAME_ALICE_VIDEO = from_ascii("alice/video");
const uintVar_t TRACK_ALIAS_ALICE_VIDEO = 0xA11CE;

TEST_CASE("Announce Message encode/decode")
{
  auto announce  = MoqAnnounce {
    .track_namespace = TRACK_NAMESPACE_CONF,
  };

  MessageBuffer buffer;
  buffer <<  announce;

  uint8_t msg_type {0};
  buffer >> msg_type;
  CHECK_EQ(msg_type, MESSAGE_TYPE_ANNOUNCE);

  MoqAnnounce announce_out;
  buffer >> announce_out;
  CHECK_EQ(TRACK_NAMESPACE_CONF, announce_out.track_namespace);
  CHECK_EQ(0, announce_out.params.size());
}

TEST_CASE("AnnounceOk Message") {

  auto announce_ok = MoqAnnounceOk {
    .track_namespace = TRACK_NAMESPACE_CONF };

  MessageBuffer buffer{};
  buffer << announce_ok;

  uint8_t msg_type {0};
  buffer >> msg_type;
  CHECK_EQ(msg_type, MESSAGE_TYPE_ANNOUNCE_OK);

  MoqAnnounceOk announce_ok_out;
  buffer >> announce_ok_out;
  CHECK_EQ(TRACK_NAMESPACE_CONF, announce_ok_out.track_namespace);

}

TEST_CASE("AnnounceError Message") {
  auto announce_err = MoqAnnounceError {
    .track_namespace = TRACK_NAMESPACE_CONF,
    .err_code = {0},
    .reason_phrase = from_ascii("All Good Here"),
  };

  MessageBuffer buffer{};
  buffer << announce_err;

  uint8_t msg_type {0};
  buffer >> msg_type;
  CHECK_EQ(msg_type, MESSAGE_TYPE_ANNOUNCE_ERROR);

  MoqAnnounceError announce_err_out;
  buffer >> announce_err_out;
  CHECK_EQ(announce_err.track_namespace, announce_err_out.track_namespace);
  CHECK_EQ(announce_err.err_code, announce_err_out.err_code);
  CHECK_EQ(announce_err.reason_phrase, announce_err_out.reason_phrase);
}

TEST_CASE("Unannounce Message") {
  auto unannounce = MoqUnannounce {
    .track_namespace = TRACK_NAMESPACE_CONF,
  };

  MessageBuffer buffer{};
  buffer << unannounce;

  uint8_t msg_type {0};
  buffer >> msg_type;
  CHECK_EQ(msg_type, MESSAGE_TYPE_UNANNOUNCE);

  MoqUnannounce unannounce_out;
  buffer >> unannounce_out;
  CHECK_EQ(unannounce.track_namespace, unannounce_out.track_namespace);
}

TEST_CASE("AnnounceCancel Message") {
  auto announce_cancel = MoqAnnounceCancel {
    .track_namespace = TRACK_NAMESPACE_CONF,
  };

  MessageBuffer buffer{};
  buffer << announce_cancel;

  uint8_t msg_type {0};
  buffer >> msg_type;
  CHECK_EQ(msg_type, MESSAGE_TYPE_ANNOUNCE_CANCEL);

  MoqAnnounceCancel announce_cancel_out;
  buffer >> announce_cancel_out;
  CHECK_EQ(announce_cancel_out.track_namespace, announce_cancel_out.track_namespace);
}

TEST_CASE("Subscribe Message encode/decode")
{
    // TODO (Suhas): re-test once we have params.
    auto subscribe  = MoqSubscribe {
        .subscribe_id = 1,
        .track_alias = TRACK_ALIAS_ALICE_VIDEO,
        .track_namespace = TRACK_NAMESPACE_CONF,
        .track_name = TRACK_NAME_ALICE_VIDEO,
        .start_group = Location { .mode = LocationMode::Absolute, .value = 100},
        .start_object = Location {.mode = LocationMode::Absolute, .value = 0},
        .end_group = Location { .mode = LocationMode::Absolute, .value = 1000},
        .end_object = Location {.mode = LocationMode::Absolute, .value = 0},
    };

    MessageBuffer buffer;
    buffer <<  subscribe;

    uint8_t msg_type {0};
    buffer >> msg_type;
    CHECK_EQ(msg_type, MESSAGE_TYPE_SUBSCRIBE);

    MoqSubscribe subscribe_out;
    buffer >> subscribe_out;
    CHECK_EQ(subscribe.subscribe_id, subscribe_out.subscribe_id);
    CHECK_EQ(subscribe.track_alias, subscribe_out.track_alias);
    CHECK_EQ(subscribe.track_namespace, subscribe_out.track_namespace);
    CHECK_EQ(subscribe.track_name, subscribe_out.track_name);
    CHECK_EQ(subscribe.start_group.mode, subscribe_out.start_group.mode);
    CHECK_EQ(subscribe.start_object.mode, subscribe_out.start_object.mode);
    CHECK_EQ(subscribe.end_group.value, subscribe_out.end_group.value);
    CHECK_EQ(subscribe.end_object.value, subscribe_out.end_object.value);
}


TEST_CASE("SubscribeOk Message encode/decode") {
    auto subscribe_ok = MoqSubscribeOk {
      .subscribe_id = 1,
      .expires = 0,
      .content_exists = true,
      .largest_group = 0xAAAA,
      .largest_object = 0xBBBB
    };

    MessageBuffer buffer;
    buffer <<  subscribe_ok;

    uint8_t msg_type {0};
    buffer >> msg_type;
    CHECK_EQ(msg_type, MESSAGE_TYPE_SUBSCRIBE_OK);

    MoqSubscribeOk subscribe_ok_out;
    buffer >> subscribe_ok_out;
    CHECK_EQ(subscribe_ok_out.subscribe_id, subscribe_ok_out.subscribe_id);
    CHECK_EQ(subscribe_ok_out.expires, subscribe_ok_out.expires);
    CHECK_EQ(subscribe_ok_out.content_exists, subscribe_ok_out.content_exists);
    CHECK_EQ(subscribe_ok_out.largest_object, subscribe_ok_out.largest_object);
    CHECK_EQ(subscribe_ok_out.largest_group, subscribe_ok_out.largest_object);
}

TEST_CASE("SubscribeOk No Content, Message encode/decode") {
    auto subscribe_ok = MoqSubscribeOk {
      .subscribe_id = 1,
      .expires = 0,
      .content_exists = false,
    };

    MessageBuffer buffer;
    buffer <<  subscribe_ok;

    uint8_t msg_type {0};
    buffer >> msg_type;
    CHECK_EQ(msg_type, MESSAGE_TYPE_SUBSCRIBE_OK);

    MoqSubscribeOk subscribe_ok_out;
    buffer >> subscribe_ok_out;
    CHECK_EQ(subscribe_ok_out.subscribe_id, subscribe_ok_out.subscribe_id);
    CHECK_EQ(subscribe_ok_out.expires, subscribe_ok_out.expires);
    CHECK_EQ(subscribe_ok_out.content_exists, subscribe_ok_out.content_exists);
}

TEST_CASE("SubscribeError Message encode/decode") {
    auto subscribe_err = MoqSubscribeError {
      .subscribe_id = 1,
      .err_code = 0x100,
      .reason_phrase = from_ascii("This is an error"),
      .track_alias = 0x1111,
    };

    MessageBuffer buffer;
    buffer <<  subscribe_err;

    uint8_t msg_type {0};
    buffer >> msg_type;
    CHECK_EQ(msg_type, MESSAGE_TYPE_SUBSCRIBE_ERROR);

    MoqSubscribeError subscribe_err_out;
    buffer >> subscribe_err_out;
    CHECK_EQ(subscribe_err.subscribe_id, subscribe_err_out.subscribe_id);
    CHECK_EQ(subscribe_err.err_code, subscribe_err_out.err_code);
    CHECK_EQ(subscribe_err.reason_phrase, subscribe_err_out.reason_phrase);
    CHECK_EQ(subscribe_err.track_alias, subscribe_err_out.track_alias);
}


TEST_CASE("Unsubscribe Message encode/decode") {
    auto unsubscribe = MoqUnsubscribe {
      .subscribe_id = 1,
    };

    MessageBuffer buffer;
    buffer <<  unsubscribe;

    uint8_t msg_type {0};
    buffer >> msg_type;
    CHECK_EQ(msg_type, MESSAGE_TYPE_UNSUBSCRIBE);

    MoqUnsubscribe unsubscribe_out;
    buffer >> unsubscribe_out;
    CHECK_EQ(unsubscribe.subscribe_id, unsubscribe_out.subscribe_id);
}

TEST_CASE("SubscribeDone Message encode/decode") {
    auto subscribe_done = MoqSubscribeDone {
      .subscribe_id = 1,
      .status_code = 0x0,
      .reason_phrase = quicr::bytes{},
      .content_exists = true,
      .final_group_id = 0x1111,
      .final_object_id = 0x2222,
    };

    MessageBuffer buffer;
    buffer <<  subscribe_done;

    uint8_t msg_type {0};
    buffer >> msg_type;
    CHECK_EQ(msg_type, MESSAGE_TYPE_SUBSCRIBE_DONE);

    MoqSubscribeDone subscribe_done_out;
    buffer >> subscribe_done_out;
    CHECK_EQ(subscribe_done.subscribe_id, subscribe_done_out.subscribe_id);
    CHECK_EQ(subscribe_done.status_code, subscribe_done_out.status_code);
    CHECK_EQ(subscribe_done.reason_phrase, subscribe_done_out.reason_phrase);
    CHECK_EQ(subscribe_done.reason_phrase.empty(), subscribe_done_out.reason_phrase.empty());
    CHECK_EQ(subscribe_done.content_exists, subscribe_done_out.content_exists);
    CHECK_EQ(subscribe_done.final_object_id, subscribe_done_out.final_object_id);
    CHECK_EQ(subscribe_done.final_group_id, subscribe_done_out.final_group_id);
}

TEST_CASE("ObjectStream Message encode/decode")
{
    quicr::Namespace qnamespace{0x10000000000000002000_name, 125};
    std::stringstream ss;
    ss << qnamespace;
    auto ns = "moq://" + ss.str();
    auto object = MoqObjectStream{};
    object.track_alias = 109955458826288;
    object.priority = 1;
    object.object_id = 0;
    object.group_id = 0;
    object.subscribe_id = 0xABCD;
    object.payload = quicr::bytes{1, 2, 3, 4, 5};

    MessageBuffer buffer;
    buffer << object;
    MoqObjectStream object_out;
    buffer >> object_out;
    CHECK_EQ(object.track_alias, object_out.track_alias);
    CHECK_EQ(object.payload.size(), object_out.payload.size());
}


struct A {
  quicr::uintVar_t val;
};

MessageBuffer &
operator<<(MessageBuffer &msg, const struct A &value) {
    msg << value.val;
    return msg;
}

MessageBuffer &
operator>>(MessageBuffer &msg, struct A &value) {
    struct A a {};
    msg >> a.val;
    value.val = a.val;
    return msg;
}

TEST_CASE("QUIC Varint") {
    A a;
    a.val = 15293;

    MessageBuffer buffer;
    buffer << a;
    A a_out;
    buffer >> a_out;
    CHECK_EQ(a.val, a_out.val);

    //.start_group = Location { .mode = LocationMode::Absolute, .value = 100},
    Location l_in = {
       .mode = LocationMode::Absolute,
       .value = 100,
    };

    buffer << l_in;
    Location l_out;
    buffer >> l_out;
    CHECK_EQ(l_in.mode, l_out.mode);

}


TEST_CASE("Client Setup encode/decode") {
    auto msg_in = messages::MoqClientSetup {
        .supported_versions =  { 0x1 },
        .role_parameter { .param_type = static_cast<uint8_t>(ParameterType::Role),
                          .param_length = uintVar_t{1},
                          .param_value = quicr::bytes{0x03}},
    };

    messages::MessageBuffer buffer;
    buffer << msg_in;
    uint8_t  msg_type {0};
    buffer >> msg_type;
    messages::MoqClientSetup msg_out{};
    buffer >> msg_out;

    CHECK_EQ(msg_out.role_parameter.param_type, msg_in.role_parameter.param_type);
    CHECK_EQ(msg_out.role_parameter.param_length, msg_in.role_parameter.param_length);
    CHECK_EQ(msg_out.role_parameter.param_value, msg_in.role_parameter.param_value);


}