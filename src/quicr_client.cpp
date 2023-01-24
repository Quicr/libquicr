#include <quicr/quicr_client.h>
#include <quicr/quicr_common.h>

#include "encode.h"
#include "message_buffer.h"

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
{
  transport_context_id = transport.start();
}

QuicRClient::QuicRClient(
  ITransport& transport_in,
  std::shared_ptr<SubscriberDelegate> subscriber_delegate)
  : transport(transport_in)
  , sub_delegate(subscriber_delegate)
{
  transport_context_id = transport.start();
}

QuicRClient::QuicRClient(ITransport& transport_in,
                         std::shared_ptr<PublisherDelegate> pub_delegate)
  : transport(transport_in)
  , pub_delegate(pub_delegate)
{
  transport_context_id = transport.start();
}

bool
QuicRClient::publishIntent(const QUICRNamespace& quicr_namespace,
                           const std::string& origin_url,
                           const std::string& auth_token,
                           bytes&& payload)
{
  throw std::runtime_error("UnImplemented");
}

void
QuicRClient::publishIntentEnd(const QUICRNamespace& quicr_namespace,
                              const std::string& auth_token)
{
  throw std::runtime_error("UnImplemented");
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
    auto msid = transport.createMediaStream(transport_context_id, false);
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
  throw std::runtime_error("UnImplemented");
}

void
QuicRClient::publishNamedObject(const QUICRName& quicr_name,
                                uint8_t priority,
                                uint16_t expiry_age_ms,
                                bool use_reliable_transport,
                                bytes&& data)
{
  // start populating message to encode
  messages::PublishDatagram datagram;
  // retrieve the context
  PublishContext context{};

  if (!publish_state.count(quicr_name)) {
    auto msid = transport.createMediaStream(transport_context_id, false);
    context.transport_context_id = transport_context_id;
    context.media_stream_id = msid;
    context.state = PublishContext::State::Pending;
    context.group_id = 0;
    context.object_id = 0;
  } else {
    context = publish_state[quicr_name];
    datagram.header.media_id =
      static_cast<messages::uintVar_t>(context.media_stream_id);
  }

  datagram.header.media_id =
    static_cast<messages::uintVar_t>(context.media_stream_id);
  datagram.header.group_id = static_cast<messages::uintVar_t>(context.group_id);
  datagram.header.object_id =
    static_cast<messages::uintVar_t>(context.object_id);
  datagram.header.flags = 0x0;
  datagram.header.offset_and_fin = static_cast<messages::uintVar_t>(1);
  datagram.media_type = messages::MediaType::RealtimeMedia;
  datagram.media_data_length = static_cast<messages::uintVar_t>(data.size());
  datagram.media_data = std::move(data);

  messages::MessageBuffer msg;
  msg << datagram;

  transport.enqueue(
    transport_context_id, context.media_stream_id, std::move(msg.buffer));
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