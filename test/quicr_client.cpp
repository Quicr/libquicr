#include "fake_transport.h"

#include <cantina/logger.h>
#include <doctest/doctest.h>
#include <quicr/encode.h>
#include <quicr/quicr_client.h>
#include <UrlEncoder.h>

#include <memory>
#include <string>
#include <vector>


using namespace quicr;

auto logger = std::make_shared<cantina::Logger>("CLIENT_TEST");

struct URIConvertor : public NumeroUriConvertor {
  virtual ~URIConvertor() = default;

  URIConvertor() {
    std::string uri_template = "quicr://webex.cisco.com<pen=1><sub_pen=1>/conferences/<int24>/mediatype/<int8>/endpoint/<int16>";
    url_encoder.AddTemplate(uri_template, true);
  }

  std::string to_namespace_uri(quicr::Namespace ns) override {
    auto uri = url_encoder.DecodeUrl(ns);
    return uri;
  }

  std::string to_name_uri(quicr::Namespace /*n*/) override {
   return "";
  }

  UrlEncoder url_encoder;

};

struct TestSubscriberDelegate : public SubscriberDelegate
{
  TestSubscriberDelegate() = default;
  ~TestSubscriberDelegate() override = default;

  void onSubscribeResponse(const quicr::Namespace& /* quicr_namespace */,
                           const SubscribeResult& /* result */) override
  {
  }

  void onSubscriptionEnded(
    const quicr::Namespace& /* quicr_namespace */,
    const SubscribeResult::SubscribeStatus& /* result */) override
  {
  }

  void onSubscribedObject(const quicr::Name& /* quicr_name */,
                          uint8_t /* priority */,
                          uint16_t /* expiry_age_ms */,
                          bool /* use_reliable_transport */,
                          bytes&& /* data */) override
  {
  }

  void onSubscribedObjectFragment(const quicr::Name& /* quicr_name */,
                                  uint8_t /* priority */,
                                  uint16_t /* expiry_age_ms */,
                                  bool /* use_reliable_transport */,
                                  const uint64_t& /* offset */,
                                  bool /* is_last_fragment */,
                                  bytes&& /* data */) override
  {
  }
};

struct TestPublisherDelegate : public PublisherDelegate
{
  void onPublishIntentResponse(const quicr::Namespace& /* quicr_namespace */,
                               const PublishIntentResult& /* result */) override
  {
  }

  ~TestPublisherDelegate() override = default;
};


TEST_CASE("Subscribe encode, send and receive")
{


  auto uri_convertor = std::make_shared<URIConvertor>();
  std::string ns_uri = "quicr://webex.cisco.com/conferences/1/mediatype/1/endpoint/6";
  auto ns = uri_convertor->url_encoder.EncodeUrl(ns_uri);
  auto ns_uri_out = uri_convertor->to_namespace_uri(ns);
  CHECK_EQ(ns_uri, ns_uri_out);
  std::shared_ptr<NumeroUriConvertor> convertor = uri_convertor;
  auto transport = std::make_shared<FakeTransport>();
  auto qclient = std::make_unique<quicr::Client>(transport, logger);
  qclient->set_numero_uri_convertor(convertor);
  qclient->connect();
  const auto expected_ns = quicr::Namespace{ 0x10000000000000002000_name, 125 };

  qclient->subscribe(
    {}, expected_ns, SubscribeIntent::wait_up, "", false, "", {});

  auto s = messages::Subscribe{};
  messages::MessageBuffer msg{ transport->stored_data };
  msg >> s;

  CHECK_EQ(s.transaction_id, s.transaction_id);
  CHECK_EQ(s.quicr_namespace, expected_ns);
  CHECK_EQ(s.intent, SubscribeIntent::wait_up);
}

TEST_CASE("Publish encode, send and receive")
{
  auto transport = std::make_shared<FakeTransport>();
  auto qclient = std::make_unique<quicr::Client>(transport, logger);
  qclient->connect();

  const auto expected_name = 0x10000000000000002000_name;
  const auto expected_ns = quicr::Namespace{ expected_name, 80 };
  const auto say_hello = std::vector<uint8_t>{ 'H', 'E', 'L', 'L', '0' };

  {
    qclient->publishIntent({}, expected_ns, "", "", {});

    auto say_hello_copy = say_hello;
    qclient->publishNamedObject(
      expected_name, 0, 0, false, std::move(say_hello_copy));
  }

  auto d = messages::PublishDatagram{};
  auto msg = messages::MessageBuffer{ transport->stored_data };
  msg >> d;

  CHECK_EQ(d.media_data, say_hello);
}
