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

#define LOGGER_TRACE(logger, ...) if (logger) SPDLOG_LOGGER_TRACE(logger, __VA_ARGS__)
#define LOGGER_DEBUG(logger, ...) if (logger) SPDLOG_LOGGER_DEBUG(logger, __VA_ARGS__)
#define LOGGER_INFO(logger, ...) if (logger) SPDLOG_LOGGER_INFO(logger, __VA_ARGS__)
#define LOGGER_WARN(logger, ...) if (logger) SPDLOG_LOGGER_WARN(logger, __VA_ARGS__)
#define LOGGER_ERROR(logger, ...) if (logger) SPDLOG_LOGGER_ERROR(logger, __VA_ARGS__)
#define LOGGER_CRITICAL(logger, ...) if (logger) SPDLOG_LOGGER_CRITICAL(logger, __VA_ARGS__)

namespace quicr {
/*
 * Initialize the QUICR server session at the port specified.
 *  @param delegate_in: Callback handlers for QUICR operations
 */
ServerRawSession::ServerRawSession(const RelayInfo& relayInfo,
                                   const qtransport::TransportConfig& tconfig,
                                   std::shared_ptr<ServerDelegate> delegate_in,
                                   std::shared_ptr<spdlog::logger> logger)
  : delegate(std::move(delegate_in))
  , logger(std::move(logger))
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

  relay_id = relayInfo.relay_id;
  transport = setupTransport(tconfig);
  transport->start(_mexport.metrics_conn_samples, _mexport.metrics_data_samples);
}

ServerRawSession::ServerRawSession(
  std::shared_ptr<qtransport::ITransport> transport_in,
  std::shared_ptr<ServerDelegate> delegate_in)
  : delegate(std::move(delegate_in))
  , transport_delegate(*this)
  , transport(std::move(transport_in))
{
}

std::shared_ptr<qtransport::ITransport>
ServerRawSession::setupTransport(const qtransport::TransportConfig& cfg)
{
  return qtransport::ITransport::make_server_transport(t_relay, cfg, transport_delegate, logger);
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
    LOGGER_INFO(logger, "Waiting for server to be ready");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }


#ifndef LIBQUICR_WITHOUT_INFLUXDB
  if (_mexport.init("http://metrics.m10x.ctgpoc.com:8086",
                   "Media10x",
                   "cisco-cto-media10x") !=
      MetricsExporter::MetricsExporterError::NoError) {
    throw std::runtime_error("Failed to connect to InfluxDB");
      }

  if (!transport->metrics_conn_samples) {
    LOGGER_ERROR(logger, "ERROR metrics conn samples null");
  }
  _mexport.run();
#endif

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
    context.transaction_id,
    context.data_ctx_id
  };

  messages::MessageBuffer msg(sizeof(response));
  msg << response;

  context.state = PublishIntentContext::State::Ready;

  const auto& conn_ctx = _connections[context.transport_conn_id];

  LOGGER_INFO(logger,
              "Sending publish intent response ns: {0} conn_id: {1} data_ctx_id: {2}",
              std::string(quicr_namespace),
              context.transport_conn_id,
              context.data_ctx_id);
  enqueue_ctrl_msg(context.transport_conn_id, conn_ctx.ctrl_data_ctx_id, msg.take());
}

void
ServerRawSession::subscribeResponse(const uint64_t& subscriber_id,
                                    const quicr::Namespace& quicr_namespace,
                                    const SubscribeResult& result)
{
  // start populating message to encode
  if (subscribe_id_state.find(subscriber_id) == subscribe_id_state.end()) {
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

  enqueue_ctrl_msg(context->transport_conn_id, conn_ctx.ctrl_data_ctx_id, msg.take());
}

void
ServerRawSession::subscriptionEnded(
  const uint64_t& subscriber_id,
  const quicr::Namespace& quicr_namespace,
  const SubscribeResult::SubscribeStatus& reason)
{
  // start populating message to encode
  if (subscribe_id_state.find(subscriber_id) == subscribe_id_state.end()) {
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
  enqueue_ctrl_msg(context->transport_conn_id, conn_ctx.ctrl_data_ctx_id, msg.take());
}

void
ServerRawSession::sendNamedObject(const uint64_t& subscriber_id,
                                  uint8_t priority,
                                  uint16_t expiry_age_ms,
                                  const messages::PublishDatagram& datagram)
{
  // start populating message to encode
  if (subscribe_id_state.find(subscriber_id) == subscribe_id_state.end()) {
    LOGGER_INFO(logger, "Send Object, missing subscriber_id: {0}", subscriber_id);
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

    context->priority = priority;
    context->pending_reliable_data_ctx = false;
    context->data_ctx_id = transport->createDataContext(context->transport_conn_id, true, priority, false);
    transport->setRemoteDataCtxId(context->transport_conn_id, context->data_ctx_id, context->remote_data_ctx_id);

#ifndef LIBQUICR_WITHOUT_INFLUXDB
    _mexport.set_data_ctx_info(context->transport_conn_id, context->data_ctx_id, {.subscribe = true, .nspace = context->nspace});
#endif

    LOGGER_INFO(
      logger,
      "Creating new data context for subscriber_id: {0} conn_id: {1} remote_data_ctx_id: {2} new data_ctx_id: {3}",
      subscriber_id,
      context->transport_conn_id,
      context->remote_data_ctx_id,
      context->data_ctx_id);
  }

  if (context->priority != priority) {
    context->priority = priority;
  }

  qtransport::ITransport::EnqueueFlags eflags;

  switch (context->transport_mode) {
    case TransportMode::ReliablePerGroup: {
      uint64_t group_id = datagram.header.name.bits<uint64_t>(16, 32);

      eflags.use_reliable = true;

      if (context->group_id && context->group_id != group_id) {
        eflags.new_stream = true;
        eflags.use_reset = true;
        eflags.clear_tx_queue = true;
      }

      context->group_id = group_id;
      break;
    }

    case TransportMode::ReliablePerObject: {
      eflags.use_reliable = true;
      eflags.new_stream = true;
      break;
    }

    case TransportMode::ReliablePerTrack:
      eflags.use_reliable = true;
      break;

    case TransportMode::Unreliable:
      break;

    default:
      break;
  }

  transport->enqueue(context->transport_conn_id,
                     context->data_ctx_id,
                     msg.take(),
                     { qtransport::MethodTraceItem{} },
                     priority,
                     expiry_age_ms,
                     0,
                     eflags);
}

///
/// Private
///

void ServerRawSession::handle(TransportConnId conn_id,
                              std::optional<uint64_t> stream_id,
                              std::optional<qtransport::DataContextId> data_ctx_id,
                              messages::MessageBuffer&& msg,
                              bool is_bidir)
{
  auto chdr = msg.front(5); // msg_len + type
  auto msg_type = static_cast<messages::MessageType>(chdr.back());

  switch (msg_type) {

    case messages::MessageType::Connect: {
      // TODO(tievens): Enforce that connect is the first message
      if (is_bidir) {
        auto& conn_ctx = _connections[conn_id];
        conn_ctx.ctrl_data_ctx_id = data_ctx_id ? *data_ctx_id : 0;
#ifndef LIBQUICR_WITHOUT_INFLUXDB
        _mexport.set_data_ctx_info(conn_id, conn_ctx.ctrl_data_ctx_id,
                                   {.subscribe = false, .nspace = {}});
#endif
      }

      handle_connect(conn_id, std::move(msg));
      break;
    }
    case messages::MessageType::Subscribe: {
      if (is_bidir) {
        auto& conn_ctx = _connections[conn_id];
        conn_ctx.ctrl_data_ctx_id = data_ctx_id ? *data_ctx_id : 0;
#ifndef LIBQUICR_WITHOUT_INFLUXDB
        _mexport.set_data_ctx_info(conn_id, conn_ctx.ctrl_data_ctx_id,
                                   {.subscribe = false, .nspace = {}});
#endif
      }

      recv_subscribes++;
      handle_subscribe(
        conn_id, data_ctx_id ? *data_ctx_id : 0, std::move(msg));
      break;
    }
    case messages::MessageType::Publish: {
      recv_publish++;
      handle_publish(conn_id, stream_id, data_ctx_id, std::move(msg));
      break;
    }
    case messages::MessageType::Unsubscribe: {
      recv_unsubscribes++;
      handle_unsubscribe(
        conn_id, data_ctx_id ? *data_ctx_id : 0, std::move(msg));
      break;
    }
    case messages::MessageType::PublishIntent: {
      if (is_bidir) {
        auto& conn_ctx = _connections[conn_id];
        conn_ctx.ctrl_data_ctx_id = data_ctx_id ? *data_ctx_id : 0;
#ifndef LIBQUICR_WITHOUT_INFLUXDB
        _mexport.set_data_ctx_info(conn_id, conn_ctx.ctrl_data_ctx_id,
                                   {.subscribe = false, .nspace = {}});
#endif
      }
      recv_pub_intents++;
      handle_publish_intent(
        conn_id, data_ctx_id ? *data_ctx_id : 0, std::move(msg));
      break;
    }
    case messages::MessageType::PublishIntentEnd: {
      handle_publish_intent_end(
        conn_id, data_ctx_id ? *data_ctx_id : 0, std::move(msg));
      break;
    }
    default:
      LOGGER_INFO(logger, "Invalid Message Type {0}", static_cast<int>(msg_type));
      break;
  }
}

void
ServerRawSession::handle_connect(
  const qtransport::TransportConnId& conn_id,
  messages::MessageBuffer&& msg)
{
  auto connect = messages::Connect{};
  msg >> connect;

  std::lock_guard<std::mutex> _(session_mutex);

  const auto conn_it = _connections.find(conn_id);
  if (conn_it != _connections.end()) {
    conn_it->second.endpoint_id = connect.endpoint_id;
    LOGGER_INFO(logger, "conn_id: {0} Connect from endpoint_id: {1}", conn_id, connect.endpoint_id);
  }

  const auto response = messages::ConnectResponse{
    .relay_id = relay_id,
  };

  messages::MessageBuffer r_msg;
  r_msg << response;

#ifndef LIBQUICR_WITHOUT_INFLUXDB
  _mexport.set_conn_ctx_info(conn_id, {.endpoint_id = connect.endpoint_id,
                                              .relay_id = relay_id,
                                              .data_ctx_info = {}}, false);
#endif

  enqueue_ctrl_msg(conn_id, conn_it->second.ctrl_data_ctx_id, r_msg.take());
}


void
ServerRawSession::handle_subscribe(
  const qtransport::TransportConnId& conn_id,
  const qtransport::DataContextId& data_ctx_id,
  messages::MessageBuffer&& msg)
{
  auto subscribe = messages::Subscribe{};
  msg >> subscribe;

  std::lock_guard<std::mutex> _(session_mutex);

  // Is this a new or existing subscriber?
  const auto& [it, newSubscription] = _subscribe_state[subscribe.quicr_namespace].try_emplace(conn_id, std::make_shared<SubscribeContext>());
  const auto& context = it->second;

  if (!newSubscription) {
      if (subscribe.transport_mode == TransportMode::Pause || subscribe.transport_mode == TransportMode::Resume) {
          context->paused = subscribe.transport_mode == TransportMode::Pause ? true : false;
          delegate->onSubscribePause(subscribe.quicr_namespace, context->subscriber_id, conn_id, data_ctx_id,
                                     context->paused);
      } else {
          LOGGER_WARN(logger, "Existing subscription is not allowed to be modified subscriber_id: {0}", context->subscriber_id);
      }

      return;
  }

  // New subscription
  context->transport_conn_id = conn_id;
  context->subscriber_id = ++_subscriber_id;
  context->transport_mode = subscribe.transport_mode;
  context->remote_data_ctx_id = subscribe.remote_data_ctx_id;
  context->nspace = subscribe.quicr_namespace;

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
      transport->setRemoteDataCtxId(conn_id, context->data_ctx_id, context->remote_data_ctx_id);
#ifndef LIBQUICR_WITHOUT_INFLUXDB
      _mexport.set_data_ctx_info(conn_id, context->data_ctx_id, {.subscribe = true, .nspace = subscribe.quicr_namespace});
#endif
      break;
    }

    case TransportMode::UsePublisher: {
      context->transport_mode_follow_publisher = true;
      context->data_ctx_id = transport->createDataContext(conn_id, true, context->priority, false);
      transport->setRemoteDataCtxId(conn_id, context->data_ctx_id, context->remote_data_ctx_id);

#ifndef LIBQUICR_WITHOUT_INFLUXDB
      _mexport.set_data_ctx_info(conn_id, context->data_ctx_id, {.subscribe = true, .nspace = subscribe.quicr_namespace});
#endif
      break;
    }

    case TransportMode::Pause:
      [[fallthrough]];
    case TransportMode::Resume:
      // Handled previously
      return;
  }

  LOGGER_DEBUG(logger,
               "New Subscribe conn_id: {0} ns: {1} transport_mode: {2} subscriber_id: {3} pending_data_ctx: {4}",
               conn_id,
               subscribe.quicr_namespace,
               static_cast<int>(context->transport_mode),
               context->subscriber_id,
               context->pending_reliable_data_ctx);

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
  [[maybe_unused]] const qtransport::DataContextId& data_ctx_id,
  messages::MessageBuffer&& msg)
{
  auto unsub = messages::Unsubscribe{};
  msg >> unsub;

  // Remove states if state exists
  auto& subscribe_state = _subscribe_state[unsub.quicr_namespace];
  if (subscribe_state.find(conn_id) != subscribe_state.end()) {
    const auto lock = std::lock_guard<std::mutex>(session_mutex);

    auto& context = subscribe_state[conn_id];

#ifndef LIBQUICR_WITHOUT_INFLUXDB
    _mexport.del_data_ctx_info(conn_id, context->data_ctx_id);
#endif

    // Before removing, exec callback
    delegate->onUnsubscribe(unsub.quicr_namespace, context->subscriber_id, {});

    transport->deleteDataContext(context->transport_conn_id, context->data_ctx_id);

    subscribe_id_state.erase(context->subscriber_id);
    _subscribe_state[unsub.quicr_namespace].erase(conn_id);

    if (_subscribe_state[unsub.quicr_namespace].empty()) {
      _subscribe_state.erase(unsub.quicr_namespace);
    }
  }
}

void
ServerRawSession::handle_publish(qtransport::TransportConnId conn_id,
                                 std::optional<uint64_t> stream_id,
                                 std::optional<qtransport::DataContextId> data_ctx_id,
                                 messages::MessageBuffer&& msg)
{
  messages::PublishDatagram datagram;
  msg >> datagram;

  auto publish_namespace = publish_namespaces.find(datagram.header.name);

  if (publish_namespace == publish_namespaces.end()) {
    // TODO(trigaux): Add metrics for tracking dropped messages
    LOGGER_INFO(logger, "Dropping published object, no namespace for {0}", std::string(datagram.header.name));
    return;
  }

  auto& [ns, context] = *publish_namespace;

  const auto gap_log = gap_check(
    false, datagram.header.name, context.prev_group_id, context.prev_object_id);

  if (!gap_log.empty()) {
    LOGGER_INFO(logger, "conn_id: {0} data_ctx_id: {1} {2}", conn_id, (data_ctx_id ? *data_ctx_id : 0), gap_log);
  }

  if (!data_ctx_id && stream_id) {
    data_ctx_id = context.data_ctx_id;
    transport->setStreamIdDataCtxId(conn_id, *data_ctx_id, *stream_id);

#ifndef LIBQUICR_WITHOUT_INFLUXDB
    _mexport.set_data_ctx_info(conn_id, context.data_ctx_id, {.subscribe = false, .nspace = ns});
#endif
  }

  // NOTE: if stream_id is nullopt, it means it's not reliable and MUST be datagram
  delegate->onPublisherObject(conn_id, *data_ctx_id, stream_id.has_value(), std::move(datagram));
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
  ps_it->second.data_ctx_id = transport->createDataContext(conn_id, false,
                                                           2 /* TODO: Receive priority */, false);


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

#ifndef LIBQUICR_WITHOUT_INFLUXDB
  _mexport.set_data_ctx_info(conn_id, ps_it->second.data_ctx_id, {.subscribe = false, .nspace = intent.quicr_namespace});
#endif

  delegate->onPublishIntent(intent.quicr_namespace,
                            "" /* intent.origin_url */,
                            "" /* intent.relay_token */,
                            std::move(intent.payload));
}

void
ServerRawSession::handle_publish_intent_end(
  const qtransport::TransportConnId& conn_id,
  [[maybe_unused]] const qtransport::DataContextId& data_ctx_id,
  messages::MessageBuffer&& msg)
{
  messages::PublishIntentEnd intent_end;
  msg >> intent_end;

  const auto& ns = intent_end.quicr_namespace;

  const auto pub_it = publish_namespaces.find(intent_end.quicr_namespace);

  if (pub_it == publish_namespaces.end()) {
    return;
  }

  transport->deleteDataContext(conn_id, pub_it->second.data_ctx_id);

#ifndef LIBQUICR_WITHOUT_INFLUXDB
  _mexport.del_data_ctx_info(conn_id, pub_it->second.data_ctx_id);
#endif


  publish_namespaces.erase(pub_it);

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
  LOGGER_DEBUG(server.logger, "connection_status: conn_id: {0} status: {1}", conn_id,  int(status));

  if (status == qtransport::TransportStatus::Disconnected) {
    LOGGER_INFO(server.logger, "Removing state for conn_id: {0}", conn_id);

#ifndef LIBQUICR_WITHOUT_INFLUXDB
    server._mexport.del_conn_ctx_info(conn_id);
#endif

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
  LOGGER_DEBUG(server.logger, "new_connection: conn_id: {0} remote: {1} port: {2}", conn_id, remote.host_or_ip, ntohs(remote.port));

  auto& conn_ctx = server._connections[conn_id];
  conn_ctx.conn_id = conn_id;
  conn_ctx.remote = remote;
}

void ServerRawSession::TransportDelegate::on_new_data_context([[maybe_unused]] const qtransport::TransportConnId &conn_id,
                                                              [[maybe_unused]] const qtransport::DataContextId &data_ctx_id)
{
  LOGGER_DEBUG(server.logger, "New BiDir data context conn_id: {0} data_ctx_id: {1}", conn_id, data_ctx_id);
}

void
ServerRawSession::TransportDelegate::on_recv_stream(const TransportConnId& conn_id,
                                                    uint64_t stream_id,
                                                    std::optional<DataContextId> data_ctx_id,
                                                    const bool is_bidir)
{
  auto stream_buf = server.transport->getStreamBuffer(conn_id, stream_id);

  if (stream_buf == nullptr) {
    return;
  }

  while (true) {
    if (stream_buf->available(4)) {
      auto msg_len_b = stream_buf->front(4);

      if (!msg_len_b.size())
        return;

      auto* msg_len = reinterpret_cast<uint32_t*>(msg_len_b.data());

      if (stream_buf->available(*msg_len)) {
        auto obj = stream_buf->front(*msg_len);
        stream_buf->pop(*msg_len);

        try {
          messages::MessageBuffer msg_buffer{ obj };

          server.handle(conn_id, stream_id, data_ctx_id, std::move(msg_buffer), is_bidir);
        } catch (const messages::MessageBuffer::ReadException& ex) {
          // TODO: When reliable, we really should reset the stream if this happens (at least more than once)
          LOGGER_CRITICAL(server.logger, "Received read exception error while reading from message buffer: {0}", ex.what());
          return;
        } catch (const std::exception& ex) {
          LOGGER_CRITICAL(server.logger, "Received standard exception error while reading from message buffer: {0}", ex.what());
          return;
        } catch (...) {
          LOGGER_CRITICAL(server.logger, "Received unknown error while reading from message buffer");
          return;
        }
      } else {
        break;
      }
    } else {
      break;
    }
  }
}


void
ServerRawSession::TransportDelegate::on_recv_dgram(const TransportConnId& conn_id,
                                                   std::optional<DataContextId> data_ctx_id)
{
  // don't starve other queues, read some number of messages at a time
  for (int i = 0; i < 100; i++) {
    auto data = server.transport->dequeue(conn_id, data_ctx_id);

    if (data && data.value().size() > 0) {
      server.recv_data_count++;
      try {
        messages::MessageBuffer msg_buffer{ data.value() };
        server.handle(conn_id, std::nullopt, data_ctx_id, std::move(msg_buffer));

      } catch (const messages::MessageBuffer::ReadException& ex) {
        // TODO: When reliable, we really should reset the stream if this happens (at least more than once)
        LOGGER_CRITICAL(server.logger, "Received read exception error while reading from message buffer: {0}",  ex.what());
        continue;
      } catch (const std::exception& ex) {
        LOGGER_CRITICAL(server.logger, "Received standard exception error while reading from message buffer: {0}", ex.what());
        continue;
      } catch (...) {
        LOGGER_CRITICAL(server.logger, "Received unknown error while reading from message buffer");
        continue;
      }
    } else {
      break;
    }
  }
}

} // namespace quicr
