/*
 *  quicr_server_raw_session.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 *
 *  Description:
 *      This file implements a session layer between the client APIs and the
 *      transport that uses raw data packets (either UDP or QUIC).
 *
 *  Portability Issues:
 *      None.
 */

#include "quicr_server_raw_session.h"

#include "quicr/encode.h"
#include "quicr/gap_check.h"
#include "quicr/message_buffer.h"
#include "quicr/quicr_common.h"

#include <algorithm>
#include <arpa/inet.h>
#include <sstream>
#include <thread>

namespace quicr {
/*
 * Initialize the QUICR server session at the port specified.
 *  @param delegate_in: Callback handlers for QUICR operations
 */
ServerRawSession::ServerRawSession(const RelayInfo& relayInfo,
                                   const qtransport::TransportConfig& tconfig,
                                   std::shared_ptr<ServerDelegate> delegate_in,
                                   const cantina::LoggerPointer& logger)
  : delegate(std::move(delegate_in))
  , logger(std::make_shared<cantina::Logger>("QSES", logger))
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

  transport = setupTransport(tconfig);
  transport->start();
}

ServerRawSession::ServerRawSession(
  std::shared_ptr<qtransport::ITransport> transport_in,
  std::shared_ptr<ServerDelegate> delegate_in,
  const cantina::LoggerPointer& logger)
  : delegate(std::move(delegate_in))
  , logger(std::make_shared<cantina::Logger>("QSES", logger))
  , transport_delegate(*this)
  , transport(std::move(transport_in))
{
}

std::shared_ptr<qtransport::ITransport>
ServerRawSession::setupTransport(const qtransport::TransportConfig& cfg)
{

  return qtransport::ITransport::make_server_transport(
    t_relay, cfg, transport_delegate, logger);
}

// Transport APIs
bool
ServerRawSession::is_transport_ready()
{
  return transport->status() == qtransport::TransportStatus::Ready;
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
ServerRawSession::run()
{
  _running = true;

  while (transport->status() == qtransport::TransportStatus::Connecting) {
    logger->Log("Waiting for server to be ready");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return transport->status() == qtransport::TransportStatus::Ready;
}

void
ServerRawSession::publishIntentResponse(const quicr::Namespace& quicr_namespace,
                                        const PublishIntentResult& result)
{
  if (!publish_namespaces.contains(quicr_namespace)) {
    return;
  }

  // TODO(trigaux): Support more than one publisher per ns
  auto& context = publish_namespaces[quicr_namespace];
  const auto response = messages::PublishIntentResponse{
    messages::MessageType::PublishIntentResponse,
    quicr_namespace,
    result.status,
    context.transaction_id
  };

  messages::MessageBuffer msg(sizeof(response));
  msg << response;

  context.state = PublishIntentContext::State::Ready;

  const auto& conn_ctx = _connections[context.transport_conn_id];

  transport->enqueue(context.transport_conn_id, conn_ctx.ctrl_data_ctx_id, msg.take());
}

void
ServerRawSession::subscribeResponse(const uint64_t& subscriber_id,
                                    const quicr::Namespace& quicr_namespace,
                                    const SubscribeResult& result)
{
  // start populating message to encode
  if (!subscribe_id_state.contains(subscriber_id)) {
    return;
  }

  const auto& context = subscribe_id_state[subscriber_id];

  const auto response = messages::SubscribeResponse{
    .quicr_namespace = quicr_namespace,
    .response = result.status,
    .transaction_id = subscriber_id,
  };

  messages::MessageBuffer msg;
  msg << response;

  const auto& conn_ctx = _connections[context->transport_conn_id];

  transport->enqueue(context->transport_conn_id, conn_ctx.ctrl_data_ctx_id, msg.take());
}

void
ServerRawSession::subscriptionEnded(
  const uint64_t& subscriber_id,
  const quicr::Namespace& quicr_namespace,
  const SubscribeResult::SubscribeStatus& reason)
{
  // start populating message to encode
  if (!subscribe_id_state.contains(subscriber_id)) {
    return;
  }

  const auto& context = subscribe_id_state[subscriber_id];

  const auto subEnd = messages::SubscribeEnd{
    .quicr_namespace = quicr_namespace,
    .reason = reason,
  };

  messages::MessageBuffer msg;
  msg << subEnd;

  const auto& conn_ctx = _connections[context->transport_conn_id];

  transport->enqueue(context->transport_conn_id, conn_ctx.ctrl_data_ctx_id, msg.take());
}

void
ServerRawSession::sendNamedObject(const uint64_t& subscriber_id,
                                  uint8_t priority,
                                  uint16_t expiry_age_ms,
                                  const messages::PublishDatagram& datagram)
{
  // start populating message to encode
  if (!subscribe_id_state.contains(subscriber_id)) {
    logger->info << "Send Object, missing subscriber_id: " << subscriber_id
                 << std::flush;
    return;
  }

  // TODO(tievens): Might need to `std::lock_guard<std::mutex> _(session_mutex);`

  auto& context = subscribe_id_state[subscriber_id];

  if (context->paused) {
      return;
  }

  messages::MessageBuffer msg;
  msg << datagram;


  if (context->pending_reliable_data_ctx) {
    logger->info << "Starting new data context for subscriber_id: " << subscriber_id
                 << " replacing data_ctx_id: " << context->data_ctx_id << std::flush;

    context->priority = priority;
    context->pending_reliable_data_ctx = false;
    context->data_ctx_id = transport->createDataContext(context->transport_conn_id, true, priority, false);
  }

  if (context->priority != priority) {
    context->priority = priority;
  }

  qtransport::ITransport::EnqueueFlags eflags;

  switch (context->transport_mode) {
    case TransportMode::ReliablePerGroup: {
      uint64_t group_id = datagram.header.name.bits<uint64_t>(16, 32);

      if (context->group_id && context->group_id != group_id) {
        eflags.new_stream = true;
        eflags.use_reset = true;
        eflags.clear_tx_queue = true;
      }

      context->group_id = group_id;
      break;
    }

    case TransportMode::ReliablePerObject: {
      eflags.new_stream = true;
      break;
    }

    case TransportMode::ReliablePerTrack:
      [[fallthrough]];
    case TransportMode::Unreliable:
      [[fallthrough]];
    default:
      break;
  }

  transport->enqueue(context->transport_conn_id,
                     context->data_ctx_id,
                     msg.take(),
                     priority,
                     expiry_age_ms,
                     eflags);
}

///
/// Private
///

void
ServerRawSession::handle_subscribe(
  const qtransport::TransportConnId& conn_id,
  const qtransport::DataContextId& data_ctx_id,
  messages::MessageBuffer&& msg)
{
  auto subscribe = messages::Subscribe{};
  msg >> subscribe;

  std::lock_guard<std::mutex> _(session_mutex);

  if (_subscribe_state[subscribe.quicr_namespace].count(conn_id)) {
    // Duplicate
    return;
  }

  _subscribe_state[subscribe.quicr_namespace][conn_id] = std::make_shared<SubscribeContext>();

  auto &context = _subscribe_state[subscribe.quicr_namespace][conn_id];

  context->transport_conn_id = conn_id;
  context->subscriber_id = ++_subscriber_id;
  context->transport_mode = subscribe.transport_mode;

  subscribe_id_state[context->subscriber_id] = context;

  switch (context->transport_mode) {
    case TransportMode::ReliablePerTrack:
      [[fallthrough]];
    case TransportMode::ReliablePerGroup:
      [[fallthrough]];
    case TransportMode::ReliablePerObject: {
      context->pending_reliable_data_ctx = true;
      break;
    }

    case TransportMode::Unreliable: {
      context->data_ctx_id = transport->createDataContext(conn_id, false, context->priority, false);
      break;
    }

    case TransportMode::UsePublisher: {
      context->transport_mode_follow_publisher = true;
      context->data_ctx_id = transport->createDataContext(conn_id, true, context->priority, false);
    }

    case TransportMode::Pause:
      context->paused = true;
      delegate->onSubscribePause(subscribe.quicr_namespace, context->subscriber_id, conn_id, data_ctx_id, true);
      return;
    case TransportMode::Resume:
      context->paused = false;
      delegate->onSubscribePause(subscribe.quicr_namespace, context->subscriber_id, conn_id, data_ctx_id, false);
      return;
  }

   logger->debug << "New Subscribe conn_id: " << conn_id
                 << " transport_mode: " << static_cast<int>(context->transport_mode)
                 << " subscriber_id: " << context->subscriber_id
                 << " pending_data_ctx: " << context->pending_reliable_data_ctx
                 << std::flush;

  delegate->onSubscribe(subscribe.quicr_namespace,
                        context->subscriber_id,
                        conn_id,
                        data_ctx_id,
                        subscribe.intent,
                        "",
                        "",
                        {});
}

void
ServerRawSession::handle_unsubscribe(
  const qtransport::TransportConnId& conn_id,
  const qtransport::DataContextId& /* data_ctx_id */,
  messages::MessageBuffer&& msg)
{
  auto unsub = messages::Unsubscribe{};
  msg >> unsub;

  // Remove states if state exists
  if (_subscribe_state[unsub.quicr_namespace].contains(conn_id)) {
    const auto lock = std::lock_guard<std::mutex>(session_mutex);

    auto& context = _subscribe_state[unsub.quicr_namespace][conn_id];

    // Before removing, exec callback
    delegate->onUnsubscribe(unsub.quicr_namespace, context->subscriber_id, {});

    subscribe_id_state.erase(context->subscriber_id);
    _subscribe_state[unsub.quicr_namespace].erase(conn_id);

    if (_subscribe_state[unsub.quicr_namespace].empty()) {
      _subscribe_state.erase(unsub.quicr_namespace);
    }
  }
}

void
ServerRawSession::handle_publish(const qtransport::TransportConnId& conn_id,
                                 const qtransport::DataContextId& data_ctx_id,
                                 messages::MessageBuffer&& msg)
{
  messages::PublishDatagram datagram;
  msg >> datagram;

  auto publish_namespace = publish_namespaces.find(datagram.header.name);

  if (publish_namespace == publish_namespaces.end()) {
    // TODO(trigaux): Add metrics for tracking dropped messages
    logger->info << "Dropping published object, no namespace for "
                 << datagram.header.name << std::flush;
    return;
  }

  auto& [ns, context] = *publish_namespace;

  const auto gap_log = gap_check(
    false, datagram.header.name, context.prev_group_id, context.prev_object_id);

  if (!gap_log.empty()) {
    logger->info << "conn_id: " << conn_id << " data_ctx_id: " << data_ctx_id
                 << " " << gap_log << std::flush;
  }

  delegate->onPublisherObject(conn_id, data_ctx_id, std::move(datagram));
}

void
ServerRawSession::handle_publish_intent(
  const qtransport::TransportConnId& conn_id,
  [[maybe_unused]] const qtransport::DataContextId& data_ctx_id,
  messages::MessageBuffer&& msg)
{
  messages::PublishIntent intent;
  msg >> intent;

  // Always update/replace the old intent with the latest
  auto [ps_it, _] = publish_namespaces.insert_or_assign(intent.quicr_namespace, PublishIntentContext{});
  ps_it->second.state = PublishIntentContext::State::Pending;
  ps_it->second.transport_conn_id = conn_id;
  ps_it->second.transport_mode = intent.transport_mode;
  ps_it->second.transaction_id = intent.transaction_id;

  auto state = ps_it->second.state;

  // NOLINTBEGIN(bugprone-branch-clone)
  switch (state) {
    case PublishIntentContext::State::Pending:
      [[fallthrough]]; // TODO(trigaux): Resend response?
    case PublishIntentContext::State::Ready:
      [[fallthrough]]; // TODO(trigaux): Already registered this namespace
                       // successfully, do nothing?
    default:
      break;
  }

  delegate->onPublishIntent(intent.quicr_namespace,
                            "" /* intent.origin_url */,
                            "" /* intent.relay_token */,
                            std::move(intent.payload));
}

void
ServerRawSession::handle_publish_intent_end(
  [[maybe_unused]] const qtransport::TransportConnId& conn_id,
  [[maybe_unused]] const qtransport::DataContextId& data_ctx_id,
  messages::MessageBuffer&& msg)
{
  messages::PublishIntentEnd intent_end;
  msg >> intent_end;

  const auto& ns = intent_end.quicr_namespace;

  if (publish_namespaces.contains(ns)) {
    return;
  }

  publish_namespaces.erase(ns);

  delegate->onPublishIntentEnd(
    ns, "" /* intent_end.relay_token */, std::move(intent_end.payload));
}

/*===========================================================================*/
// Transport Delegate Implementation
/*===========================================================================*/

ServerRawSession::TransportDelegate::TransportDelegate(
  quicr::ServerRawSession& server)
  : server(server)
{
}

void
ServerRawSession::TransportDelegate::on_connection_status(
  const qtransport::TransportConnId& conn_id,
  const qtransport::TransportStatus status)
{
  LOGGER_DEBUG(server.logger,
               "connection_status: conn_id: " << conn_id
                                              << " status: " << int(status));

  if (status == qtransport::TransportStatus::Disconnected) {
    server.logger->info << "Removing state for conn_id: " << conn_id
                        << std::flush;

    std::lock_guard<std::mutex> _(server.session_mutex);
    server._connections.erase(conn_id);

    auto pub_names_to_remove = std::vector<quicr::Namespace>{};
    for (auto& [ns, context] : server.publish_namespaces) {
      if (context.transport_conn_id == conn_id) {
        pub_names_to_remove.push_back(ns);
        server.delegate->onPublishIntentEnd(ns, {}, {});
      }
    }

    for (auto& ns : pub_names_to_remove) {
      server.publish_namespaces.erase(ns);
    }

    auto sub_names_to_remove = std::vector<quicr::Namespace>{};
    for (auto& [ns, sub_map] : server._subscribe_state) {
      auto sub_it = sub_map.find(conn_id);
      if (sub_it != sub_map.end()) {
        server.delegate->onUnsubscribe(ns, sub_it->second->subscriber_id, {});
        server.subscribe_id_state.erase(sub_it->second->subscriber_id);
        sub_map.erase(sub_it);
      }

      if (sub_map.empty()) {
        sub_names_to_remove.push_back(ns);
      }
    }

    for (const auto& ns : sub_names_to_remove) {
      server._subscribe_state.erase(ns);
    }
  }
}

void
ServerRawSession::TransportDelegate::on_new_connection(
  const qtransport::TransportConnId& conn_id,
  const qtransport::TransportRemote& remote)
{
  LOGGER_DEBUG(server.logger,
               "new_connection: conn_id: " << conn_id
                                           << " remote: " << remote.host_or_ip
                                           << " port:" << ntohs(remote.port));


  auto& conn_ctx = server._connections[conn_id];
  conn_ctx.conn_id = conn_id;
  conn_ctx.ctrl_data_ctx_id = server.transport->createDataContext(conn_id, false, 0);
  conn_ctx.remote = remote;
}

void ServerRawSession::TransportDelegate::on_new_data_context(const qtransport::TransportConnId &conn_id,
                                                              const qtransport::DataContextId &data_ctx_id)
{
  LOGGER_DEBUG(server.logger, "New BiDir data context conn_id: " << conn_id << " data_ctx_id: " << data_ctx_id);
}

void
ServerRawSession::TransportDelegate::on_recv_notify(const qtransport::TransportConnId& conn_id,
                                                    const qtransport::DataContextId& data_ctx_id,
                                                    const bool is_bidir)
{
  // don't starve other queues, read some number of messages at a time
  for (int i = 0; i < 150; i++) {
    auto data = server.transport->dequeue(conn_id, data_ctx_id);

    if (data.has_value() && data.value().size() > 0) {
      server.recv_data_count++;
      try {
        auto msg_type = static_cast<messages::MessageType>(data->front());
        messages::MessageBuffer msg_buffer{ data.value() };

        switch (msg_type) {
          case messages::MessageType::Subscribe: {
              if (is_bidir) {
                auto& conn_ctx = server._connections[conn_id];
                conn_ctx.ctrl_data_ctx_id = data_ctx_id;
              }

              server.recv_subscribes++;
              server.handle_subscribe(
                conn_id, data_ctx_id, std::move(msg_buffer));
              break;
          }
          case messages::MessageType::Publish: {
              if (is_bidir) {
                auto& conn_ctx = server._connections[conn_id];
                conn_ctx.ctrl_data_ctx_id = data_ctx_id;
              }
            server.recv_publish++;
            server.handle_publish(conn_id, data_ctx_id, std::move(msg_buffer));
            break;
          }
          case messages::MessageType::Unsubscribe: {
            server.recv_unsubscribes++;
            server.handle_unsubscribe(
              conn_id, data_ctx_id, std::move(msg_buffer));
            break;
          }
          case messages::MessageType::PublishIntent: {
              if (is_bidir) {
                auto& conn_ctx = server._connections[conn_id];
                conn_ctx.ctrl_data_ctx_id = data_ctx_id;
              }
            server.recv_pub_intents++;
            server.handle_publish_intent(
              conn_id, data_ctx_id, std::move(msg_buffer));
            break;
          }
          case messages::MessageType::PublishIntentEnd: {
            server.handle_publish_intent_end(
              conn_id, data_ctx_id, std::move(msg_buffer));
            break;
          }
          default:
            server.logger->Log("Invalid Message Type");
            break;
        }
      } catch (const messages::MessageBuffer::ReadException& ex) {

        // TODO(trigaux): When reliable, we really should reset the stream if
        // this happens (at least more than once)
        server.logger->critical
          << "Received read exception error while reading from message buffer: "
          << ex.what() << std::flush;
        continue;

      } catch (const std::exception& /* ex */) {
        server.logger->Log(cantina::LogLevel::Critical,
                           "Received standard exception error while reading "
                           "from message buffer");
        continue;
      } catch (...) {
        server.logger->Log(
          cantina::LogLevel::Critical,
          "Received unknown error while reading from message buffer");
        continue;
      }
    } else {
      break;
    }
  }
}

} // namespace quicr
