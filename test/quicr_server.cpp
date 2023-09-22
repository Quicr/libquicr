#include "fake_transport.h"

#include <doctest/doctest.h>
#include <quicr/encode.h>
#include <quicr/quicr_client.h>
#include <quicr/quicr_server.h>

#include <memory>
#include <vector>

using namespace quicr;

static auto logger =
  std::make_shared<cantina::Logger>("Server Test", "SRV_TEST");

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

class TestServerDelegate : public ServerDelegate
{

  virtual void onPublishIntent(const quicr::Namespace& /* quicr_name */,
                               const std::string& /* origin_url */,
                               bool /* use_reliable_transport */,
                               const std::string& /* auth_token */,
                               bytes&& /* e2e_token */) override
  {
  }

  virtual void onPublishIntentEnd(const quicr::Namespace&,
                                  const std::string&,
                                  bytes&&) override
  {
  }

  virtual void onPublisherObject(
    [[maybe_unused]] const qtransport::TransportContextId& context_id,
    [[maybe_unused]] const qtransport::StreamId& stream_id,
    [[maybe_unused]] bool use_reliable_transport,
    [[maybe_unused]] messages::PublishDatagram&& datagram) override
  {
  }

  virtual void onSubscribe(
    const quicr::Namespace& /* quicr_namespace */,
    const uint64_t& /* subscriber_id */,
    const qtransport::TransportContextId& /* context_id */,
    const qtransport::TransportContextId& /*stream_id */,
    const SubscribeIntent /* subscribe_intent */,
    const std::string& /* origin_url */,
    bool /* use_reliable_transport */,
    const std::string& /* auth_token */,
    bytes&& /* data */) override
  {
  }

  virtual void onUnsubscribe(const quicr::Namespace& /* quicr_namespace */,
                             const uint64_t& /* subscriber_id */,
                             const std::string& /* auth_token */) override
  {
  }
};

TEST_CASE("Object Lifetime")
{
  auto delegate = std::make_shared<TestServerDelegate>();
  FakeTransport fake_transport;
  RelayInfo relayInfo = { .hostname = "127.0.0.1",
                          .port = 1234,
                          .proto = RelayInfo::Protocol::UDP };
  cantina::LoggerPointer logger = std::make_shared<cantina::Logger>("TEST");
  qtransport::TransportConfig tcfg{ .tls_cert_filename = NULL,
                                    .tls_key_filename = NULL };
  CHECK_NOTHROW(std::make_unique<Server>(relayInfo, tcfg, delegate, logger));
}
