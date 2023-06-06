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

#include "helpers.h"
#include "quicr/encode.h"
#include "quicr/message_buffer.h"
#include "quicr/quicr_common.h"

#include <algorithm>
#include <arpa/inet.h>
#include <iostream>
#include <sstream>
#include <thread>

// TODO (trigaux): Remove these and make them more dynamic from the client.
namespace {
constexpr uint16_t GroupIDBitsOffset = 16u;
constexpr uint16_t ObjectIDBitsOffset = 0u;
constexpr uint16_t GroupIDBitLength = 32u;
constexpr uint16_t ObjectIDBitLength = 16u;
}

namespace {
/**
 * @brief Detects the stream mode based on name and stream id.
 *
 * Based on the name changing, checks whether the difference of the last name
 * and the new name is a change in the group id bits, or the object id bits, and
 * returns accordingly. However, if the bits have changed for that stream mode,
 * but the stream is the same, then we have to consider the possibilities:
 *  1. If the object is updating, but the stream is not, then the mode could be
 *     PerGroup
 *  2. If the group id is updating, but the stream is the same, then the mode
 *     could be PerPriority
 *
 * @param same_stream True if the stream ids are the same, false otherwise
 * @param last_name   The name prior to new_name.
 * @param new_name    The current name about to be used.
 * @return
 */
quicr::StreamMode
detect_stream_mode(bool same_stream,
                   quicr::Name last_name,
                   quicr::Name new_name)
{
  quicr::StreamMode interim_mode = quicr::StreamMode::Datagram;

  auto diff = new_name - last_name;
  if (diff < (0x1_name << GroupIDBitsOffset)) {
    interim_mode = same_stream ? quicr::StreamMode::PerPriority
                               : quicr::StreamMode::PerGroup;
  }

  if (diff < (0x1_name << ObjectIDBitsOffset)) {
    return same_stream ? interim_mode : quicr::StreamMode::PerObject;
  }

  return interim_mode;
}
}

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
  : logger(logger)
  , delegate(delegate_in)
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
  _context_id = transport->start();

  transport->createStream(_context_id, false);
  _control_stream_id = transport->createStream(_context_id, true);
}

QuicRServerRawSession::QuicRServerRawSession(
  std::shared_ptr<qtransport::ITransport> transport_in,
  ServerDelegate& delegate_in,
  qtransport::LogHandler& logger)
  : logger(logger)
  , delegate(delegate_in)
  , transport_delegate(*this)
  , transport(transport_in)
{
}

std::shared_ptr<qtransport::ITransport>
QuicRServerRawSession::setupTransport([[maybe_unused]] RelayInfo& relayInfo,
                                      qtransport::TransportConfig cfg)
{

  return qtransport::ITransport::make_server_transport(
    t_relay, cfg, transport_delegate, logger);
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
    logger.log(qtransport::LogLevel::info, "Waiting for server to be ready");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return transport->status() == qtransport::TransportStatus::Ready ? true
                                                                   : false;
}

void
QuicRServerRawSession::publishIntentResponse(
  const quicr::Namespace& quicr_namespace,
  const qtransport::TransportContextId& context_id,
  const PublishIntentResult& result)
{
  if (!publish_namespaces.count(quicr_namespace) ||
      !publish_namespaces[quicr_namespace].count(context_id))
    return;

  auto& context = publish_namespaces[quicr_namespace][context_id];
  messages::PublishIntentResponse response{
    messages::MessageType::PublishIntentResponse,
    quicr_namespace,
    result.status,
    messages::create_transaction_id()
  };

  messages::MessageBuffer msg(sizeof(response));
  msg << response;

  context.state = PublishContext::State::Ready;

  transport->enqueue(context_id, context.stream_id, msg.take());
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

  transport->enqueue(context.context_id, context.stream_id, msg.take());
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

  transport->enqueue(context.context_id, context.stream_id, msg.take());
}

void
QuicRServerRawSession::sendNamedObject(
  const uint64_t& subscriber_id,
  bool use_reliable_transport,
  uint8_t priority,
  uint16_t expiry_age_ms,
  bool new_stream,
  const messages::PublishDatagram& datagram)
{
  // start populating message to encode
  if (subscribe_id_state.count(subscriber_id) == 0) {
    logger.log(qtransport::LogLevel::info,
               "Send Object, missing subscriber_id: " +
                 std::to_string(subscriber_id));
    return;
  }

  auto& context = subscribe_id_state[subscriber_id];
  if (new_stream) {
    transport->closeStream(context.context_id, context.stream_id);
    context.stream_id =
      transport->createStream(context.context_id, use_reliable_transport);
  }

  messages::MessageBuffer msg(sizeof(datagram));
  msg << datagram;

  transport->enqueue(
    context.context_id, context.stream_id, msg.take(), priority, expiry_age_ms);
}

/*===========================================================================*/
// Private
/*===========================================================================*/

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
    SubscribeContext context;
    context.context_id = context_id;
    context.stream_id = streamId;
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
  const qtransport::StreamId& stream_id,
  bool use_reliable_transport,
  messages::MessageBuffer&& msg)
{
  messages::PublishDatagram datagram;
  msg >> datagram;

  if (publish_namespaces.count(datagram.header.name)) {
    // No such namespace, don't publish yet.
    return;
  }

  auto& [_, contexts] = *publish_namespaces.find(datagram.header.name);

  auto publish_namespace = contexts.find(context_id);
  if (publish_namespace == contexts.end()) {
    std::ostringstream log_msg;
    log_msg << "Dropping published object, no namespace for "
            << datagram.header.name;
    logger.log(qtransport::LogLevel::info, log_msg.str());
    return;
  }

  auto& [ns, context] = *publish_namespace;

  context.stream_id = stream_id;
  context.group_id =
    datagram.header.name.bits<uint32_t>(ObjectIDBitsOffset, GroupIDBitLength);
  context.object_id =
    datagram.header.name.bits<uint16_t>(ObjectIDBitsOffset, ObjectIDBitLength);

  if (context.group_id - context.prev_group_id > 1) {
    std::ostringstream log_msg;
    log_msg << "RX Group jump for ns: " << ns << " " << context.group_id
            << " - " << context.prev_group_id << " = "
            << context.group_id - context.prev_group_id - 1;
    logger.log(qtransport::LogLevel::info, log_msg.str());
  }

  if (context.group_id == context.prev_group_id &&
      context.object_id - context.prev_object_id > 1) {
    std::ostringstream log_msg;
    log_msg << "RX Object jump for ns: " << ns << " " << context.object_id
            << " - " << context.prev_object_id << " = "
            << context.object_id - context.prev_object_id - 1;
    logger.log(qtransport::LogLevel::info, log_msg.str());
  }

  context.prev_group_id = context.group_id;
  context.prev_object_id = context.object_id;

  // Check if this is the datagram stream, and react accordingly if not.
  bool new_stream = false;
  if (stream_id >= 4) {
    const bool same_stream = stream_id == context.stream_id;
    const auto stream_mode =
      ::detect_stream_mode(same_stream, context.name, datagram.header.name);

    switch (stream_mode) {
      case StreamMode::PerPriority:
        [[fallthrough]];
      case StreamMode::PerGroup:
        if (same_stream)
          break;
        [[fallthrough]];
      case StreamMode::PerObject:
        new_stream = true;
        break;
      default:
        break;
    }
  }

  delegate.onPublisherObject(
    context_id, use_reliable_transport, new_stream, std::move(datagram));
}

void
QuicRServerRawSession::handle_publish_intent(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& stream_id,
  bool use_reliable_transport,
  messages::MessageBuffer&& msg)
{
  messages::PublishIntent intent;
  msg >> intent;

  if (!publish_namespaces.count(intent.quicr_namespace) ||
      !publish_namespaces.at(intent.quicr_namespace).count(context_id)) {
    PublishContext context;
    context.state = PublishContext::State::Pending;
    context.context_id = context_id;
    context.stream_id = stream_id;

    publish_namespaces[intent.quicr_namespace][context_id] = context;
  } else {
    auto state = publish_namespaces[intent.quicr_namespace][context_id].state;
    switch (state) {
      case PublishContext::State::Pending:
        // TODO: Resend response?
        break;
      case PublishContext::State::Ready:
        // TODO: Already registered this namespace successfully, do nothing?
        break;
      default:
        break;
    }
  }

  delegate.onPublishIntent(intent.quicr_namespace,
                           context_id,
                           "" /* intent.origin_url */,
                           use_reliable_transport,
                           "" /* intent.relay_token */,
                           std::move(intent.payload));
}

void
QuicRServerRawSession::handle_publish_intent_end(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& stream_id,
  messages::MessageBuffer&& msg)
{
  messages::PublishIntentEnd intent_end;
  msg >> intent_end;

  if (!publish_namespaces.count(intent_end.quicr_namespace) ||
      !publish_namespaces.at(intent_end.quicr_namespace).count(context_id)) {
    return;
  }

  if (publish_namespaces.at(intent_end.quicr_namespace).size() > 1) {
    publish_namespaces.at(intent_end.quicr_namespace).erase(context_id);
  } else {
    publish_namespaces.erase(intent_end.quicr_namespace);
  }

  transport->closeStream(context_id, stream_id);

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
  server.logger.log(qtransport::LogLevel::debug, log_msg.str());

  if (status == qtransport::TransportStatus::Disconnected) {
    log_msg.str("");
    log_msg << "Removing state for context_id: " << context_id;
    server.logger.log(qtransport::LogLevel::info, log_msg.str());

    std::lock_guard<std::mutex> lock(server.session_mutex);

    std::vector<quicr::Namespace> pub_names_to_remove;
    for (auto & [ns, context]: server.publish_namespaces) {
      if (context.transport_context_id == context_id) {
        pub_names_to_remove.push_back(ns);
        server.delegate.onPublishIntentEnd(ns, {}, {});
      }
    }

    for (auto &ns: pub_names_to_remove) {
      server.publish_namespaces.erase(ns);
    }

    std::vector<quicr::Namespace> sub_names_to_remove;
    for (auto& sub : server.subscribe_state) {
      if (sub.second.count(context_id) != 0) {

        const auto& stream_id = sub.second[context_id].subscriber_id;

        // Before removing, exec callback
        server.delegate.onUnsubscribe(sub.first, stream_id, {});

        server.subscribe_id_state.erase(stream_id);
        sub.second.erase(context_id);

        if (sub.second.empty()) {
          sub_names_to_remove.push_back(sub.first);
        }

        break;
      }

      for (const auto& ns : sub_names_to_remove) {
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
  server.logger.log(qtransport::LogLevel::debug, log_msg.str());
}

void
QuicRServerRawSession::TransportDelegate::on_new_stream(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& streamId)
{
  std::stringstream log_msg;
  log_msg << "new_stream: cid: " << context_id << " msid: " << streamId;

  server.logger.log(qtransport::LogLevel::debug, log_msg.str());
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
            server.handle_publish(
              context_id, streamId, false, std::move(msg_buffer));
            break;
          case messages::MessageType::Unsubscribe:
            server.recv_unsubscribes++;
            server.handle_unsubscribe(
              context_id, streamId, std::move(msg_buffer));
            break;
          case messages::MessageType::PublishIntent: {
            server.recv_pub_intents++;
            server.handle_publish_intent(
              context_id, streamId, false, std::move(msg_buffer));
            break;
          }
          case messages::MessageType::PublishIntentEnd: {
            server.handle_publish_intent_end(
              context_id, streamId, std::move(msg_buffer));
            break;
          }
          default:
            server.logger.log(qtransport::LogLevel::info,
                              "Invalid Message Type");
            break;
        }
      } catch (const messages::MessageBuffer::ReadException& /* ex */) {
        server.logger.log(
          qtransport::LogLevel::fatal,
          "Received read exception error while reading from message buffer.");
        continue;

      } catch (const std::exception& /* ex */) {
        server.logger.log(qtransport::LogLevel::fatal,
                          "Received standard exception error while "
                          "reading from message buffer.");
        continue;
      } catch (...) {
        server.logger.log(
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
