#include <doctest/doctest.h>
#include <memory>
#include <sstream>
#include <string>
#include <iostream>
#include <quicr/moq_messages.h>

using namespace quicr;
using namespace quicr::messages;

static bytes from_ascii(const std::string& ascii)
{
  return std::vector<uint8_t>(ascii.begin(), ascii.end());
}

const quicr::bytes TRACK_NAMESPACE_CONF = from_ascii("moqt://conf.example.com/conf/1");
const quicr::bytes TRACK_NAME_ALICE_VIDEO = from_ascii("alice/video");
const uintVar_t TRACK_ALIAS_ALICE_VIDEO = 0xA11CE;

template<typename T>
bool verify(std::vector<uint8_t>& net_data, uint64_t message_type, T& message, size_t slice_depth=1) {
  // TODO: support size_depth > 1, if needed
  qtransport::StreamBuffer<uint8_t> in_buffer;
  std::optional<uint64_t> msg_type;
  bool done = false;
  for (auto& v: net_data) {
    in_buffer.push(v);
    if (!msg_type) {
      msg_type = in_buffer.decode_uintV();
      if (!msg_type) {
        continue;
      }
      CHECK_EQ(*msg_type, message_type);
      continue;
    }

    done = in_buffer >> message;
    if (done) {
      break;
    }
  }

  return done;
}

TEST_CASE("AnnounceOk Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto announce_ok  = MoqAnnounceOk {};
  announce_ok.track_namespace = TRACK_NAMESPACE_CONF;
  buffer << announce_ok;

  std::vector<uint8_t> net_data = buffer.front(buffer.size());
  MoqAnnounceOk announce_ok_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MESSAGE_TYPE_ANNOUNCE_OK), announce_ok_out));
  CHECK_EQ(TRACK_NAMESPACE_CONF, announce_ok_out.track_namespace);
}

TEST_CASE("Announce Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto announce  = MoqAnnounce {};
  announce.track_namespace = TRACK_NAMESPACE_CONF;
  announce.params = {};
  buffer <<  announce;
  std::vector<uint8_t> net_data = buffer.front(buffer.size());
  MoqAnnounce announce_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MESSAGE_TYPE_ANNOUNCE), announce_out));
  CHECK_EQ(TRACK_NAMESPACE_CONF, announce_out.track_namespace);
  CHECK_EQ(0, announce_out.params.size());
}

TEST_CASE("Unannounce Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto unannounce  = MoqUnannounce {};
  unannounce.track_namespace = TRACK_NAMESPACE_CONF;
  buffer << unannounce;

  std::vector<uint8_t> net_data = buffer.front(buffer.size());
  MoqAnnounceOk announce_ok_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MESSAGE_TYPE_UNANNOUNCE), announce_ok_out));
  CHECK_EQ(TRACK_NAMESPACE_CONF, announce_ok_out.track_namespace);
}

TEST_CASE("AnnounceError Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto announce_err  = MoqAnnounceError {};
  announce_err.track_namespace = TRACK_NAMESPACE_CONF;
  announce_err.err_code = 0x1234;
  announce_err.reason_phrase = quicr::bytes{0x1,0x2,0x3};
  buffer << announce_err;

  std::vector<uint8_t> net_data = buffer.front(buffer.size());
  MoqAnnounceError announce_err_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MESSAGE_TYPE_ANNOUNCE_ERROR), announce_err_out));
  CHECK_EQ(TRACK_NAMESPACE_CONF, announce_err_out.track_namespace);
  CHECK_EQ(announce_err.err_code, announce_err_out.err_code);
  CHECK_EQ(announce_err.reason_phrase, announce_err_out.reason_phrase);
}

TEST_CASE("AnnounceCancel Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto announce_cancel  = MoqAnnounceCancel {};
  announce_cancel.track_namespace = TRACK_NAMESPACE_CONF;
  buffer << announce_cancel;

  std::vector<uint8_t> net_data = buffer.front(buffer.size());
  MoqAnnounceCancel announce_cancel_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MESSAGE_TYPE_ANNOUNCE_CANCEL), announce_cancel_out));
  CHECK_EQ(TRACK_NAMESPACE_CONF, announce_cancel_out.track_namespace);
}

TEST_CASE("Subscribe (LatestObject) Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto subscribe  = MoqSubscribe {};
  subscribe.subscribe_id = 0x1;
  subscribe.track_alias = TRACK_ALIAS_ALICE_VIDEO;
  subscribe.track_namespace = TRACK_NAMESPACE_CONF;
  subscribe.track_name = TRACK_NAME_ALICE_VIDEO;
  subscribe.filter_type = FilterType::LatestObject;
  subscribe.num_params = 0;

  buffer << subscribe;

  std::vector<uint8_t> net_data = buffer.front(buffer.size());

  MoqSubscribe subscribe_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MESSAGE_TYPE_SUBSCRIBE), subscribe_out));
  CHECK_EQ(TRACK_NAMESPACE_CONF, subscribe_out.track_namespace);
  CHECK_EQ(TRACK_NAME_ALICE_VIDEO, subscribe_out.track_name);
  CHECK_EQ(subscribe.subscribe_id.value(), subscribe_out.subscribe_id.value());
  CHECK_EQ(subscribe.track_alias.value(), subscribe_out.track_alias.value());
  CHECK_EQ(subscribe.num_params, subscribe_out.num_params);
  CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
}

TEST_CASE("Subscribe (LatestGroup) Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto subscribe  = MoqSubscribe {};
  subscribe.subscribe_id = 0x1;
  subscribe.track_alias = TRACK_ALIAS_ALICE_VIDEO;
  subscribe.track_namespace = TRACK_NAMESPACE_CONF;
  subscribe.track_name = TRACK_NAME_ALICE_VIDEO;
  subscribe.filter_type = FilterType::LatestGroup;
  subscribe.num_params = 0;

  buffer << subscribe;

  std::vector<uint8_t> net_data = buffer.front(buffer.size());

  MoqSubscribe subscribe_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MESSAGE_TYPE_SUBSCRIBE), subscribe_out));
  CHECK_EQ(TRACK_NAMESPACE_CONF, subscribe_out.track_namespace);
  CHECK_EQ(TRACK_NAME_ALICE_VIDEO, subscribe_out.track_name);
  CHECK_EQ(subscribe.subscribe_id.value(), subscribe_out.subscribe_id.value());
  CHECK_EQ(subscribe.track_alias.value(), subscribe_out.track_alias.value());
  CHECK_EQ(subscribe.num_params, subscribe_out.num_params);
  CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
}

TEST_CASE("Subscribe (AbsoluteStart) Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto subscribe  = MoqSubscribe {};
  subscribe.subscribe_id = 0x1;
  subscribe.track_alias = TRACK_ALIAS_ALICE_VIDEO;
  subscribe.track_namespace = TRACK_NAMESPACE_CONF;
  subscribe.track_name = TRACK_NAME_ALICE_VIDEO;
  subscribe.filter_type = FilterType::AbsoluteStart;
  subscribe.start_group = 0x1000;
  subscribe.start_object = 0xFF;
  subscribe.num_params = 0;

  buffer << subscribe;

  std::vector<uint8_t> net_data = buffer.front(buffer.size());

  MoqSubscribe subscribe_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MESSAGE_TYPE_SUBSCRIBE), subscribe_out));
  CHECK_EQ(TRACK_NAMESPACE_CONF, subscribe_out.track_namespace);
  CHECK_EQ(TRACK_NAME_ALICE_VIDEO, subscribe_out.track_name);
  CHECK_EQ(subscribe.subscribe_id.value(), subscribe_out.subscribe_id.value());
  CHECK_EQ(subscribe.track_alias.value(), subscribe_out.track_alias.value());
  CHECK_EQ(subscribe.num_params, subscribe_out.num_params);
  CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
  CHECK_EQ(subscribe.start_group, subscribe_out.start_group);
  CHECK_EQ(subscribe.start_object, subscribe_out.start_object);
}

TEST_CASE("Subscribe (AbsoluteRange) Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto subscribe  = MoqSubscribe {};
  subscribe.subscribe_id = 0x1;
  subscribe.track_alias = TRACK_ALIAS_ALICE_VIDEO;
  subscribe.track_namespace = TRACK_NAMESPACE_CONF;
  subscribe.track_name = TRACK_NAME_ALICE_VIDEO;
  subscribe.filter_type = FilterType::AbsoluteRange;
  subscribe.start_group = 0x1000;
  subscribe.start_object = 0x1;
  subscribe.end_group = 0xFFF;
  subscribe.end_object = 0xFF;

  subscribe.num_params = 0;

  buffer << subscribe;

  std::vector<uint8_t> net_data = buffer.front(buffer.size());

  MoqSubscribe subscribe_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MESSAGE_TYPE_SUBSCRIBE), subscribe_out));
  CHECK_EQ(TRACK_NAMESPACE_CONF, subscribe_out.track_namespace);
  CHECK_EQ(TRACK_NAME_ALICE_VIDEO, subscribe_out.track_name);
  CHECK_EQ(subscribe.subscribe_id.value(), subscribe_out.subscribe_id.value());
  CHECK_EQ(subscribe.track_alias.value(), subscribe_out.track_alias.value());
  CHECK_EQ(subscribe.num_params, subscribe_out.num_params);
  CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
  CHECK_EQ(subscribe.start_group, subscribe_out.start_group);
  CHECK_EQ(subscribe.start_object, subscribe_out.start_object);
  CHECK_EQ(subscribe.end_group, subscribe_out.end_group);
  CHECK_EQ(subscribe.end_object, subscribe_out.end_object);
}

TEST_CASE("Subscribe (Params) Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;
  MoqParameter param;
  param.param_type = static_cast<uint64_t>(ParameterType::AuthorizationInfo),
  param.param_length = 0x2;
  param.param_value = {0x1, 0x2};

  auto subscribe  = MoqSubscribe {};
  subscribe.subscribe_id = 0x1;
  subscribe.track_alias = TRACK_ALIAS_ALICE_VIDEO;
  subscribe.track_namespace = TRACK_NAMESPACE_CONF;
  subscribe.track_name = TRACK_NAME_ALICE_VIDEO;
  subscribe.filter_type = FilterType::LatestObject;
  subscribe.num_params = 1;
  subscribe.track_params.push_back(param);
  buffer << subscribe;

  std::vector<uint8_t> net_data = buffer.front(buffer.size());

  MoqSubscribe subscribe_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MESSAGE_TYPE_SUBSCRIBE), subscribe_out));
  CHECK_EQ(TRACK_NAMESPACE_CONF, subscribe_out.track_namespace);
  CHECK_EQ(TRACK_NAME_ALICE_VIDEO, subscribe_out.track_name);
  CHECK_EQ(subscribe.subscribe_id.value(), subscribe_out.subscribe_id.value());
  CHECK_EQ(subscribe.track_alias.value(), subscribe_out.track_alias.value());
  CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
  CHECK_EQ(subscribe.track_params.size(), subscribe_out.track_params.size());
  CHECK_EQ(subscribe.track_params[0].param_type, subscribe_out.track_params[0].param_type);
  CHECK_EQ(subscribe.track_params[0].param_length, subscribe_out.track_params[0].param_length);
  CHECK_EQ(subscribe.track_params[0].param_value, subscribe_out.track_params[0].param_value);
}