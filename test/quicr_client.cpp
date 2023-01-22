#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <quicr/quicr_client.h>

using namespace quicr;

struct TestSubscriberDelegate : public SubscriberDelegate
{
  TestSubscriberDelegate() = default;
  ~TestSubscriberDelegate() = default;

  void onSubscribeResponse(const QUICRNamespace& quicr_namespace,
                           const SubscribeResult& result)
  {
  }

  void onSubscriptionEnded(const QUICRNamespace& quicr_namespace,
                           const SubscribeResult& result)
  {
  }

  void onSubscribedObject(const QUICRName& quicr_name,
                          uint8_t priority,
                          uint16_t expiry_age_ms,
                          bool use_reliable_transport,
                          bytes&& data)
  {
  }

  void onSubscribedObjectFragment(const QUICRName& quicr_name,
                                  uint8_t priority,
                                  uint16_t expiry_age_ms,
                                  bool use_reliable_transport,
                                  const uint64_t& offset,
                                  bool is_last_fragment,
                                  bytes&& data)
  {
  }
};

struct TestPublisherDelegate : public PublisherDelegate
{
  void onPublishIntentResponse(const QUICRNamespace& quicr_namespace,
                               const PublishIntentResult& result) override
  {
  }

  ~TestPublisherDelegate() override = default;
};

TEST_CASE("Object Lifetime")
{
  std::shared_ptr<TestSubscriberDelegate> sub_delegate {};
  std::shared_ptr<TestPublisherDelegate> pub_delegate{};
  ITransport fake_transport;
  CHECK_NOTHROW(
    std::make_unique<QuicRClient>(fake_transport, sub_delegate, pub_delegate));
}
