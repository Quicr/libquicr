#include <quicr/quicr_client.h>
#include <quicr/quicr_common.h>

#include "encode.h"
#include "message_buffer.h"
#include "state.h"

namespace quicr {
///
/// Common
///

bool
is_quicr_name_in_namespace(const QUICRNamespace& ns, const QUICRName& n)
{
  auto a = ns.hi ^ n.hi;
  auto b = ns.low ^ n.low;
  unsigned count = 0;
  while (a > 0) {
    count += a & 1;
    a >>= 1;
  }

  while (b > 0) {
    count += b & 1;
    b >>= 1;
  }

  return count == ns.mask;
}

///
/// QuicRClient
///

quicr::QuicRClient::QuicRClient(
  ITransport& transport_in,
  std::shared_ptr<SubscriberDelegate> subscriber_delegate,
  std::shared_ptr<PublisherDelegate> pub_delegate)
  : transport(transport_in)
  , sub_delegate(subscriber_delegate)
  , pub_delegate(pub_delegate)
  , state(std::make_unique<State>())
{
  transport_context_id = transport.start();
}

QuicRClient::QuicRClient(
  ITransport& transport_in,
  std::shared_ptr<SubscriberDelegate> subscriber_delegate)
  : transport(transport_in)
  , sub_delegate(subscriber_delegate)
  , state(std::make_unique<State>())
{
  transport_context_id = transport.start();
}

QuicRClient::QuicRClient(ITransport& transport_in,
                         std::shared_ptr<PublisherDelegate> pub_delegate)
  : transport(transport_in)
  , pub_delegate(pub_delegate)
  , state(std::make_unique<State>())
{
  transport_context_id = transport.start();
}

bool
QuicRClient::publishIntent(const QUICRNamespace& quicr_namespace,
                           const std::string& origin_url,
                           const std::string& auth_token,
                           bytes&& payload)
{
  return false;
}

void
QuicRClient::publishIntentEnd(const QUICRNamespace& quicr_namespace,
                              const std::string& auth_token)
{
}

void
QuicRClient::subscribe(const QUICRNamespace& quicr_namespace,
                       const SubscribeIntent& intent,
                       const std::string& origin_url,
                       bool use_reliable_transport,
                       const std::string& auth_token,
                       bytes&& e2e_token)
{
  // encode subscribe
  messages::MessageBuffer msg{};
  auto transaction_id = messages::transaction_id();
  messages::Subscribe subscribe{ 0x1, transaction_id, quicr_namespace, intent };
  msg << subscribe;

  qtransport::MediaStreamId msid{};
  if (!subscribe_state.count(quicr_namespace)) {
    // create a new media-stream for this subscribe
    auto msid = transport.createMediaStream(transaction_id, false);
    subscribe_state[quicr_namespace] =
      SubscribeContext{ SubscribeContext::State::Pending,
                        transport_context_id,
                        msid,
                        transaction_id };
    transport.enqueue(transport_context_id, msid, std::move(msg.buffer));
    return;
  } else {
    auto& ctx = subscribe_state[quicr_namespace];
    if (ctx.state == SubscribeContext::State::Ready) {
      // already subscribed
      return;
    } else if (ctx.state == SubscribeContext::State::Pending) {
      // todo - resend or wait or may be take in timeout in the api
    }
    transport.enqueue(transport_context_id, msid, std::move(msg.buffer));
  }
}

void
QuicRClient::unsubscribe(const QUICRNamespace& quicr_namespace,
                         const std::string& origin_url,
                         const std::string& auth_token)
{
}

void
QuicRClient::publishNamedObject(const QUICRName& quicr_name,
                                uint8_t priority,
                                uint16_t expiry_age_ms,
                                bool use_reliable_transport,
                                bytes&& data)
{
}

void
QuicRClient::publishNamedObjectFragment(const QUICRName& quicr_name,
                                        uint8_t priority,
                                        uint16_t expiry_age_ms,
                                        bool use_reliable_transport,
                                        const uint64_t& offset,
                                        bool is_last_fragment,
                                        bytes&& data)
{
  throw std::runtime_error("UnImplemented");
}

} // namespace quicr