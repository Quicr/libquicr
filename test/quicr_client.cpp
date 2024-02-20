#include "fake_transport.h"

#include <cantina/logger.h>
#include <doctest/doctest.h>
#include <quicr/encode.h>
#include <quicr/quicr_client.h>

#include <memory>
#include <string>
#include <vector>

using namespace quicr;

auto logger = std::make_shared<cantina::Logger>("CLIENT_TEST");

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
                          bytes&& /* data */) override
  {
  }

  void onSubscribedObjectFragment(const quicr::Name& /* quicr_name */,
                                  uint8_t /* priority */,
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
  auto transport = std::make_shared<FakeTransport>();
  auto qclient = std::make_unique<quicr::Client>(transport, logger);
  qclient->connect();

  const auto expected_ns = quicr::Namespace{ 0x10000000000000002000_name, 125 };

  qclient->subscribe(
    {}, expected_ns, SubscribeIntent::wait_up, {}, "", "", {});

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
    qclient->publishIntent({}, expected_ns, "", "", {}, {});

    auto say_hello_copy = say_hello;
    qclient->publishNamedObject(
      expected_name, 0, 0, std::move(say_hello_copy), {});
  }

  auto d = messages::PublishDatagram{};
  auto msg = messages::MessageBuffer{ transport->stored_data };
  msg >> d;

  CHECK_EQ(d.media_data, say_hello);
}
