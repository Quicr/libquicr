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

  void onSubscriptionEnded(
    const quicr::Namespace& /* quicr_namespace */,
    const SubscribeResult::SubscribeStatus& /* result */) override
  {
  }

  void onSubscribedObject([[maybe_unused]] const quicr::Name& quicr_name,
                          [[maybe_unused]] uint8_t priority,
                          [[maybe_unused]] bytes&& data) override
  {
  }

  void onSubscribedObjectFragment([[maybe_unused]] const quicr::Name& quicr_name,
                                  [[maybe_unused]] uint8_t priority,
                                  [[maybe_unused]] const uint64_t& offset,
                                  [[maybe_unused]] bool is_last_fragment,
                                  [[maybe_unused]] bytes&& data) override
  {
  }
};

class TestServerDelegate : public ServerDelegate
{

  void onPublishIntent([[maybe_unused]] const quicr::Namespace& quicr_name,
                       [[maybe_unused]] const std::string& origin_url,
                       [[maybe_unused]] const std::string& auth_token,
                       [[maybe_unused]] bytes&& e2e_token) override
  {
  }

  void onPublishIntentEnd(const quicr::Namespace& /* quicr_namespace */,
                          const std::string& /* auth_token */,
                          bytes&& /* e2e_token */) override
  {
  }

  void onPublisherObject([[maybe_unused]] const qtransport::TransportConnId& conn_id,
                         [[maybe_unused]] const qtransport::DataContextId& data_ctx_id,
                         [[maybe_unused]] messages::PublishDatagram&& datagram) override
  {
  }

  void onSubscribe([[maybe_unused]] const quicr::Namespace& quicr_namespace,
                   [[maybe_unused]] const uint64_t& subscriber_id,
                   [[maybe_unused]] const qtransport::TransportConnId& conn_id,
                   [[maybe_unused]] const qtransport::DataContextId& data_ctx_id,
                   [[maybe_unused]] const SubscribeIntent subscribe_intent,
                   [[maybe_unused]] const std::string& origin_url,
                   [[maybe_unused]] const std::string& auth_token,
                   [[maybe_unused]] bytes&& data) override
  {
  }

  void onUnsubscribe(const quicr::Namespace& /* quicr_namespace */,
                     const uint64_t& /* subscriber_id */,
                     const std::string& /* auth_token */) override
  {
  }

  void onSubscribePause([[maybe_unused]] const quicr::Namespace& quicr_namespace,
                        [[maybe_unused]] const uint64_t subscriber_id,
                        [[maybe_unused]] const qtransport::TransportConnId conn_id,
                        [[maybe_unused]] const qtransport::DataContextId data_ctx_id,
                        [[maybe_unused]] const bool pause) override {}
};

TEST_CASE("Object Lifetime")
{
  auto delegate = std::make_shared<TestServerDelegate>();
  const auto relayInfo = RelayInfo{ .hostname = "127.0.0.1",
                                    .port = 1234,
                                    .proto = RelayInfo::Protocol::UDP,
                                    .relay_id = "relay-test"};
  const auto logger = std::make_shared<cantina::Logger>("TEST");
  const auto tcfg = qtransport::TransportConfig{ .tls_cert_filename = nullptr,
                                                 .tls_key_filename = nullptr };

  // NOLINTNEXTLINE(cert-err33-c)
  CHECK_NOTHROW(std::make_unique<Server>(relayInfo, tcfg, delegate, logger));
}
