#include <memory>
#include <string>
#include <vector>

#include "fake_transport.h"
#include <doctest/doctest.h>
#include <quicr/encode.h>
#include <quicr/quicr_client.h>

using namespace quicr;

struct TestSubscriberDelegate : public SubscriberDelegate
{
  TestSubscriberDelegate() = default;
  ~TestSubscriberDelegate() = default;

  void onSubscribeResponse(const quicr::Namespace& /* quicr_namespace */,
                           const SubscribeResult& /* result */)
  {
  }

  void onSubscriptionEnded(const quicr::Namespace& /* quicr_namespace */,
                           const SubscribeResult::SubscribeStatus& /* result */)
  {
  }

  void onSubscribedObject(const quicr::Name& /* quicr_name */,
                          uint8_t /* priority */,
                          uint16_t /* expiry_age_ms */,
                          bool /* use_reliable_transport */,
                          bytes&& /* data */)
  {
  }

  void onSubscribedObjectFragment(const quicr::Name& /* quicr_name */,
                                  uint8_t /* priority */,
                                  uint16_t /* expiry_age_ms */,
                                  bool /* use_reliable_transport */,
                                  const uint64_t& /* offset */,
                                  bool /* is_last_fragment */,
                                  bytes&& /* data */)
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

struct TestGetDelegate : public GetDelegate
{
  TestGetDelegate() = default;
  ~TestGetDelegate() = default;

  void onGetResponse(const quicr::Namespace& /* quicr_namespace */,
                     const SubscribeResult& /* result */,
                     std::vector<GetObject>&& /* data*/)
  {
  }
};

TEST_CASE("Subscribe encode, send and receive")
{
  std::shared_ptr<TestSubscriberDelegate> sub_delegate{};
  std::shared_ptr<TestPublisherDelegate> pub_delegate{};
  FakeTransportDelegate transport_delegate;

  auto transport = std::make_shared<FakeTransport>();
  auto qclient = std::make_unique<QuicRClient>(transport);

  qclient->subscribe(sub_delegate,
                     { 0x10000000000000002000_name, 125 },
                     SubscribeIntent::wait_up,
                     "",
                     false,
                     "",
                     {});

  messages::Subscribe s;
  messages::MessageBuffer msg{ transport->stored_data };
  msg >> s;

  CHECK_EQ(s.transaction_id, s.transaction_id);
  CHECK_EQ(s.quicr_namespace,
           quicr::Namespace{ 0x10000000000000002000_name, 125 });
  CHECK_EQ(s.intent, SubscribeIntent::wait_up);
}

TEST_CASE("Publish encode, send and receive")
{
  std::shared_ptr<TestSubscriberDelegate> sub_delegate{};
  std::shared_ptr<TestPublisherDelegate> pub_delegate{};
  FakeTransportDelegate transport_delegate;

  auto transport = std::make_shared<FakeTransport>();

  auto qclient = std::make_unique<QuicRClient>(transport);
  std::vector<uint8_t> say_hello = { 'H', 'E', 'L', 'L', '0' };
  qclient->publishNamedObject(
    0x10000000000000002000_name, 0, 0, false, std::move(say_hello));

  auto fake_transport = std::reinterpret_pointer_cast<FakeTransport>(transport);
  messages::PublishDatagram d;
  messages::MessageBuffer msg{ fake_transport->stored_data };
  msg >> d;
  say_hello = { 'H', 'E', 'L', 'L', '0' };
  CHECK_EQ(d.media_data, say_hello);
}


TEST_CASE("Get encode, send and receive")
{
  std::shared_ptr<TestGetDelegate> get_delegate{};
  FakeTransportDelegate transport_delegate;

  auto transport = std::make_shared<FakeTransport>();
  auto qclient = std::make_unique<QuicRClient>(transport);

  qclient->get(get_delegate,
               { 0x10000000000000002000_name, 125 });

  messages::Get get;
  messages::MessageBuffer msg{ transport->stored_data };
  msg >> get;

  CHECK_EQ(get.resource,
           quicr::Namespace{ 0x10000000000000002000_name, 125 });
}
