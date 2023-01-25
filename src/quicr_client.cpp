#include <sstream>
#include <iomanip>
#include <iostream>

#include <quicr/quicr_client.h>
#include <quicr/quicr_common.h>

#include "encode.h"
#include "quicr/message_buffer.h"

namespace quicr {
///
/// Common
///

// TODO Remove this
static std::string to_hex(const quicr::QUICRNamespace& ns) {
  std::stringstream stream;
  stream << "0x"
         << std::setfill ('0')
         << std::setw(sizeof(uint64_t)*2)
         << std::hex
         << ns.hi
         << std::setw(sizeof(uint64_t)*2)
         << ns.low;
  return stream.str();
}

static std::string to_hex(const quicr::QUICRName& name) {
  std::stringstream stream;
  stream << "0x"
         << std::setfill ('0')
         << std::setw(sizeof(uint64_t)*2)
         << std::hex
         << name.hi
         << std::setw(sizeof(uint64_t)*2)
         << name.low;
  return stream.str();
}


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
/// Transport Delegate Implementation
///

class QuicRTransportDelegate : public ITransport::TransportDelegate
{
public:
  QuicRTransportDelegate(std::shared_ptr<ITransport> transport_in, QuicRClient& client_in)
    : transport(transport_in),
      client(client_in) {}

  virtual ~QuicRTransportDelegate() = default;

  virtual void on_connection_status(
    const qtransport::TransportContextId& context_id,
    const qtransport::TransportStatus status)
  {
  }

  virtual void on_new_connection(
    const qtransport::TransportContextId& context_id,
    const qtransport::TransportRemote& remote)
  {
  }

  virtual void on_new_media_stream(
    const qtransport::TransportContextId& context_id,
    const qtransport::MediaStreamId& mStreamId)
  {
  }

  virtual void on_recv_notify(const qtransport::TransportContextId& context_id,
                              const qtransport::MediaStreamId& mStreamId)
  {
    auto data = transport->dequeue(context_id, mStreamId);
    if (!data.has_value()) {
      return;
    }
    messages::MessageBuffer msg_buffer {data.value()};
    client.handle(std::move(msg_buffer));
  }
private:
  std::shared_ptr<ITransport> transport;
  QuicRClient& client;
};


void QuicRClient::make_transport(RelayInfo& relay_info, qtransport::LogHandler& logger)
{
  qtransport::TransportRemote server = {
    host_or_ip : relay_info.hostname,
    port : relay_info.port,
    proto : relay_info.proto == RelayInfo::Protocol::UDP
      ? qtransport::TransportProtocol::UDP
      : qtransport::TransportProtocol::QUIC,
  };
  transport_delegate = std::make_unique<QuicRTransportDelegate>();
  transport = qtransport::ITransport::make_client_transport(server, *transport_delegate, logger);
  transport_context_id = transport->start();
}

///
/// QuicRClient
///
/// \param relayInfo
QuicRClient::QuicRClient(RelayInfo& relay_info, qtransport::LogHandler& logger)
  : transport_delegate(std::make_unique<QuicRTransportDelegate>())
{
  make_transport(relay_info, logger);
}

bool
QuicRClient::publishIntent(std::shared_ptr<PublisherDelegate> pub_delegate,
                           const QUICRNamespace& quicr_namespace,
                           const std::string& origin_url,
                           const std::string& auth_token,
                           bytes&& payload)
{
  this->pub_delegate = pub_delegate;
  throw std::runtime_error("UnImplemented");
}

void
QuicRClient::publishIntentEnd(const QUICRNamespace& quicr_namespace,
                              const std::string& auth_token)
{
  throw std::runtime_error("UnImplemented");
}

void
QuicRClient::subscribe(std::shared_ptr<SubscriberDelegate> subscriber_delegate,
                       const QUICRNamespace& quicr_namespace,
                       const SubscribeIntent& intent,
                       const std::string& origin_url,
                       bool use_reliable_transport,
                       const std::string& auth_token,
                       bytes&& e2e_token)
{

  if(!sub_delegates.count(quicr_namespace)) {
    sub_delegates[quicr_namespace] = subscriber_delegate;
  }

  // encode subscribe
  messages::MessageBuffer msg{};
  auto transaction_id = messages::transaction_id();
  messages::Subscribe subscribe{ 0x1, transaction_id, quicr_namespace, intent };
  msg << subscribe;

  qtransport::MediaStreamId msid{};
  if (!subscribe_state.count(quicr_namespace)) {
    // create a new media-stream for this subscribe
    auto msid = transport->createMediaStream(transport_context_id, false);
    subscribe_state[quicr_namespace] =
      SubscribeContext{ SubscribeContext::State::Pending,
                        transport_context_id,
                        msid,
                        transaction_id };
    transport->enqueue(transport_context_id, msid, std::move(msg.buffer));
    return;
  } else {
    auto& ctx = subscribe_state[quicr_namespace];
    if (ctx.state == SubscribeContext::State::Ready) {
      // already subscribed
      return;
    } else if (ctx.state == SubscribeContext::State::Pending) {
      // todo - resend or wait or may be take in timeout in the api
    }
    transport->enqueue(transport_context_id, msid, std::move(msg.buffer));
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
    auto msid = transport->createMediaStream(transport_context_id, false);
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

  transport->enqueue(
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

void
QuicRClient::handle(messages::MessageBuffer&& msg)
{
  if(msg.buffer.empty()) {
    std::cout << "Transport Reported Empty Data" <<std::endl;

    return;
  }

  uint8_t msg_type = msg.back();

  if (msg_type == static_cast<uint8_t>(messages::MessageType::SubscribeResponse)) {
    messages::SubscribeResponse response;
    msg >> response;
    if (sub_delegates.count(response.quicr_namespace)) {
      sub_delegates[response.quicr_namespace]->onSubscribeResponse(response.quicr_namespace, response.response);
    } else {
      std::cout << "Got SubscribeResponse: No delegate found for namespace" << to_hex(response.quicr_namespace) << std::endl;
    }
  } else if (msg_type ==
             static_cast<uint8_t>(messages::MessageType::Publish)) {
    messages::PublishDatagram datagram;
    msg >> datagram;
    for (const auto& entry : sub_delegates) {
      if (is_quicr_name_in_namespace(entry.first, datagram.header.name)) {
        sub_delegates[entry.first]->onSubscribedObject(datagram.header.name, 0x0, false, std::move(datagram.media_data));
      } else {
        std::cout << "Name:" << to_hex(datagram.header.name) << ", not in namespace " << to_hex(entry.first) << std::endl;
      }
    }
  }

}
} // namespace quicr