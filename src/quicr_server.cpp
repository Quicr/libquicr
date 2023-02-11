#include <quicr/quicr_common.h>
#include <quicr/quicr_server.h>

#include "encode.h"
#include "quicr/message_buffer.h"
#include <iostream>
#include <sstream>
#include <thread>

#include <arpa/inet.h>

namespace quicr {

void
ServerDelegate::onPublishedFragment(const quicr::Name& /* quicr_name */,
                                    uint8_t /* priority */,
                                    uint16_t /* expiry_age_ms */,
                                    bool /* use_reliable_transport */,
                                    const uint64_t& /* offset */,
                                    bool /* is_last_fragment */,
                                    bytes&& /* data */)
{
}
void
ServerDelegate::onSubscribe(const quicr::Namespace& /* quicr_namespace */,
                            const uint64_t& /* subscriber_id */,
                            const SubscribeIntent /* subscribe_intent */,
                            const std::string& /* origin_url */,
                            bool /* use_reliable_transport */,
                            const std::string& /* auth_token */,
                            bytes&& /* data */)
{
}

void
ServerDelegate::onUnsubscribe(const quicr::Namespace& /* quicr_namespace */,
                              const uint64_t& /* subscriber_id */,
                              const std::string& /* auth_token */)
{
}

/*
 * Start the  QUICR server at the port specified.
 *  @param delegate_in: Callback handlers for QUICR operations
 */
QuicRServer::QuicRServer(RelayInfo& relayInfo,
                         ServerDelegate& delegate_in,
                         qtransport::LogHandler& logger)
  : delegate(delegate_in)
  , log_handler(logger)
  , transport_delegate(*this)

{
  t_relay.host_or_ip = relayInfo.hostname;
  t_relay.port = relayInfo.port;
  switch (relayInfo.proto) {
    case RelayInfo::Protocol::UDP:
      t_relay.proto = qtransport::TransportProtocol::UDP;
      break;
    default:
      t_relay.proto = qtransport::TransportProtocol::QUIC;
      break;
  }

  transport = setupTransport(relayInfo);
  transport->start();
}

QuicRServer::QuicRServer(std::shared_ptr<qtransport::ITransport> transport_in,
                         ServerDelegate& delegate_in,
                         qtransport::LogHandler& logger)
  : delegate(delegate_in)
  , log_handler(logger)
  , transport_delegate(*this)
  , transport(transport_in)
{
}

std::shared_ptr<qtransport::ITransport>
QuicRServer::setupTransport([[maybe_unused]] RelayInfo& relayInfo)
{
  return qtransport::ITransport::make_server_transport(
    t_relay, transport_delegate, log_handler);
}

// Transport APIs
bool
QuicRServer::is_transport_ready()
{
  return false;
}

/**
 * @brief Run Server API event loop
 *
 * @details This method will open listening sockets and run an event loop
 *    for callbacks.
 *
 * @returns true if error, false if no error
 */
bool
QuicRServer::run()
{
  running = true;

  return true;
}

void
QuicRServer::publishIntentResponse(
  const quicr::Namespace& /* quicr_namespace */,
  const PublishIntentResult& /* result */)
{
  throw std::runtime_error("Unimplemented");
}

void
QuicRServer::subscribeResponse(const quicr::Namespace& quicr_namespace,
                               const uint64_t& transaction_id,
                               const SubscribeResult& result)
{
  messages::SubscribeResponse response;
  response.transaction_id = transaction_id;
  response.quicr_namespace = quicr_namespace;
  response.response = result.status;

  messages::MessageBuffer msg;
  msg << response;
  // transport.enqueue(transport_context_id, 0x0, std::move(msg.buffer));
}

void
QuicRServer::subscriptionEnded(const quicr::Namespace& /* quicr_namespace */,
                               const SubscribeResult& /* result */)
{
  throw std::runtime_error("Unimplemented");
}

void
QuicRServer::sendNamedObject(const uint64_t& subscriber_id,
                             const quicr::Name& quicr_name,
                             [[maybe_unused]] uint8_t priority,
                             [[maybe_unused]] uint64_t best_before,
                             [[maybe_unused]] bool use_reliable_transport,
                             bytes&& data)
{
  // start populating message to encode
  messages::PublishDatagram datagram;
  if (subscribe_id_state.count(subscriber_id) == 0) {
    return;
  }

  auto& context = subscribe_id_state[subscriber_id];
  datagram.header.name = quicr_name;
  datagram.header.media_id = static_cast<uintVar_t>(context.media_stream_id);
  datagram.header.flags = 0x0;
  datagram.header.offset_and_fin = static_cast<uintVar_t>(1);
  datagram.media_type =
    messages::MediaType::RealtimeMedia; // TODO this should not be here
  datagram.media_data_length = static_cast<uintVar_t>(data.size());
  datagram.media_data = std::move(data);

  messages::MessageBuffer msg;
  msg << datagram;

  transport->enqueue(
    context.transport_context_id, context.media_stream_id, msg.get());
}

void
QuicRServer::sendNamedFragment(const quicr::Name& /* name */,
                               uint8_t /* priority */,
                               uint64_t /* best_before */,
                               bool /* use_reliable_transport */,
                               uint64_t /* offset */,
                               bool /* is_last_fragment */,
                               bytes&& /* data */)
{
  throw std::runtime_error("Unimplemented");
}

///
/// Private
///

void
QuicRServer::handle_subscribe(const qtransport::TransportContextId& context_id,
                              const qtransport::MediaStreamId& mStreamId,
                              messages::MessageBuffer&& msg)
{
  messages::Subscribe subscribe;
  msg >> subscribe;

  if (subscribe_state[subscribe.quicr_namespace].count(context_id) == 0) {
    SubscribeContext context;
    context.transport_context_id = context_id;
    context.media_stream_id = mStreamId;
    context.subscriber_id = subscriber_id;

    subscriber_id++;

    subscribe_state[subscribe.quicr_namespace][context_id] = context;
    subscribe_id_state[context.subscriber_id] = context;
  }

  auto& context = subscribe_state[subscribe.quicr_namespace][context_id];

  delegate.onSubscribe(subscribe.quicr_namespace,
                       context.subscriber_id,
                       subscribe.intent,
                       "",
                       false,
                       "",
                       {});
}

void
QuicRServer::handle_publish(
  [[maybe_unused]] const qtransport::TransportContextId& context_id,
  [[maybe_unused]] const qtransport::MediaStreamId& mStreamId,
  messages::MessageBuffer&& msg)
{
  messages::PublishDatagram datagram;
  msg >> datagram;
  // TODO: Add publish_state when we support PublishIntent

  delegate.onPublisherObject(datagram.header.name,
                             context_id,
                             mStreamId,
                             0,
                             0,
                             false,
                             std::move(datagram.media_data));
}

// --------------------------------------------------
// Transport Delegate Implementation
// ---------------
QuicRServer::TransportDelegate::TransportDelegate(quicr::QuicRServer& server)
  : server(server)
{
}

void
QuicRServer::TransportDelegate::on_connection_status(
  const qtransport::TransportContextId& context_id,
  const qtransport::TransportStatus status)
{
  std::stringstream log_msg;
  log_msg << "connection_status: cid: " << context_id
          << " status: " << int(status);
  server.log_handler.log(qtransport::LogLevel::debug, log_msg.str());
}

void
QuicRServer::TransportDelegate::on_new_connection(
  const qtransport::TransportContextId& context_id,
  const qtransport::TransportRemote& remote)
{
  std::stringstream log_msg;
  log_msg << "new_connection: cid: " << context_id
          << " remote: " << remote.host_or_ip << " port:" << ntohs(remote.port);
  server.log_handler.log(qtransport::LogLevel::debug, log_msg.str());
}

void
QuicRServer::TransportDelegate::on_new_media_stream(
  const qtransport::TransportContextId& context_id,
  const qtransport::MediaStreamId& mStreamId)
{
  std::stringstream log_msg;
  log_msg << "new_stream: cid: " << context_id << " msid: " << mStreamId;

  server.log_handler.log(qtransport::LogLevel::debug, log_msg.str());
}

void
QuicRServer::TransportDelegate::on_recv_notify(
  const qtransport::TransportContextId& context_id,
  const qtransport::MediaStreamId& mStreamId)
{
  while (true) {
    auto data = server.transport->dequeue(context_id, mStreamId);

    if (data.has_value()) {
      uint8_t msg_type = data.value().back();
      messages::MessageBuffer msg_buffer{ data.value() };
      if (msg_type == static_cast<uint8_t>(messages::MessageType::Subscribe)) {
        server.handle_subscribe(context_id, mStreamId, std::move(msg_buffer));
      } else if (msg_type ==
                 static_cast<uint8_t>(messages::MessageType::Publish)) {
        server.handle_publish(context_id, mStreamId, std::move(msg_buffer));
      }

    } else {
      break;
    }
  }
}

} /* namespace end */
