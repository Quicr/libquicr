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
  CHECK_EQ(subscribe.subscribe_id, subscribe_out.subscribe_id);
  CHECK_EQ(subscribe.track_alias, subscribe_out.track_alias);
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
  CHECK_EQ(subscribe.subscribe_id, subscribe_out.subscribe_id);
  CHECK_EQ(subscribe.track_alias, subscribe_out.track_alias);
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
  CHECK_EQ(subscribe.subscribe_id, subscribe_out.subscribe_id);
  CHECK_EQ(subscribe.track_alias, subscribe_out.track_alias);
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
  CHECK_EQ(subscribe.subscribe_id, subscribe_out.subscribe_id);
  CHECK_EQ(subscribe.track_alias, subscribe_out.track_alias);
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
  CHECK_EQ(subscribe.subscribe_id, subscribe_out.subscribe_id);
  CHECK_EQ(subscribe.track_alias, subscribe_out.track_alias);
  CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
  CHECK_EQ(subscribe.track_params.size(), subscribe_out.track_params.size());
  CHECK_EQ(subscribe.track_params[0].param_type, subscribe_out.track_params[0].param_type);
  CHECK_EQ(subscribe.track_params[0].param_length, subscribe_out.track_params[0].param_length);
  CHECK_EQ(subscribe.track_params[0].param_value, subscribe_out.track_params[0].param_value);
}


TEST_CASE("Subscribe (Params - 2) Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;
  MoqParameter param1;
  param1.param_type = static_cast<uint64_t>(ParameterType::AuthorizationInfo);
  param1.param_length = 0x2;
  param1.param_value = {0x1, 0x2};

  MoqParameter param2;
  param2.param_type = static_cast<uint64_t>(ParameterType::AuthorizationInfo);
  param2.param_length = 0x3;
  param2.param_value = {0x1, 0x2, 0x3};


  auto subscribe  = MoqSubscribe {};
  subscribe.subscribe_id = 0x1;
  subscribe.track_alias = TRACK_ALIAS_ALICE_VIDEO;
  subscribe.track_namespace = TRACK_NAMESPACE_CONF;
  subscribe.track_name = TRACK_NAME_ALICE_VIDEO;
  subscribe.filter_type = FilterType::LatestObject;
  subscribe.num_params = 2;
  subscribe.track_params.push_back(param1);
  subscribe.track_params.push_back(param2);
  buffer << subscribe;

  std::vector<uint8_t> net_data = buffer.front(buffer.size());

  MoqSubscribe subscribe_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MESSAGE_TYPE_SUBSCRIBE), subscribe_out));
  CHECK_EQ(TRACK_NAMESPACE_CONF, subscribe_out.track_namespace);
  CHECK_EQ(TRACK_NAME_ALICE_VIDEO, subscribe_out.track_name);
  CHECK_EQ(subscribe.subscribe_id, subscribe_out.subscribe_id);
  CHECK_EQ(subscribe.track_alias, subscribe_out.track_alias);
  CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
  CHECK_EQ(subscribe.track_params.size(), subscribe_out.track_params.size());
  CHECK_EQ(subscribe.track_params[0].param_type, subscribe_out.track_params[0].param_type);
  CHECK_EQ(subscribe.track_params[0].param_length, subscribe_out.track_params[0].param_length);
  CHECK_EQ(subscribe.track_params[0].param_value, subscribe_out.track_params[0].param_value);

  CHECK_EQ(subscribe.track_params[1].param_type, subscribe_out.track_params[1].param_type);
  CHECK_EQ(subscribe.track_params[1].param_length, subscribe_out.track_params[1].param_length);
  CHECK_EQ(subscribe.track_params[1].param_value, subscribe_out.track_params[1].param_value);
}


MoqSubscribe generate_subscribe(FilterType filter, size_t  num_params = 0, uint64_t sg = 0, uint64_t so=0,
                   uint64_t eg=0, uint64_t eo=0) {
  MoqSubscribe out;
  out.subscribe_id = 0xABCD;
  out.track_alias = TRACK_ALIAS_ALICE_VIDEO;
  out.track_namespace = TRACK_NAMESPACE_CONF;
  out.track_name = TRACK_NAME_ALICE_VIDEO;
  out.filter_type = filter;
  out.num_params = num_params;
  switch (filter) {
    case FilterType::LatestObject:
    case FilterType::LatestGroup:
      break;
    case FilterType::AbsoluteStart:
      out.start_group = sg;
      out.start_object = so;
      break;
    case FilterType::AbsoluteRange:
      out.start_group = sg;
      out.start_object = so;
      out.end_group = eg;
      out.end_object = eo;
      break;
  }

  while(num_params > 0) {
    MoqParameter param1;
    param1.param_type = static_cast<uint64_t>(ParameterType::AuthorizationInfo);
    param1.param_length = 0x2;
    param1.param_value = {0x1, 0x2};
    out.track_params.push_back(param1);
    num_params--;
  }
  return out;
}

TEST_CASE("Subscribe (Combo) Message encode/decode")
{
  auto subscribes = std::vector<MoqSubscribe> {
    generate_subscribe(FilterType::LatestObject),
    generate_subscribe(FilterType::LatestGroup),
    generate_subscribe(FilterType::LatestObject, 1),
    generate_subscribe(FilterType::LatestGroup, 2),
    generate_subscribe(FilterType::AbsoluteStart, 0, 0x100, 0x2),
    generate_subscribe(FilterType::AbsoluteStart, 2, 0x100, 0x2),
    generate_subscribe(FilterType::AbsoluteRange, 0, 0x100, 0x2, 0x500, 0x2),
    generate_subscribe(FilterType::AbsoluteRange, 2, 0x100, 0x2, 0x500, 0x2),
  };

  for (size_t i = 0; i < subscribes.size(); i++) {
    qtransport::StreamBuffer<uint8_t> buffer;
    buffer << subscribes[i];
    std::vector<uint8_t> net_data = buffer.front(buffer.size());
    MoqSubscribe subscribe_out;
    CHECK(verify(
      net_data, static_cast<uint64_t>(MESSAGE_TYPE_SUBSCRIBE), subscribe_out));
    CHECK_EQ(TRACK_NAMESPACE_CONF, subscribe_out.track_namespace);
    CHECK_EQ(TRACK_NAME_ALICE_VIDEO, subscribe_out.track_name);
    CHECK_EQ(subscribes[i].subscribe_id, subscribe_out.subscribe_id);
    CHECK_EQ(subscribes[i].track_alias, subscribe_out.track_alias);
    CHECK_EQ(subscribes[i].filter_type, subscribe_out.filter_type);
    CHECK_EQ(subscribes[i].track_params.size(), subscribe_out.track_params.size());
    for(size_t j = 0; j < subscribes[i].track_params.size(); j++) {
      CHECK_EQ(subscribes[i].track_params[j].param_type,
               subscribe_out.track_params[j].param_type);
      CHECK_EQ(subscribes[i].track_params[j].param_length,
               subscribe_out.track_params[j].param_length);
      CHECK_EQ(subscribes[i].track_params[j].param_value,
               subscribe_out.track_params[j].param_value);

    }
  }
}


TEST_CASE("SubscribeOk Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto subscribe_ok  = MoqSubscribeOk {};
  subscribe_ok.subscribe_id = 0x1;
  subscribe_ok.expires = 0x100;
  subscribe_ok.content_exists = false;
  buffer << subscribe_ok;

  std::vector<uint8_t> net_data = buffer.front(buffer.size());

  MoqSubscribeOk subscribe_ok_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MESSAGE_TYPE_SUBSCRIBE_OK), subscribe_ok_out));
  CHECK_EQ(subscribe_ok.subscribe_id, subscribe_ok_out.subscribe_id);
  CHECK_EQ(subscribe_ok.expires, subscribe_ok_out.expires);
  CHECK_EQ(subscribe_ok.content_exists, subscribe_ok_out.content_exists);
}


TEST_CASE("SubscribeOk (content-exists) Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto subscribe_ok  = MoqSubscribeOk {};
  subscribe_ok.subscribe_id = 0x1;
  subscribe_ok.expires = 0x100;
  subscribe_ok.content_exists = true;
  subscribe_ok.largest_group = 0x1000;
  subscribe_ok.largest_object = 0xff;
  buffer << subscribe_ok;

  std::vector<uint8_t> net_data = buffer.front(buffer.size());

  MoqSubscribeOk subscribe_ok_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MESSAGE_TYPE_SUBSCRIBE_OK), subscribe_ok_out));
  CHECK_EQ(subscribe_ok.subscribe_id, subscribe_ok_out.subscribe_id);
  CHECK_EQ(subscribe_ok.expires, subscribe_ok_out.expires);
  CHECK_EQ(subscribe_ok.content_exists, subscribe_ok_out.content_exists);
  CHECK_EQ(subscribe_ok.largest_group, subscribe_ok_out.largest_group);
  CHECK_EQ(subscribe_ok.largest_object, subscribe_ok_out.largest_object);
}

TEST_CASE("SubscribeError  Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto subscribe_err  = MoqSubscribeError {};
  subscribe_err.subscribe_id = 0x1;
  subscribe_err.err_code = 0;
  subscribe_err.reason_phrase = quicr::bytes {0x0, 0x1};
  subscribe_err.track_alias = TRACK_ALIAS_ALICE_VIDEO;
  buffer << subscribe_err;

  std::vector<uint8_t> net_data = buffer.front(buffer.size());

  MoqSubscribeError subscribe_err_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MESSAGE_TYPE_SUBSCRIBE_ERROR), subscribe_err_out));
  CHECK_EQ(subscribe_err.subscribe_id, subscribe_err_out.subscribe_id);
  CHECK_EQ(subscribe_err.err_code, subscribe_err_out.err_code);
  CHECK_EQ(subscribe_err.reason_phrase, subscribe_err_out.reason_phrase);
  CHECK_EQ(subscribe_err.track_alias, subscribe_err_out.track_alias);
}

TEST_CASE("Unsubscribe  Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto unsubscribe  = MoqUnsubscribe {};
  unsubscribe.subscribe_id = 0x1;
  buffer << unsubscribe;

  std::vector<uint8_t> net_data = buffer.front(buffer.size());

  MoqUnsubscribe unsubscribe_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MESSAGE_TYPE_UNSUBSCRIBE), unsubscribe_out));
  CHECK_EQ(unsubscribe.subscribe_id, unsubscribe_out.subscribe_id);
}

TEST_CASE("SubscribeDone  Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto subscribe_done  = MoqSubscribeDone {};
  subscribe_done.subscribe_id = 0x1;
  subscribe_done.status_code = 0x0;
  subscribe_done.reason_phrase = quicr::bytes {0x0};
  subscribe_done.content_exists = false;

  buffer << subscribe_done;

  std::vector<uint8_t> net_data = buffer.front(buffer.size());

  MoqSubscribeDone subscribe_done_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MESSAGE_TYPE_SUBSCRIBE_DONE), subscribe_done_out));
  CHECK_EQ(subscribe_done.subscribe_id, subscribe_done_out.subscribe_id);
  CHECK_EQ(subscribe_done.status_code, subscribe_done_out.status_code);
  CHECK_EQ(subscribe_done.reason_phrase, subscribe_done_out.reason_phrase);
  CHECK_EQ(subscribe_done.content_exists, subscribe_done_out.content_exists);
}

TEST_CASE("SubscribeDone (content-exists)  Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto subscribe_done  = MoqSubscribeDone {};
  subscribe_done.subscribe_id = 0x1;
  subscribe_done.status_code = 0x0;
  subscribe_done.reason_phrase = quicr::bytes {0x0};
  subscribe_done.content_exists = true;
  subscribe_done.final_group_id = 0x1111;
  subscribe_done.final_object_id = 0xff;

  buffer << subscribe_done;

  std::vector<uint8_t> net_data = buffer.front(buffer.size());

  MoqSubscribeDone subscribe_done_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MESSAGE_TYPE_SUBSCRIBE_DONE), subscribe_done_out));
  CHECK_EQ(subscribe_done.subscribe_id, subscribe_done_out.subscribe_id);
  CHECK_EQ(subscribe_done.status_code, subscribe_done_out.status_code);
  CHECK_EQ(subscribe_done.reason_phrase, subscribe_done_out.reason_phrase);
  CHECK_EQ(subscribe_done.content_exists, subscribe_done_out.content_exists);
  CHECK_EQ(subscribe_done.final_group_id, subscribe_done_out.final_group_id);
  CHECK_EQ(subscribe_done.final_object_id, subscribe_done_out.final_object_id);
}


TEST_CASE("ClientSetup  Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;
  auto client_setup = MoqClientSetup {};
  client_setup.num_versions = 2;
  client_setup.supported_versions = {0x1000, 0x2000};
  client_setup.role_parameter.param_type = static_cast<uint64_t>(ParameterType::Role);
  client_setup.role_parameter.param_length = 0x1;
  client_setup.role_parameter.param_value = {0xFF};

  buffer << client_setup;

  std::vector<uint8_t> net_data = buffer.front(buffer.size());

  MoqClientSetup client_setup_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MESSAGE_TYPE_CLIENT_SETUP), client_setup_out));
  CHECK_EQ(client_setup.supported_versions, client_setup_out.supported_versions);
  CHECK_EQ(client_setup.role_parameter.param_value, client_setup_out.role_parameter.param_value);
}


TEST_CASE("ServertSetup  Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;
  auto server_setup = MoqServerSetup {};
  server_setup.selection_version = {0x1000};
  server_setup.role_parameter.param_type = static_cast<uint64_t>(ParameterType::Role);
  server_setup.role_parameter.param_length = 0x1;
  server_setup.role_parameter.param_value = {0xFF};

  buffer << server_setup;

  std::vector<uint8_t> net_data = buffer.front(buffer.size());

  MoqServerSetup server_setup_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MESSAGE_TYPE_SERVER_SETUP), server_setup_out));
  CHECK_EQ(server_setup.selection_version, server_setup_out.selection_version);
  CHECK_EQ(server_setup.role_parameter.param_value, server_setup.role_parameter.param_value);
}