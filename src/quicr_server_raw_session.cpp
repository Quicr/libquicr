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

#include <quicr/quicr_common.h>

#include <algorithm>
#include <iostream>
#include <quicr/encode.h>
#include <quicr/message_buffer.h>
#include <sstream>
#include <thread>

#include <arpa/inet.h>

namespace quicr {
/*
 * Initialize the QUICR server session at the port specified.
 *  @param delegate_in: Callback handlers for QUICR operations
 */
QuicRServerRawSession::QuicRServerRawSession(
  RelayInfo& relayInfo,
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

QuicRServerRawSession::QuicRServerRawSession(
  std::shared_ptr<qtransport::ITransport> transport_in,
  ServerDelegate& delegate_in,
  qtransport::LogHandler& logger)
  : delegate(delegate_in)
  , log_handler(logger)
  , transport_delegate(*this)
  , transport(transport_in)
{
}

std::shared_ptr<qtransport::ITransport>
QuicRServerRawSession::setupTransport([[maybe_unused]] RelayInfo& relayInfo,
                                      qtransport::TransportConfig cfg)
{

  return qtransport::ITransport::make_server_transport(
    t_relay, cfg, transport_delegate, log_handler);
}

// Transport APIs
bool
QuicRServerRawSession::is_transport_ready()
{
  if (transport->status() == qtransport::TransportStatus::Ready)
    return true;
  else
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
QuicRServerRawSession::run()
{
  running = true;

  while (transport->status() == qtransport::TransportStatus::Connecting) {
    log_handler.log(qtransport::LogLevel::info,
                    "Waiting for server to be ready");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return transport->status() == qtransport::TransportStatus::Ready ? true
                                                                   : false;
}

void
QuicRServerRawSession::publishIntentResponse(
  const quicr::Namespace& quicr_namespace,
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
QuicRServerRawSession::subscribeResponse(
  const uint64_t& subscriber_id,
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
QuicRServerRawSession::subscriptionEnded(
  const uint64_t& subscriber_id,
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
QuicRServerRawSession::sendNamedObject(
  const uint64_t& subscriber_id,
  [[maybe_unused]] bool use_reliable_transport,
  uint8_t priority,
  uint16_t expiry_age_ms,
  const messages::PublishDatagram& datagram)
{
  // start populating message to encode
  if (subscribe_id_state.count(subscriber_id) == 0) {
    log_handler.log(qtransport::LogLevel::info, "Send Object, missing subscriber_id: " + std::to_string(subscriber_id));
    return;
  }

  auto& context = subscribe_id_state[subscriber_id];
  messages::MessageBuffer msg;

  msg << datagram;

  transport->enqueue(
    context.transport_context_id, context.transport_stream_id,
    msg.get(), priority, expiry_age_ms);
}

///
/// Private
///

void
QuicRServerRawSession::handle_subscribe(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& streamId,
  messages::MessageBuffer&& msg)
{
  messages::Subscribe subscribe;
  msg >> subscribe;

  std::lock_guard<std::mutex> lock(session_mutex);

  if (subscribe_state[subscribe.quicr_namespace].count(context_id) == 0) {
    log_handler.log(qtransport::LogLevel::info, "New subscriber_id: " + std::to_string(subscriber_id));
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
QuicRServerRawSession::handle_unsubscribe(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& /* streamId */,
  messages::MessageBuffer&& msg)
{
  messages::Unsubscribe unsub;
  msg >> unsub;

  // Remove states if state exists
  if (subscribe_state[unsub.quicr_namespace].count(context_id) != 0) {

    std::lock_guard<std::mutex> lock(session_mutex);

    auto& context = subscribe_state[unsub.quicr_namespace][context_id];

    // Before removing, exec callback
    delegate.onUnsubscribe(unsub.quicr_namespace, context.subscriber_id, {});

    subscribe_id_state.erase(context.subscriber_id);
    subscribe_state[unsub.quicr_namespace].erase(context_id);

    if (subscribe_state[unsub.quicr_namespace].empty()) {
      subscribe_state.erase(unsub.quicr_namespace);
    }
  }
}

void
QuicRServerRawSession::handle_publish(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& streamId,
  messages::MessageBuffer&& msg)
{
  messages::PublishDatagram datagram;
  msg >> datagram;

  auto publish_namespace = publish_namespaces.find(datagram.header.name);

  if (publish_namespace == publish_namespaces.end()) {
    std::ostringstream log_msg;
    log_msg << "Dropping published object, no namespace for "
            << datagram.header.name;
    log_handler.log(qtransport::LogLevel::info, log_msg.str());
    return;
  }


  uint64_t n_low64 = datagram.header.name.low64();
  auto& [ns, context ] = *publish_namespace;

  context.group_id = (n_low64 & 0xFFFFFFFF0000) >> 16;
  context.object_id = n_low64 & 0xFFFF;

  if (context.group_id - context.prev_group_id > 1) {
    std::ostringstream log_msg;
    log_msg << "RX Group jump for ns: "
            << ns << " "
            << context.group_id << " - " << context.prev_group_id
            << " = " << context.group_id - context.prev_group_id - 1;
    log_handler.log(qtransport::LogLevel::info, log_msg.str());
  }

  if (context.group_id == context.prev_group_id && context.object_id - context.prev_object_id > 1) {
    std::ostringstream log_msg;
    log_msg << "RX Object jump for ns: "
            << ns << " "
            << context.object_id << " - " << context.prev_object_id
            << " = " << context.object_id - context.prev_object_id - 1;
    log_handler.log(qtransport::LogLevel::info, log_msg.str());
  }

  context.prev_group_id = context.group_id;
  context.prev_object_id = context.object_id;

  delegate.onPublisherObject(context_id,
                             streamId,
                             false,
                             std::move(datagram));
}

void
QuicRServerRawSession::handle_publish_intent(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& streamId,
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
QuicRServerRawSession::handle_publish_intent_end(
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

  delegate.onPublishIntentEnd(intent_end.quicr_namespace,
                              "" /* intent_end.relay_token */,
                              std::move(intent_end.payload));
}

/*===========================================================================*/
// Transport Delegate Implementation
/*===========================================================================*/

QuicRServerRawSession::TransportDelegate::TransportDelegate(
  quicr::QuicRServerRawSession& server)
  : server(server)
{
}

void
QuicRServerRawSession::TransportDelegate::on_connection_status(
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

    std::lock_guard<std::mutex> lock(server.session_mutex);

    std::vector<quicr::Namespace> namespaces_to_remove;
    for (auto& sub : server.subscribe_state) {
      if (sub.second.count(context_id) != 0) {

        const auto& stream_id = sub.second[context_id].subscriber_id;

        // Before removing, exec callback
        server.delegate.onUnsubscribe(sub.first, stream_id, {});

        server.subscribe_id_state.erase(stream_id);
        sub.second.erase(context_id);

        if (sub.second.empty()) {
          namespaces_to_remove.push_back(sub.first);
        }

        break;

      }

      for (const auto& ns : namespaces_to_remove) {
        server.subscribe_state.erase(ns);
      }
    }
  }
}

void
QuicRServerRawSession::TransportDelegate::on_new_connection(
  const qtransport::TransportContextId& context_id,
  const qtransport::TransportRemote& remote)
{
  std::stringstream log_msg;
  log_msg << "new_connection: cid: " << context_id
          << " remote: " << remote.host_or_ip << " port:" << ntohs(remote.port);
  server.log_handler.log(qtransport::LogLevel::debug, log_msg.str());
}

void
QuicRServerRawSession::TransportDelegate::on_new_stream(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& streamId)
{
  std::stringstream log_msg;
  log_msg << "new_stream: cid: " << context_id << " msid: " << streamId;

  server.log_handler.log(qtransport::LogLevel::debug, log_msg.str());
}

void
QuicRServerRawSession::TransportDelegate::on_recv_notify(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& streamId)
{


  // don't starve other queues, read some number of messages at a time
  for (int i = 0; i < 150; i++) {
    auto data = server.transport->dequeue(context_id, streamId);

    if (data.has_value()) {
      server.recv_data_count++;
      try {
        // TODO: Extracting type will change when the message is encoded
        // correctly
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
            server.log_handler.log(qtransport::LogLevel::info, "Invalid Message Type");
            break;
        }
      } catch (const messages::MessageBuffer::ReadException& /* ex */) {
        server.log_handler.log(
          qtransport::LogLevel::fatal,
          "Received read exception error while reading from message buffer.");
        continue;

      } catch (const std::exception& /* ex */) {
        server.log_handler.log(
          qtransport::LogLevel::fatal,
          "Received standard exception error while reading from message buffer.");
        continue;
      } catch (...) {
        server.log_handler.log(
          qtransport::LogLevel::fatal,
          "Received unknown error while reading from message buffer.");
        continue;
      }
    } else {
      break;
    }
  }
}

} /* namespace end */
