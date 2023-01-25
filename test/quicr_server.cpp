#include <memory>
#include <vector>

#include <doctest/doctest.h>
#include <quicr/quicr_server.h>

#include "../src/encode.h"
#include "fake_transport.h"

using namespace quicr;

class TestServerDelegate : public ServerDelegate
{

  virtual void onPublishIntent(const QUICRNamespace& quicr_name,
                               const std::string& origin_url,
                               bool use_reliable_transport,
                               const std::string& auth_token,
                               bytes&& e2e_token) override
  {
  }

  virtual void onPublisherObject(const QUICRName& quicr_name,
                                 uint8_t priority,
                                 uint16_t expiry_age_ms,
                                 bool use_reliable_transport,
                                 bytes&& data) override
  {
  }

  virtual void onPublishedFragment(const QUICRName& quicr_name,
                                   uint8_t priority,
                                   uint16_t expiry_age_ms,
                                   bool use_reliable_transport,
                                   const uint64_t& offset,
                                   bool is_last_fragment,
                                   bytes&& data)
  {
  }

  virtual void onSubscribe(const QUICRNamespace& quicr_namespace,
                           const SubscribeIntent subscribe_intent,
                           const std::string& origin_url,
                           bool use_reliable_transport,
                           const std::string& auth_token,
                           bytes&& data)
  {
  }
};

TEST_CASE("Object Lifetime")
{
  TestServerDelegate delegate{};
  FakeTransport fake_transport;
  RelayInfo relayInfo = { hostname: "127.0.0.1", port: 1234, proto: RelayInfo::Protocol::UDP };
  CHECK_NOTHROW(std::make_unique<QuicRServer>(relayInfo, delegate));
}

TEST_CASE("SubscribeResponse encode, send and receive")
{
  TestServerDelegate delegate{};
  FakeTransportDelegate transport_delegate;

  auto transport = std::make_shared<FakeTransport>();

  auto qserver = std::make_unique<QuicRServer>(*transport, delegate);

  qserver->subscribeResponse(
    { 0x1000, 0x2000, 3 },
    0x5555,
    SubscribeResult{ SubscribeResult::SubscribeStatus::Ok });
  auto fake_transport = std::reinterpret_pointer_cast<FakeTransport>(transport);
  messages::MessageBuffer msg{ fake_transport->stored_data };
  messages::SubscribeResponse resp;
  msg >> resp;

  CHECK_EQ(resp.transaction_id, 0x5555);
  CHECK_EQ(resp.quicr_namespace.low, 0x2000);
  CHECK_EQ(resp.quicr_namespace.hi, 0x1000);
  CHECK_EQ(resp.quicr_namespace.mask, 3);
  CHECK_EQ(resp.response, SubscribeResult::SubscribeStatus::Ok);
}

TEST_CASE("Send Object Message Encode, send and receive")
{

  TestServerDelegate delegate{};
  FakeTransportDelegate transport_delegate;

  auto transport = std::make_shared<FakeTransport>();

  auto qserver = std::make_unique<QuicRServer>(*transport, delegate);

  std::vector<uint8_t> say_hello = { 'H', 'E', 'L', 'L', '0' };
  qserver->sendNamedObject(
    { 0x1000, 0x2000 }, 0, 0, false, std::move(say_hello));

  auto fake_transport = std::reinterpret_pointer_cast<FakeTransport>(transport);
  messages::PublishDatagram d;
  messages::MessageBuffer msg{ fake_transport->stored_data };
  msg >> d;
  say_hello = { 'H', 'E', 'L', 'L', '0' };
  CHECK_EQ(d.media_data, say_hello);
}
