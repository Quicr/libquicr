#include <any>
#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <sys/socket.h>

#include "moq/messages.h"

using namespace moq;
using namespace moq::messages;

static Bytes from_ascii(const std::string& ascii)
{
  return std::vector<uint8_t>(ascii.begin(), ascii.end());
}

const Bytes TRACK_NAMESPACE_CONF = from_ascii("moqt://conf.example.com/conf/1");
const Bytes TRACK_NAME_ALICE_VIDEO = from_ascii("alice/video");
const UintVT TRACK_ALIAS_ALICE_VIDEO { ToUintV(0xA11CE)};

template<typename T>
bool verify(std::vector<uint8_t>& net_data, uint64_t message_type, T& message, size_t slice_depth=1) {
  // TODO: support Size_depth > 1, if needed
  qtransport::StreamBuffer<uint8_t> in_buffer;
  in_buffer.InitAny<T>();    // Set parsed data to be of this type using out param

  std::optional<uint64_t> msg_type;
  bool done = false;

  for (auto& v: net_data) {
    auto &msg = in_buffer.GetAny<T>();
    in_buffer.Push(v);

    if (!msg_type) {
      msg_type = in_buffer.DecodeUintV();
      if (!msg_type) {
        continue;
      }
      CHECK_EQ(*msg_type, message_type);
      continue;
    }

    done = in_buffer >> msg;
    if (done) {
      // copy the working parsed data to out param.
      message = msg;
      break;
    }
  }

  return done;
}

TEST_CASE("AnnounceOk Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto announce_ok  = MoqtAnnounceOk {};
  announce_ok.track_namespace = TRACK_NAMESPACE_CONF;
  buffer << announce_ok;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());
  MoqtAnnounceOk announce_ok_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::ANNOUNCE_OK), announce_ok_out));
  CHECK_EQ(TRACK_NAMESPACE_CONF, announce_ok_out.track_namespace);
}

TEST_CASE("Announce Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto announce  = MoqtAnnounce {};
  announce.track_namespace = TRACK_NAMESPACE_CONF;
  announce.params = {};
  buffer <<  announce;
  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());
  MoqtAnnounce announce_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::ANNOUNCE), announce_out));
  CHECK_EQ(TRACK_NAMESPACE_CONF, announce_out.track_namespace);
  CHECK_EQ(0, announce_out.params.size());
}

TEST_CASE("Unannounce Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto unannounce  = MoqtUnannounce {};
  unannounce.track_namespace = TRACK_NAMESPACE_CONF;
  buffer << unannounce;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());
  MoqtAnnounceOk announce_ok_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::UNANNOUNCE), announce_ok_out));
  CHECK_EQ(TRACK_NAMESPACE_CONF, announce_ok_out.track_namespace);
}

TEST_CASE("AnnounceError Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto announce_err  = MoqtAnnounceError {};
  announce_err.track_namespace = TRACK_NAMESPACE_CONF;
  announce_err.err_code = 0x1234;
  announce_err.reason_phrase = Bytes{0x1,0x2,0x3};
  buffer << announce_err;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());
  MoqtAnnounceError announce_err_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::ANNOUNCE_ERROR), announce_err_out));
  CHECK_EQ(TRACK_NAMESPACE_CONF, announce_err_out.track_namespace);
  CHECK_EQ(announce_err.err_code, announce_err_out.err_code);
  CHECK_EQ(announce_err.reason_phrase, announce_err_out.reason_phrase);
}

TEST_CASE("AnnounceCancel Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto announce_cancel  = MoqtAnnounceCancel {};
  announce_cancel.track_namespace = TRACK_NAMESPACE_CONF;
  buffer << announce_cancel;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());
  MoqtAnnounceCancel announce_cancel_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::ANNOUNCE_CANCEL), announce_cancel_out));
  CHECK_EQ(TRACK_NAMESPACE_CONF, announce_cancel_out.track_namespace);
}

TEST_CASE("Subscribe (LatestObject) Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto subscribe  = MoqtSubscribe {};
  subscribe.subscribe_id = 0x1;
  subscribe.track_alias = ToUint64(TRACK_ALIAS_ALICE_VIDEO);
  subscribe.track_namespace = TRACK_NAMESPACE_CONF;
  subscribe.track_name = TRACK_NAME_ALICE_VIDEO;
  subscribe.filter_type = FilterType::LatestObject;
  subscribe.num_params = 0;

  buffer << subscribe;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());

  MoqtSubscribe subscribe_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::SUBSCRIBE), subscribe_out));
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

  auto subscribe  = MoqtSubscribe {};
  subscribe.subscribe_id = 0x1;
  subscribe.track_alias = ToUint64(TRACK_ALIAS_ALICE_VIDEO);
  subscribe.track_namespace = TRACK_NAMESPACE_CONF;
  subscribe.track_name = TRACK_NAME_ALICE_VIDEO;
  subscribe.filter_type = FilterType::LatestGroup;
  subscribe.num_params = 0;

  buffer << subscribe;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());

  MoqtSubscribe subscribe_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::SUBSCRIBE), subscribe_out));
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

  auto subscribe  = MoqtSubscribe {};
  subscribe.subscribe_id = 0x1;
  subscribe.track_alias = ToUint64(TRACK_ALIAS_ALICE_VIDEO);
  subscribe.track_namespace = TRACK_NAMESPACE_CONF;
  subscribe.track_name = TRACK_NAME_ALICE_VIDEO;
  subscribe.filter_type = FilterType::AbsoluteStart;
  subscribe.start_group = 0x1000;
  subscribe.start_object = 0xFF;
  subscribe.num_params = 0;

  buffer << subscribe;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());

  MoqtSubscribe subscribe_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::SUBSCRIBE), subscribe_out));
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

  auto subscribe  = MoqtSubscribe {};
  subscribe.subscribe_id = 0x1;
  subscribe.track_alias = ToUint64(TRACK_ALIAS_ALICE_VIDEO);
  subscribe.track_namespace = TRACK_NAMESPACE_CONF;
  subscribe.track_name = TRACK_NAME_ALICE_VIDEO;
  subscribe.filter_type = FilterType::AbsoluteRange;
  subscribe.start_group = 0x1000;
  subscribe.start_object = 0x1;
  subscribe.end_group = 0xFFF;
  subscribe.end_object = 0xFF;

  subscribe.num_params = 0;

  buffer << subscribe;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());

  MoqtSubscribe subscribe_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::SUBSCRIBE), subscribe_out));
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
  MoqtParameter param;
  param.type = static_cast<uint64_t>(ParameterType::AuthorizationInfo),
  param.length = 0x2;
  param.value = {0x1, 0x2};

  auto subscribe  = MoqtSubscribe {};
  subscribe.subscribe_id = 0x1;
  subscribe.track_alias = ToUint64(TRACK_ALIAS_ALICE_VIDEO);
  subscribe.track_namespace = TRACK_NAMESPACE_CONF;
  subscribe.track_name = TRACK_NAME_ALICE_VIDEO;
  subscribe.filter_type = FilterType::LatestObject;
  subscribe.num_params = 1;
  subscribe.track_params.push_back(param);
  buffer << subscribe;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());

  MoqtSubscribe subscribe_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::SUBSCRIBE), subscribe_out));
  CHECK_EQ(TRACK_NAMESPACE_CONF, subscribe_out.track_namespace);
  CHECK_EQ(TRACK_NAME_ALICE_VIDEO, subscribe_out.track_name);
  CHECK_EQ(subscribe.subscribe_id, subscribe_out.subscribe_id);
  CHECK_EQ(subscribe.track_alias, subscribe_out.track_alias);
  CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
  CHECK_EQ(subscribe.track_params.size(), subscribe_out.track_params.size());
  CHECK_EQ(subscribe.track_params[0].type, subscribe_out.track_params[0].type);
  CHECK_EQ(subscribe.track_params[0].length, subscribe_out.track_params[0].length);
  CHECK_EQ(subscribe.track_params[0].value, subscribe_out.track_params[0].value);
}


TEST_CASE("Subscribe (Params - 2) Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;
  MoqtParameter param1;
  param1.type = static_cast<uint64_t>(ParameterType::AuthorizationInfo);
  param1.length = 0x2;
  param1.value = {0x1, 0x2};

  MoqtParameter param2;
  param2.type = static_cast<uint64_t>(ParameterType::AuthorizationInfo);
  param2.length = 0x3;
  param2.value = {0x1, 0x2, 0x3};


  auto subscribe  = MoqtSubscribe {};
  subscribe.subscribe_id = 0x1;
  subscribe.track_alias = ToUint64(TRACK_ALIAS_ALICE_VIDEO);
  subscribe.track_namespace = TRACK_NAMESPACE_CONF;
  subscribe.track_name = TRACK_NAME_ALICE_VIDEO;
  subscribe.filter_type = FilterType::LatestObject;
  subscribe.num_params = 2;
  subscribe.track_params.push_back(param1);
  subscribe.track_params.push_back(param2);
  buffer << subscribe;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());

  MoqtSubscribe subscribe_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::SUBSCRIBE), subscribe_out));
  CHECK_EQ(TRACK_NAMESPACE_CONF, subscribe_out.track_namespace);
  CHECK_EQ(TRACK_NAME_ALICE_VIDEO, subscribe_out.track_name);
  CHECK_EQ(subscribe.subscribe_id, subscribe_out.subscribe_id);
  CHECK_EQ(subscribe.track_alias, subscribe_out.track_alias);
  CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
  CHECK_EQ(subscribe.track_params.size(), subscribe_out.track_params.size());
  CHECK_EQ(subscribe.track_params[0].type, subscribe_out.track_params[0].type);
  CHECK_EQ(subscribe.track_params[0].length, subscribe_out.track_params[0].length);
  CHECK_EQ(subscribe.track_params[0].value, subscribe_out.track_params[0].value);

  CHECK_EQ(subscribe.track_params[1].type, subscribe_out.track_params[1].type);
  CHECK_EQ(subscribe.track_params[1].length, subscribe_out.track_params[1].length);
  CHECK_EQ(subscribe.track_params[1].value, subscribe_out.track_params[1].value);
}


MoqtSubscribe generate_subscribe(FilterType filter, size_t  num_params = 0, uint64_t sg = 0, uint64_t so=0,
                   uint64_t eg=0, uint64_t eo=0) {
  MoqtSubscribe out;
  out.subscribe_id = 0xABCD;
  out.track_alias = ToUint64(TRACK_ALIAS_ALICE_VIDEO);
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
    MoqtParameter param1;
    param1.type = static_cast<uint64_t>(ParameterType::AuthorizationInfo);
    param1.length = 0x2;
    param1.value = {0x1, 0x2};
    out.track_params.push_back(param1);
    num_params--;
  }
  return out;
}

TEST_CASE("Subscribe (Combo) Message encode/decode")
{
  auto subscribes = std::vector<MoqtSubscribe> {
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
    std::vector<uint8_t> net_data = buffer.Front(buffer.Size());
    MoqtSubscribe subscribe_out;
    CHECK(verify(
      net_data, static_cast<uint64_t>(MoqtMessageType::SUBSCRIBE), subscribe_out));
    CHECK_EQ(TRACK_NAMESPACE_CONF, subscribe_out.track_namespace);
    CHECK_EQ(TRACK_NAME_ALICE_VIDEO, subscribe_out.track_name);
    CHECK_EQ(subscribes[i].subscribe_id, subscribe_out.subscribe_id);
    CHECK_EQ(subscribes[i].track_alias, subscribe_out.track_alias);
    CHECK_EQ(subscribes[i].filter_type, subscribe_out.filter_type);
    CHECK_EQ(subscribes[i].track_params.size(), subscribe_out.track_params.size());
    for(size_t j = 0; j < subscribes[i].track_params.size(); j++) {
      CHECK_EQ(subscribes[i].track_params[j].type,
               subscribe_out.track_params[j].type);
      CHECK_EQ(subscribes[i].track_params[j].length,
               subscribe_out.track_params[j].length);
      CHECK_EQ(subscribes[i].track_params[j].value,
               subscribe_out.track_params[j].value);

    }
  }
}


TEST_CASE("SubscribeOk Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto subscribe_ok  = MoqtSubscribeOk {};
  subscribe_ok.subscribe_id = 0x1;
  subscribe_ok.expires = 0x100;
  subscribe_ok.content_exists = false;
  buffer << subscribe_ok;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());

  MoqtSubscribeOk subscribe_ok_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::SUBSCRIBE_OK), subscribe_ok_out));
  CHECK_EQ(subscribe_ok.subscribe_id, subscribe_ok_out.subscribe_id);
  CHECK_EQ(subscribe_ok.expires, subscribe_ok_out.expires);
  CHECK_EQ(subscribe_ok.content_exists, subscribe_ok_out.content_exists);
}


TEST_CASE("SubscribeOk (content-exists) Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto subscribe_ok  = MoqtSubscribeOk {};
  subscribe_ok.subscribe_id = 0x1;
  subscribe_ok.expires = 0x100;
  subscribe_ok.content_exists = true;
  subscribe_ok.largest_group = 0x1000;
  subscribe_ok.largest_object = 0xff;
  buffer << subscribe_ok;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());

  MoqtSubscribeOk subscribe_ok_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::SUBSCRIBE_OK), subscribe_ok_out));
  CHECK_EQ(subscribe_ok.subscribe_id, subscribe_ok_out.subscribe_id);
  CHECK_EQ(subscribe_ok.expires, subscribe_ok_out.expires);
  CHECK_EQ(subscribe_ok.content_exists, subscribe_ok_out.content_exists);
  CHECK_EQ(subscribe_ok.largest_group, subscribe_ok_out.largest_group);
  CHECK_EQ(subscribe_ok.largest_object, subscribe_ok_out.largest_object);
}

TEST_CASE("Error  Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto subscribe_err  = MoqtSubscribeError {};
  subscribe_err.subscribe_id = 0x1;
  subscribe_err.err_code = 0;
  subscribe_err.reason_phrase = Bytes {0x0, 0x1};
  subscribe_err.track_alias = ToUint64(TRACK_ALIAS_ALICE_VIDEO);
  buffer << subscribe_err;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());

  MoqtSubscribeError subscribe_err_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::SUBSCRIBE_ERROR), subscribe_err_out));
  CHECK_EQ(subscribe_err.subscribe_id, subscribe_err_out.subscribe_id);
  CHECK_EQ(subscribe_err.err_code, subscribe_err_out.err_code);
  CHECK_EQ(subscribe_err.reason_phrase, subscribe_err_out.reason_phrase);
  CHECK_EQ(subscribe_err.track_alias, subscribe_err_out.track_alias);
}

TEST_CASE("Unsubscribe  Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto unsubscribe  = MoqtUnsubscribe {};
  unsubscribe.subscribe_id = 0x1;
  buffer << unsubscribe;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());

  MoqtUnsubscribe unsubscribe_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::UNSUBSCRIBE), unsubscribe_out));
  CHECK_EQ(unsubscribe.subscribe_id, unsubscribe_out.subscribe_id);
}

TEST_CASE("SubscribeDone  Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto subscribe_done  = MoqtSubscribeDone {};
  subscribe_done.subscribe_id = 0x1;
  subscribe_done.status_code = 0x0;
  subscribe_done.reason_phrase = Bytes {0x0};
  subscribe_done.content_exists = false;

  buffer << subscribe_done;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());

  MoqtSubscribeDone subscribe_done_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::SUBSCRIBE_DONE), subscribe_done_out));
  CHECK_EQ(subscribe_done.subscribe_id, subscribe_done_out.subscribe_id);
  CHECK_EQ(subscribe_done.status_code, subscribe_done_out.status_code);
  CHECK_EQ(subscribe_done.reason_phrase, subscribe_done_out.reason_phrase);
  CHECK_EQ(subscribe_done.content_exists, subscribe_done_out.content_exists);
}

TEST_CASE("SubscribeDone (content-exists)  Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto subscribe_done  = MoqtSubscribeDone {};
  subscribe_done.subscribe_id = 0x1;
  subscribe_done.status_code = 0x0;
  subscribe_done.reason_phrase = Bytes {0x0};
  subscribe_done.content_exists = true;
  subscribe_done.final_group_id = 0x1111;
  subscribe_done.final_object_id = 0xff;

  buffer << subscribe_done;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());

  MoqtSubscribeDone subscribe_done_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::SUBSCRIBE_DONE), subscribe_done_out));
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
  const std::string endpoint_id = "client test";
  auto client_setup = MoqtClientSetup {};
  client_setup.num_versions = 2;
  client_setup.supported_versions = {0x1000, 0x2000};
  client_setup.role_parameter.type = static_cast<uint64_t>(ParameterType::Role);
  client_setup.role_parameter.length = 0x1;
  client_setup.role_parameter.value = {0xFF};
  client_setup.endpoint_id_parameter.value.assign(endpoint_id.begin(), endpoint_id.end());

  buffer << client_setup;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());

  MoqtClientSetup client_setup_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::CLIENT_SETUP), client_setup_out));
  CHECK_EQ(client_setup.supported_versions, client_setup_out.supported_versions);
  CHECK_EQ(client_setup.role_parameter.value, client_setup_out.role_parameter.value);
  CHECK_EQ(client_setup.endpoint_id_parameter.value, client_setup_out.endpoint_id_parameter.value);
}


TEST_CASE("ServerSetup  Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;
  const std::string endpoint_id = "server_test";
  auto server_setup = MoqtServerSetup {};
  server_setup.selection_version = {0x1000};
  server_setup.role_parameter.type = static_cast<uint64_t>(ParameterType::Role);
  server_setup.role_parameter.length = 0x1;
  server_setup.role_parameter.value = {0xFF};
  server_setup.endpoint_id_parameter.value.assign(endpoint_id.begin(), endpoint_id.end());

  buffer << server_setup;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());

  MoqtServerSetup server_setup_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::SERVER_SETUP), server_setup_out));
  CHECK_EQ(server_setup.selection_version, server_setup_out.selection_version);
  CHECK_EQ(server_setup.role_parameter.value, server_setup.role_parameter.value);
  CHECK_EQ(server_setup.endpoint_id_parameter.value, server_setup_out.endpoint_id_parameter.value);
}

TEST_CASE("ObjectStream  Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;
  auto object_stream = MoqtObjectStream {};
  object_stream.subscribe_id = 0x100;
  object_stream.track_alias = ToUint64(TRACK_ALIAS_ALICE_VIDEO);
  object_stream.group_id = 0x1000;
  object_stream.object_id = 0xFF;
  object_stream.priority =0xA;
  object_stream.payload = {0x1, 0x2, 0x3, 0x5, 0x6};

  buffer << object_stream;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());

  MoqtObjectStream object_stream_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::OBJECT_STREAM), object_stream_out));
  CHECK_EQ(object_stream.subscribe_id, object_stream_out.subscribe_id);
  CHECK_EQ(object_stream.track_alias, object_stream_out.track_alias);
  CHECK_EQ(object_stream.group_id, object_stream_out.group_id);
  CHECK_EQ(object_stream.object_id, object_stream_out.object_id);
  CHECK_EQ(object_stream.priority, object_stream_out.priority);
  CHECK_EQ(object_stream.payload, object_stream_out.payload);
}

TEST_CASE("ObjectDatagram  Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;
  auto object_datagram = MoqtObjectDatagram {};
  object_datagram.subscribe_id = 0x100;
  object_datagram.track_alias = ToUint64(TRACK_ALIAS_ALICE_VIDEO);
  object_datagram.group_id = 0x1000;
  object_datagram.object_id = 0xFF;
  object_datagram.priority =0xA;
  object_datagram.payload = {0x1, 0x2, 0x3, 0x5, 0x6};

  buffer << object_datagram;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());

  MoqtObjectStream object_datagram_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::OBJECT_DATAGRAM), object_datagram_out));
  CHECK_EQ(object_datagram.subscribe_id, object_datagram_out.subscribe_id);
  CHECK_EQ(object_datagram.track_alias, object_datagram_out.track_alias);
  CHECK_EQ(object_datagram.group_id, object_datagram_out.group_id);
  CHECK_EQ(object_datagram.object_id, object_datagram_out.object_id);
  CHECK_EQ(object_datagram.priority, object_datagram_out.priority);
  CHECK_EQ(object_datagram.payload, object_datagram_out.payload);
}

TEST_CASE("StreamPerGroup Object  Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;
  auto hdr_grp = MoqtStreamHeaderGroup {};
  hdr_grp.subscribe_id = 0x100;
  hdr_grp.track_alias = ToUint64(TRACK_ALIAS_ALICE_VIDEO);
  hdr_grp.group_id = 0x1000;
  hdr_grp.priority = 0xA;

  buffer << hdr_grp;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());
  MoqtStreamHeaderGroup hdr_group_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::STREAM_HEADER_GROUP), hdr_group_out));
  CHECK_EQ(hdr_grp.subscribe_id, hdr_group_out.subscribe_id);
  CHECK_EQ(hdr_grp.track_alias, hdr_group_out.track_alias);
  CHECK_EQ(hdr_grp.group_id, hdr_group_out.group_id);

  // stream all the objects
  buffer.Pop(buffer.Size());
  auto objects = std::vector<MoqtStreamGroupObject>{};
  // send 10 objects
  for(size_t i = 0; i < 1000; i++) {
    auto obj = MoqtStreamGroupObject{};
    obj.object_id = i;
    obj.payload = {0x1, 0x2, 0x3, 0x4, 0x5};
    objects.push_back(obj);
    buffer << obj;
  }

  net_data.clear();
  net_data = buffer.Front(buffer.Size());
  auto obj_out = MoqtStreamGroupObject{};
  size_t object_count = 0;
  qtransport::StreamBuffer<uint8_t> in_buffer;
  for(size_t i =0; i < net_data.size(); i++) {
    in_buffer.Push(net_data.at(i));
    bool done;
    done = in_buffer >> obj_out;
    if (done) {
      CHECK_EQ(obj_out.object_id, objects[object_count].object_id);
      CHECK_EQ(obj_out.payload, objects[object_count].payload);
      // got one object
      object_count++;
      obj_out = MoqtStreamGroupObject{};
      in_buffer.Pop(in_buffer.Size());
    }
  }

  CHECK_EQ(object_count, 1000);
}

TEST_CASE("StreamPerTrack Object  Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;
  auto hdr = MoqtStreamHeaderTrack {};
  hdr.subscribe_id = 0x100;
  hdr.track_alias = ToUint64(TRACK_ALIAS_ALICE_VIDEO);
  hdr.priority = 0xA;

  buffer << hdr;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());
  MoqtStreamHeaderTrack hdr_out;
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::STREAM_HEADER_TRACK), hdr_out));
  CHECK_EQ(hdr_out.subscribe_id, hdr_out.subscribe_id);
  CHECK_EQ(hdr_out.track_alias, hdr_out.track_alias);
  CHECK_EQ(hdr_out.priority, hdr_out.priority);

  // stream all the objects
  buffer.Pop(buffer.Size());
  auto objects = std::vector<MoqtStreamTrackObject>{};
  // send 10 objects
  for(size_t i = 0; i < 1000; i++) {
    auto obj = MoqtStreamTrackObject{};
   if ( i % 10 == 0) {
      obj.group_id = i;
      obj.object_id = 0;
    } else {
      obj.object_id = i;
    }

    obj.payload = {0x1, 0x2, 0x3, 0x4, 0x5};
    objects.push_back(obj);
    buffer << obj;
  }

  net_data.clear();
  net_data = buffer.Front(buffer.Size());
  auto obj_out = MoqtStreamTrackObject{};
  size_t object_count = 0;
  qtransport::StreamBuffer<uint8_t> in_buffer;
  for(size_t i =0; i < net_data.size(); i++) {
    in_buffer.Push(net_data.at(i));
    bool done;
    done = in_buffer >> obj_out;
    if (done) {
      CHECK_EQ(obj_out.group_id, objects[object_count].group_id);
      CHECK_EQ(obj_out.object_id, objects[object_count].object_id);
      CHECK_EQ(obj_out.payload, objects[object_count].payload);
      // got one object
      object_count++;
      obj_out = MoqtStreamTrackObject{};
      in_buffer.Pop(in_buffer.Size());
    }
  }

  CHECK_EQ(object_count, 1000);
}


TEST_CASE("MoqtGoaway Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto goaway  = MoqtGoaway {};
  goaway.new_session_uri = from_ascii("go.away.now.no.return");
  buffer << goaway;

  std::vector<uint8_t> net_data = buffer.Front(buffer.Size());
  MoqtGoaway goaway_out{};
  CHECK(verify(net_data, static_cast<uint64_t>(MoqtMessageType::GOAWAY), goaway_out));
  CHECK_EQ(from_ascii("go.away.now.no.return"), goaway_out.new_session_uri);
}