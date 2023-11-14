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
  running = true;

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

  transport->enqueue(
    context.transport_context_id, context.transport_stream_id, msg.take());
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

  transport->enqueue(
    context.transport_context_id, context.transport_stream_id, msg.take());
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

  transport->enqueue(
    context.transport_context_id, context.transport_stream_id, msg.take());
}

void
ServerRawSession::sendNamedObject(const uint64_t& subscriber_id,
                                  [[maybe_unused]] bool use_reliable_transport,
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

  const auto& context = subscribe_id_state[subscriber_id];

  messages::MessageBuffer msg;
  msg << datagram;

  transport->enqueue(context.transport_context_id,
                     context.transport_stream_id,
                     msg.take(),
                     priority,
                     expiry_age_ms);
}

///
/// Private
///

void
ServerRawSession::handle_subscribe(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& streamId,
  messages::MessageBuffer&& msg)
{
  logger->info <<"Server:handle_subscribe: Stream: " << streamId << std::flush;
  auto subscribe = messages::Subscribe{};
  msg >> subscribe;

  const auto lock = std::lock_guard<std::mutex>(session_mutex);

  if (!subscribe_state[subscribe.quicr_namespace].contains(context_id)) {
    auto context = SubscribeContext{};
    context.transport_context_id = context_id;
    context.transport_stream_id = streamId;
    context.subscriber_id = subscriber_id;

    subscriber_id++;

    subscribe_state[subscribe.quicr_namespace][context_id] = context;
    subscribe_id_state[context.subscriber_id] = context;
  }

  const auto& context = subscribe_state[subscribe.quicr_namespace][context_id];

  delegate->onSubscribe(subscribe.quicr_namespace,
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
ServerRawSession::handle_unsubscribe(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& /* streamId */,
  messages::MessageBuffer&& msg)
{
  auto unsub = messages::Unsubscribe{};
  msg >> unsub;

  // Remove states if state exists
  if (subscribe_state[unsub.quicr_namespace].contains(context_id)) {
    const auto lock = std::lock_guard<std::mutex>(session_mutex);

    auto& context = subscribe_state[unsub.quicr_namespace][context_id];

    // Before removing, exec callback
    delegate->onUnsubscribe(unsub.quicr_namespace, context.subscriber_id, {});

    subscribe_id_state.erase(context.subscriber_id);
    subscribe_state[unsub.quicr_namespace].erase(context_id);

    if (subscribe_state[unsub.quicr_namespace].empty()) {
      subscribe_state.erase(unsub.quicr_namespace);
    }
  }
}

void
ServerRawSession::handle_publish(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& streamId,
  messages::MessageBuffer&& msg)
{
  logger->info <<"Server:handle_publish: Stream:" << streamId << std::flush;
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
    false, datagram.header.name, context.last_group_id, context.last_object_id);

  if (!gap_log.empty()) {
    logger->info << "context_id: " << context_id << " stream_id: " << streamId
                 << " " << gap_log << std::flush;
  }

  delegate->onPublisherObject(context_id, streamId, false, std::move(datagram));
}

void
ServerRawSession::handle_publish_intent(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& streamId,
  messages::MessageBuffer&& msg)
{
  messages::PublishIntent intent;
  msg >> intent;

  if (!publish_namespaces.contains(intent.quicr_namespace)) {
    PublishIntentContext context;
    context.state = PublishIntentContext::State::Pending;
    context.transport_context_id = context_id;
    context.transport_stream_id = streamId;
    context.transaction_id = intent.transaction_id;

    publish_namespaces[intent.quicr_namespace] = context;
  } else {
    auto state = publish_namespaces[intent.quicr_namespace].state;
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
    // NOLINTEND(bugprone-branch-clone)
  }

  delegate->onPublishIntent(intent.quicr_namespace,
                            "" /* intent.origin_url */,
                            false,
                            "" /* intent.relay_token */,
                            std::move(intent.payload));
}

void
ServerRawSession::handle_publish_intent_end(
  [[maybe_unused]] const qtransport::TransportContextId& context_id,
  [[maybe_unused]] const qtransport::StreamId& streamId,
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
  const qtransport::TransportContextId& context_id,
  const qtransport::TransportStatus status)
{
  LOGGER_DEBUG(server.logger,
               "connection_status: cid: " << context_id
                                          << " status: " << int(status));

  if (status == qtransport::TransportStatus::Disconnected) {
    server.logger->info << "Removing state for context_id: " << context_id
                        << std::flush;

    const auto lock = std::lock_guard<std::mutex>(server.session_mutex);

    auto pub_names_to_remove = std::vector<quicr::Namespace>{};
    for (auto& [ns, context] : server.publish_namespaces) {
      if (context.transport_context_id == context_id) {
        pub_names_to_remove.push_back(ns);
        server.delegate->onPublishIntentEnd(ns, {}, {});
      }
    }

    for (auto& ns : pub_names_to_remove) {
      server.publish_namespaces.erase(ns);
    }

    auto sub_names_to_remove = std::vector<quicr::Namespace>{};
    for (auto& [ns, sub_map] : server.subscribe_state) {
      auto sub_it = sub_map.find(context_id);
      if (sub_it != sub_map.end()) {
        server.delegate->onUnsubscribe(ns, sub_it->second.subscriber_id, {});
        server.subscribe_id_state.erase(sub_it->second.subscriber_id);
        sub_map.erase(sub_it);
      }

      if (sub_map.empty()) {
        sub_names_to_remove.push_back(ns);
      }
    }

    for (const auto& ns : sub_names_to_remove) {
      server.subscribe_state.erase(ns);
    }
  }
}

void
ServerRawSession::TransportDelegate::on_new_connection(
  const qtransport::TransportContextId& context_id,
  const qtransport::TransportRemote& remote)
{
  LOGGER_DEBUG(server.logger,
               "new_connection: cid: " << context_id
                                       << " remote: " << remote.host_or_ip
                                       << " port:" << ntohs(remote.port));
}

void
ServerRawSession::TransportDelegate::on_new_stream(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& streamId)
{
  LOGGER_DEBUG(server.logger,
               "new_stream: cid: " << context_id << " msid: " << streamId);
}

void
ServerRawSession::TransportDelegate::on_recv_notify(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& streamId)
{
  // don't starve other queues, read some number of messages at a time
  for (int i = 0; i < 150; i++) {
    auto data = server.transport->dequeue(context_id, streamId);

    if (data.has_value()) {
      server.recv_data_count++;
      try {
        auto msg_type = static_cast<messages::MessageType>(data->front());
        messages::MessageBuffer msg_buffer{ data.value() };

        switch (msg_type) {
          case messages::MessageType::Subscribe:
            server.recv_subscribes++;
            server.handle_subscribe(
              context_id, streamId, std::move(msg_buffer));
            break;
          case messages::MessageType::Publish:
            server.recv_publish++;
            server.handle_publish(context_id, streamId, std::move(msg_buffer));
            break;
          case messages::MessageType::Unsubscribe:
            server.recv_unsubscribes++;
            server.handle_unsubscribe(
              context_id, streamId, std::move(msg_buffer));
            break;
          case messages::MessageType::PublishIntent: {
            server.recv_pub_intents++;
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
