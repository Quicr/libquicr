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
  ~TestSubscriberDelegate() override = default;

  void onSubscribeResponse(const quicr::Namespace& /* quicr_namespace */,
                           const SubscribeResult& /* result */) override
  {
  }

  void onSubscriptionEnded(const quicr::Namespace& /* quicr_namespace */,
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

class TestServerDelegate : public ServerDelegate
{

  void onPublishIntent(const quicr::Namespace& /* quicr_name */,
                               const std::string& /* origin_url */,
                               bool /* use_reliable_transport */,
                               const std::string& /* auth_token */,
                               bytes&& /* e2e_token */) override
  {
  }

  void onPublishIntentEnd(const quicr::Namespace& /* quicr_namespace */,
                                  const std::string& /* auth_token */,
                                  bytes&& /* e2e_token */) override
  {
  }

  void onPublisherObject(
    [[maybe_unused]] const qtransport::TransportContextId& context_id,
    [[maybe_unused]] const qtransport::StreamId& stream_id,
    [[maybe_unused]] bool use_reliable_transport,
    [[maybe_unused]] messages::PublishDatagram&& datagram) override
  {
  }

  void onSubscribe(
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

  void onUnsubscribe(const quicr::Namespace& /* quicr_namespace */,
                             const uint64_t& /* subscriber_id */,
                             const std::string& /* auth_token */) override
  {
  }
};

TEST_CASE("Object Lifetime")
{
  auto delegate = std::make_shared<TestServerDelegate>();
  const auto relayInfo = RelayInfo{ .hostname = "127.0.0.1",
                          .port = 1234,
                          .proto = RelayInfo::Protocol::UDP };
  const auto logger = std::make_shared<cantina::Logger>("TEST");
  const auto tcfg = qtransport::TransportConfig{ .tls_cert_filename = nullptr,
                                    .tls_key_filename = nullptr };

  // NOLINTNEXTLINE(cert-err33-c)
  CHECK_NOTHROW(
    std::make_unique<Server>(relayInfo, tcfg, delegate, logger));
}
