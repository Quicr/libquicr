#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <quicr/quicr_server.h>

using namespace quicr;

class TestServerDelegate : public ServerDelegate
{

  void onPublishIntent(const QUICRNamespace& quicr_name,
                       const std::string& origin_url,
                       bool use_reliable_transport,
                       const std::string& auth_token,
                       bytes&& e2e_token) override
  {
  }

  void onPublisherObject(const QUICRName& quicr_name,
                         uint8_t priority,
                         uint16_t expiry_age_ms,
                         bool use_reliable_transport,
                         bytes&& data) override
  {
  }

  void onPublishedFragment(const QUICRName& quicr_name,
                           uint8_t priority,
                           uint16_t expiry_age_ms,
                           bool use_reliable_transport,
                           const uint64_t& offset,
                           bool is_last_fragment,
                           bytes&& data) override
  {
  }

  void onSubscribe(const QUICRNamespace& quicr_namespace,
                   const SubscribeIntent subscribe_intent,
                   const std::string& origin_url,
                   bool use_reliable_transport,
                   const std::string& auth_token,
                   bytes&& data) override
  {
  }
};

TEST_CASE("Object Lifetime")
{
  TestServerDelegate delegate{};
  ITransport fake_transport;
  CHECK_NOTHROW(std::make_unique<QuicRServer>(fake_transport, delegate));
}
