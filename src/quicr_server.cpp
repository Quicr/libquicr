#include <quicr/quicr_common.h>
#include <quicr/quicr_server.h>

#include <iostream>
#include <quicr/encode.h>
#include <quicr/message_buffer.h>
#include <sstream>
#include <thread>

#include <arpa/inet.h>

namespace quicr {
void
ServerDelegate::onSubscribe(
  const quicr::Namespace& /*quicr_namespace*/,
  const uint64_t& /*subscriber_id*/,
  const qtransport::TransportContextId& /*context_id*/,
  const qtransport::StreamId& /*stream_id*/,
  const SubscribeIntent /*subscribe_intent*/,
  const std::string& /*origin_url*/,
  bool /*use_reliable_transport*/,
  const std::string& /*auth_token*/,
  bytes&& /*data*/)
{
}

void
ServerDelegate::onUnsubscribe(const quicr::Namespace& /*quicr_namespace*/,
                              const uint64_t& /*subscriber_id*/,
                              const std::string& /*auth_token*/)
{
}

/*
 * Start the  QUICR server at the port specified.
 *  @param delegate_in: Callback handlers for QUICR operations
 */
QuicRServer::QuicRServer(RelayInfo& relayInfo,
                         qtransport::TransportConfig tconfig,
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

  transport = setupTransport(relayInfo, std::move(tconfig));
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
QuicRServer::setupTransport([[maybe_unused]] RelayInfo& relayInfo,
                            qtransport::TransportConfig cfg)
{

  return qtransport::ITransport::make_server_transport(
    t_relay, cfg, transport_delegate, log_handler);

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

  while (transport->status() != qtransport::TransportStatus::Ready) {
    log_handler.log(qtransport::LogLevel::info, "Waiting for server to be ready");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return true;
}

void
QuicRServer::publishIntentResponse(const quicr::Namespace& quicr_namespace,
                                   const PublishIntentResult& result)
{
  if (!publish_namespaces.count(quicr_namespace))
    return;

  auto& context = publish_namespaces[quicr_namespace];
  messages::PublishIntentResponse response{
    messages::MessageType::PublishIntentResponse,
    quicr_namespace,
    result.status,
    context.transaction_id
  };

  messages::MessageBuffer msg(sizeof(response));
  msg << response;

  context.state = PublishIntentContext::State::Ready;

  transport->enqueue(
    context.transport_context_id, context.transport_stream_id, msg.get());
}

void
QuicRServer::subscribeResponse(const uint64_t& subscriber_id,
                               const quicr::Namespace& quicr_namespace,
                               const SubscribeResult& result)
{
  // start populating message to encode
  if (subscribe_id_state.count(subscriber_id) == 0) {
    return;
  }

  auto& context = subscribe_id_state[subscriber_id];

  messages::SubscribeResponse response;
  response.transaction_id = subscriber_id;
  response.quicr_namespace = quicr_namespace;
  response.response = result.status;

  messages::MessageBuffer msg;
  msg << response;

  transport->enqueue(
    context.transport_context_id, context.transport_stream_id, msg.get());
}

void
QuicRServer::subscriptionEnded(const uint64_t& subscriber_id,
                               const quicr::Namespace& quicr_namespace,
                               const SubscribeResult::SubscribeStatus& reason)
{
  // start populating message to encode
  if (subscribe_id_state.count(subscriber_id) == 0) {
    return;
  }

  auto& context = subscribe_id_state[subscriber_id];

  messages::SubscribeEnd subEnd;
  subEnd.quicr_namespace = quicr_namespace;
  subEnd.reason = reason;

  messages::MessageBuffer msg;
  msg << subEnd;

  transport->enqueue(
    context.transport_context_id, context.transport_stream_id, msg.get());
}

void
QuicRServer::sendNamedObject(const uint64_t& subscriber_id,
                             [[maybe_unused]] bool use_reliable_transport,
                             const messages::PublishDatagram& datagram)
{
  // start populating message to encode
  if (subscribe_id_state.count(subscriber_id) == 0) {
    return;
  }

  auto& context = subscribe_id_state[subscriber_id];
  messages::MessageBuffer msg;

  msg << datagram;

  transport->enqueue(
    context.transport_context_id, context.transport_stream_id, msg.get());
}

///
/// Private
///

void
QuicRServer::handle_subscribe(const qtransport::TransportContextId& context_id,
                              const qtransport::StreamId& streamId,
                              messages::MessageBuffer&& msg)
{
  messages::Subscribe subscribe;
  msg >> subscribe;

  std::lock_guard<std::mutex> lock(mutex);

  if (subscribe_state[subscribe.quicr_namespace].count(context_id) == 0) {

    SubscribeContext context;
    context.transport_context_id = context_id;
    context.transport_stream_id = streamId;
    context.subscriber_id = subscriber_id;

    subscriber_id++;

    subscribe_state[subscribe.quicr_namespace][context_id] = context;
    subscribe_id_state[context.subscriber_id] = context;
  }

  auto& context = subscribe_state[subscribe.quicr_namespace][context_id];

  delegate.onSubscribe(subscribe.quicr_namespace,
                       context.subscriber_id,
                       context_id,
                       streamId,
                       subscribe.intent,
                       "",
                       false,
                       "",
                       {});
}

void
QuicRServer::handle_unsubscribe(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& /* streamId */,
  messages::MessageBuffer&& msg)
{
  messages::Unsubscribe unsub;
  msg >> unsub;

  // Remove states if state exists
  if (subscribe_state[unsub.quicr_namespace].count(context_id) != 0) {

    std::lock_guard<std::mutex> lock(mutex);

    auto& context = subscribe_state[unsub.quicr_namespace][context_id];

    // Before removing, exec callback
    delegate.onUnsubscribe(unsub.quicr_namespace, context.subscriber_id, {});


    subscribe_id_state.erase(context.subscriber_id);
    subscribe_state[unsub.quicr_namespace].erase(context_id);

    if (subscribe_state[unsub.quicr_namespace].size() > 0) {
      subscribe_state.erase(unsub.quicr_namespace);
    }
  }
}

void
QuicRServer::handle_publish(const qtransport::TransportContextId& context_id,
                            const qtransport::MediaStreamId& streamId,
                            messages::MessageBuffer&& msg)
{
  messages::PublishDatagram datagram;
  msg >> datagram;

  auto publish_namespace =
    std::find_if(publish_namespaces.begin(),
                 publish_namespaces.end(),
                 [&datagram](const auto& ns) {
                   return ns.first.contains(datagram.header.name);
                 });

  if (publish_namespace == publish_namespaces.end()) {
    // No such namespace, don't publish yet.
    return;
  }

  PublishContext context;
  if (!publish_state.count(datagram.header.name)) {
    context.transport_context_id = context_id;
    context.transport_stream_id = streamId;

    publish_state[datagram.header.name] = context;
  } else {
    context = publish_state[datagram.header.name];
  }

  delegate.onPublisherObject(context.transport_context_id,
                             context._stream_id,
                             false,
                             std::move(datagram));
}

void
QuicRServer::handle_publish_intent(
  const qtransport::TransportContextId& context_id,
  const qtransport::MediaStreamId& streamId,
  messages::MessageBuffer&& msg)
{
  messages::PublishIntent intent;
  msg >> intent;

  if (!publish_namespaces.count(intent.quicr_namespace)) {
    PublishIntentContext context;
    context.state = PublishIntentContext::State::Pending;
    context.transport_context_id = context_id;
    context.transport_stream_id = streamId;
    context.transaction_id = intent.transaction_id;

    publish_namespaces[intent.quicr_namespace] = context;
  } else {
    auto state = publish_namespaces[intent.quicr_namespace].state;
    switch (state) {
      case PublishIntentContext::State::Pending:
        // TODO: Resend response?
        break;
      case PublishIntentContext::State::Ready:
        // TODO: Already registered this namespace successfully, do nothing?
        break;
      default:
        break;
    }
  }

  delegate.onPublishIntent(intent.quicr_namespace,
                           "" /* intent.origin_url */,
                           false,
                           "" /* intent.relay_token */,
                           std::move(intent.payload));
}

void
QuicRServer::handle_publish_intent_end(
  [[maybe_unused]] const qtransport::TransportContextId& context_id,
  [[maybe_unused]] const qtransport::StreamId& streamId,
  messages::MessageBuffer&& msg)
{
  messages::PublishIntentEnd intent_end;
  msg >> intent_end;

  const auto& name = intent_end.quicr_namespace;

  if (!publish_namespaces.count(intent_end.quicr_namespace)) {
    return;
  }

  publish_namespaces.erase(name);

  for (auto it = publish_state.begin(); it != publish_state.end();) {
    const auto& [name, _] = *it;
    if (intent_end.quicr_namespace.contains(name))
      it = publish_state.erase(it);
    else
      ++it;
  }

  delegate.onPublishIntentEnd(intent_end.quicr_namespace,
                              "" /* intent_end.relay_token */,
                              std::move(intent_end.payload));
}

/*===========================================================================*/
// Transport Delegate Implementation
/*===========================================================================*/

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

  if (status == qtransport::TransportStatus::Disconnected) {
    log_msg.str("");
    log_msg << "Removing state for context_id: " << context_id;
    server.log_handler.log(qtransport::LogLevel::info, log_msg.str());

    std::lock_guard<std::mutex> lock(server.mutex);

    std::vector<quicr::Namespace> namespaces_to_remove;
    for (auto& sub: server.subscribe_state) {
      if (sub.second.count(context_id) != 0) {


        const auto& stream_id = sub.second[context_id].subscriber_id;

        // Before removing, exec callback
        server.delegate.onUnsubscribe(sub.first, stream_id, {});

        server.subscribe_id_state.erase(stream_id);

        if (sub.second.size() == 0) {
          namespaces_to_remove.push_back(sub.first);
        }
      }

      for (const auto& ns: namespaces_to_remove) {
          server.subscribe_state.erase(ns);
      }
    }
  }

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
QuicRServer::TransportDelegate::on_new_stream(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& streamId)
{
  std::stringstream log_msg;
  log_msg << "new_stream: cid: " << context_id << " msid: " << streamId;

  server.log_handler.log(qtransport::LogLevel::debug, log_msg.str());
}

void
QuicRServer::TransportDelegate::on_recv_notify(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& streamId)
{

  // TODO: Consider running this in a task/async thread
  while (true) {
    auto data = server.transport->dequeue(context_id, streamId);

    if (data.has_value()) {
      try {
        // TODO: Extracting type will change when the message is encoded
        // correctly
        auto msg_type = static_cast<messages::MessageType>(data->front());
        messages::MessageBuffer msg_buffer{ data.value() };

        switch (msg_type) {
          case messages::MessageType::Subscribe:
            server.handle_subscribe(
              context_id, streamId, std::move(msg_buffer));
            break;
          case messages::MessageType::Publish:
            server.handle_publish(context_id, streamId, std::move(msg_buffer));
            break;
          case messages::MessageType::Unsubscribe:
            server.handle_unsubscribe(
              context_id, streamId, std::move(msg_buffer));
            break;
          case messages::MessageType::PublishIntent: {
            server.handle_publish_intent(
              context_id, streamId, std::move(msg_buffer));
            break;
          }
          case messages::MessageType::PublishIntentEnd: {
            server.handle_publish_intent_end(
              context_id, streamId, std::move(msg_buffer));
            break;
          }
          default:
            break;
        }
      } catch (const messages::MessageBuffer::ReadException& /* ex */) {
        continue;
      } catch (const std::exception& /* ex */) {
        continue;
      } catch (...) {
        server.log_handler.log(
          qtransport::LogLevel::fatal,
          "Received unknown error while reading from message buffer.");
        throw;
      }
    } else {
      break;
    }
  }
}

} /* namespace end */
