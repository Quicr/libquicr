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

#include <transport/transport_metrics.h>

#include <algorithm>
#include <arpa/inet.h>
#include <limits>
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
                                   const cantina::LoggerPointer& logger,
                                   std::optional<Namespace> metrics_ns)
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

  relay_id = relayInfo.relay_id;
  transport = setupTransport(tconfig);

  metrics_conn_samples = std::make_shared<qtransport::safe_queue<qtransport::MetricsConnSample>>(qtransport::MAX_METRICS_SAMPLES_QUEUE);
  metrics_data_samples = std::make_shared<qtransport::safe_queue<qtransport::MetricsDataSample>>(qtransport::MAX_METRICS_SAMPLES_QUEUE);

  if (metrics_ns)
  {
    metrics_namespace = *metrics_ns;
  }

  transport->start(metrics_conn_samples, metrics_data_samples);
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

  if (metrics_namespace)
  {
    runPublishMeasurements();
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
    context.transaction_id,
    context.data_ctx_id
  };

  messages::MessageBuffer msg(sizeof(response));
  msg << response;

  context.state = PublishIntentContext::State::Ready;

  const auto& conn_ctx = _connections[context.transport_conn_id];

  logger->info << "Sending publish intent response ns: " << quicr_namespace
               << " conn_id: " << context.transport_conn_id
               << " data_ctx_id: " << context.data_ctx_id
               << std::flush;
  enqueue_ctrl_msg(context.transport_conn_id, conn_ctx.ctrl_data_ctx_id, msg.take());
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

  enqueue_ctrl_msg(context->transport_conn_id, conn_ctx.ctrl_data_ctx_id, msg.take());
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
  enqueue_ctrl_msg(context->transport_conn_id, conn_ctx.ctrl_data_ctx_id, msg.take());
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

    context->priority = priority;
    context->pending_reliable_data_ctx = false;
    context->data_ctx_id = transport->createDataContext(context->transport_conn_id, true, priority, false);
    transport->setRemoteDataCtxId(context->transport_conn_id, context->data_ctx_id, context->remote_data_ctx_id);

    addDataMeasurement(context->transport_conn_id, context->data_ctx_id, "subscribe");

    logger->info << "Creating new data context for subscriber_id: " << subscriber_id
                 << " conn_id: " << context->transport_conn_id
                 << " remote_data_ctx_id: " << context->remote_data_ctx_id
                 << " new data_ctx_id: " << context->data_ctx_id
                 << std::flush;
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

void ServerRawSession::handle(qtransport::TransportConnId conn_id,
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
      }

      handle_connect(conn_id, std::move(msg));
      break;
    }
    case messages::MessageType::Subscribe: {
      if (is_bidir) {
        auto& conn_ctx = _connections[conn_id];
        conn_ctx.ctrl_data_ctx_id = data_ctx_id ? *data_ctx_id : 0;
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
      logger->info << "Invalid Message Type "
                   << static_cast<int>(msg_type)
                   << std::flush;
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
    logger->info << "conn_id: " << conn_id
                 << " Connect from endpoint_id: " << connect.endpoint_id
                 << std::flush;
  }

  const auto response = messages::ConnectResponse{
    .relay_id = relay_id,
  };

  addConnMeasurement(conn_it->second.endpoint_id, conn_id);
  addDataMeasurement(conn_id, conn_it->second.ctrl_data_ctx_id, "publish");

  messages::MessageBuffer r_msg;
  r_msg << response;

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
          logger->warning << "Existing subscription is not allowed to be modified "
                          << " subscriber_id: " << context->subscriber_id
                          << std::flush;
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
      break;
    }

    case TransportMode::UsePublisher: {
      context->transport_mode_follow_publisher = true;
      context->data_ctx_id = transport->createDataContext(conn_id, true, context->priority, false);
      transport->setRemoteDataCtxId(conn_id, context->data_ctx_id, context->remote_data_ctx_id);

      break;
    }

    case TransportMode::Pause:
      [[fallthrough]];
    case TransportMode::Resume:
      // Handled previously
      return;
  }

  logger->debug << "New Subscribe conn_id: " << conn_id
                << " ns: " << subscribe.quicr_namespace
                << " transport_mode: " << static_cast<int>(context->transport_mode)
                << " subscriber_id: " << context->subscriber_id
                << " pending_data_ctx: " << context->pending_reliable_data_ctx
                << std::flush;

  addDataMeasurement(conn_id, data_ctx_id, "subscribe");
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
  if (_subscribe_state[unsub.quicr_namespace].contains(conn_id)) {
    const auto lock = std::lock_guard<std::mutex>(session_mutex);

    auto& context = _subscribe_state[unsub.quicr_namespace][conn_id];

    data_measurements.at(conn_id).erase(context->data_ctx_id);

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
    logger->info << "Dropping published object, no namespace for "
                 << datagram.header.name << std::flush;
    return;
  }

  auto& [ns, context] = *publish_namespace;

  const auto gap_log = gap_check(
    false, datagram.header.name, context.prev_group_id, context.prev_object_id);

  if (!gap_log.empty()) {
    logger->info << "conn_id: " << conn_id
                 << " data_ctx_id: " << (data_ctx_id ? *data_ctx_id : 0)
                 << " " << gap_log << std::flush;
  }

  if (!data_ctx_id && stream_id) {
    data_ctx_id = context.data_ctx_id;
    transport->setStreamIdDataCtxId(conn_id, *data_ctx_id, *stream_id);
  }

  addDataMeasurement(conn_id, *data_ctx_id, "publish");
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
      [[fallthrough]]; // TODO: Resend response?
    case PublishIntentContext::State::Ready:
      [[fallthrough]]; // TODO: Already registered this namespace successfully, do nothing?
    default:
      break;
  }

  addDataMeasurement(conn_id, ps_it->second.data_ctx_id, "publish");

  delegate->onPublishIntent(intent.quicr_namespace,
                            "" /* intent.origin_url */,
                            "" /* intent.relay_token */,
                            std::move(intent.payload));
}

void
ServerRawSession::handle_publish_intent_end(
  const qtransport::TransportConnId& conn_id,
  const qtransport::DataContextId& data_ctx_id,
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

  data_measurements.at(conn_id).erase(data_ctx_id);

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
  conn_ctx.remote = remote;
}

void ServerRawSession::TransportDelegate::on_new_data_context(const qtransport::TransportConnId &conn_id,
                                                              const qtransport::DataContextId &data_ctx_id)
{
  LOGGER_DEBUG(server.logger, "New BiDir data context conn_id: " << conn_id << " data_ctx_id: " << data_ctx_id);
}

void
ServerRawSession::TransportDelegate::on_recv_stream(const qtransport::TransportConnId& conn_id,
                                                    uint64_t stream_id,
                                                    std::optional<qtransport::DataContextId> data_ctx_id,
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

          server.handle(conn_id, stream_id, data_ctx_id,
                        std::move(msg_buffer), is_bidir);

        } catch (const messages::MessageBuffer::ReadException& ex) {

          // TODO(trigaux): When reliable, we really should reset the stream if
          // this happens (at least more than once)
          server.logger->critical
            << "Received read exception error while reading from message buffer: "
            << ex.what() << std::flush;
          return;

        } catch (const std::exception& /* ex */) {
          server.logger->Log(cantina::LogLevel::Critical,
                             "Received standard exception error while reading "
                             "from message buffer");
          return;
        } catch (...) {
          server.logger->Log(
            cantina::LogLevel::Critical,
            "Received unknown error while reading from message buffer");
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
ServerRawSession::TransportDelegate::on_recv_dgram(const qtransport::TransportConnId& conn_id,
                                                   std::optional<qtransport::DataContextId> data_ctx_id)
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
        server.logger->critical
          << "Received read exception error while reading from message buffer: "
          << ex.what() << std::flush;
        continue;

      } catch (const std::exception& e) {
        server.logger->critical
          << "Received standard exception error while reading from message buffer: "
          << e.what();
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

void
ServerRawSession::publishMeasurement(const Measurement& measurement)
{
  const json measurement_json = measurement;
  const std::string measurement_str = measurement_json.dump();
  quicr::bytes measurement_bytes(measurement_str.begin(), measurement_str.end());

  messages::PublishDatagram datagram;
  datagram.header.name = *metrics_namespace;
  datagram.header.media_id = 0;
  datagram.header.group_id = 0;
  datagram.header.object_id = 0;
  datagram.header.priority = 127;
  datagram.header.offset_and_fin = 1ULL;
  datagram.media_type = messages::MediaType::RealtimeMedia;
  datagram.media_data_length = measurement_bytes.size();
  datagram.media_data = std::move(measurement_bytes);

  delegate->onPublisherObject(std::numeric_limits<qtransport::TransportConnId>::max(), 0, false, std::move(datagram));
}

void
ServerRawSession::runPublishMeasurements()
{
  LOGGER_INFO(logger, "Starting metrics thread");
  _metrics_thread = std::thread([this]{
    while (_running)
    {
      // TODO(trigaux): Currently for adapting previous version of transport metrics, should be removed once new changes for MOQT are in.
      while (!metrics_conn_samples->empty())
      {
        const auto conn_sample = metrics_conn_samples->block_pop();
        auto conn_it = conn_measurements.find(conn_sample->conn_ctx_id);
        if (conn_it == conn_measurements.end())
        {
          LOGGER_WARNING(logger, "Failed to set metrics for conn context '" << conn_sample->conn_ctx_id << "', skipping sending metrics for this context.");
          continue;
        }

        auto tp = std::chrono::system_clock::now()
                        + std::chrono::duration_cast<std::chrono::system_clock::duration>(conn_sample->sample_time - std::chrono::steady_clock::now());

        auto& conn_measurement = conn_it->second;
        conn_measurement
          .SetTime(tp)
          .SetMetric("tx_retransmits", conn_sample->quic_sample->tx_retransmits)
          .SetMetric("tx_congested", conn_sample->quic_sample->tx_congested)
          .SetMetric("tx_lost_pkts", conn_sample->quic_sample->tx_lost_pkts)
          .SetMetric("tx_timer_losses", conn_sample->quic_sample->tx_timer_losses)
          .SetMetric("tx_spurious_losses", conn_sample->quic_sample->tx_spurious_losses)
          .SetMetric("tx_dgram_lost", conn_sample->quic_sample->tx_dgram_lost)
          .SetMetric("tx_dgram_ack", conn_sample->quic_sample->tx_dgram_ack)
          .SetMetric("tx_dgram_cb", conn_sample->quic_sample->tx_dgram_cb)
          .SetMetric("tx_dgram_spurious", conn_sample->quic_sample->tx_dgram_spurious)
          .SetMetric("rx_dgrams", conn_sample->quic_sample->rx_dgrams)
          .SetMetric("rx_dgrams_bytes", conn_sample->quic_sample->rx_dgrams_bytes)
          .SetMetric("cwin_congested", conn_sample->quic_sample->cwin_congested)
          .SetMetric("tx_rate_bps_min", conn_sample->quic_sample->tx_rate_bps.min)
          .SetMetric("tx_rate_bps_max", conn_sample->quic_sample->tx_rate_bps.max)
          .SetMetric("tx_rate_bps_avg", conn_sample->quic_sample->tx_rate_bps.avg)
          .SetMetric("tx_in_transit_bytes_min", conn_sample->quic_sample->tx_in_transit_bytes.min)
          .SetMetric("tx_in_transit_bytes_max", conn_sample->quic_sample->tx_in_transit_bytes.max)
          .SetMetric("tx_in_transit_bytes_avg", conn_sample->quic_sample->tx_in_transit_bytes.avg)
          .SetMetric("tx_cwin_bytes_min", conn_sample->quic_sample->tx_cwin_bytes.min)
          .SetMetric("tx_cwin_bytes_max", conn_sample->quic_sample->tx_cwin_bytes.max)
          .SetMetric("tx_cwin_bytes_avg", conn_sample->quic_sample->tx_cwin_bytes.avg)
          .SetMetric("rtt_us_min", conn_sample->quic_sample->rtt_us.min)
          .SetMetric("rtt_us_max", conn_sample->quic_sample->rtt_us.max)
          .SetMetric("rtt_us_avg", conn_sample->quic_sample->rtt_us.avg)
          .SetMetric("srtt_us_min", conn_sample->quic_sample->srtt_us.min)
          .SetMetric("srtt_us_max", conn_sample->quic_sample->srtt_us.max)
          .SetMetric("srtt_us_avg", conn_sample->quic_sample->srtt_us.avg);

        publishMeasurement(conn_measurement);
      }

      // TODO(trigaux): Currently for adapting previous version of transport metrics, should be removed once new changes for MOQT are in.
      while (!metrics_data_samples->empty())
      {
        const auto data_sample = metrics_data_samples->block_pop();
        if (data_measurements.find(data_sample->conn_ctx_id) == data_measurements.end())
        {
          LOGGER_WARNING(logger, "Failed to set data metrics for connection with id '" << data_sample->conn_ctx_id << "', skipping sending metrics for this context.");
          continue;
        }

        auto data_ns_it = data_measurements.at(data_sample->conn_ctx_id).find(data_sample->data_ctx_id);
        if (data_ns_it == data_measurements.at(data_sample->conn_ctx_id).end())
        {
          LOGGER_WARNING(logger, "Failed to set metrics for data context '" << data_sample->data_ctx_id << "' (connection = " << data_sample->conn_ctx_id << "), skipping sending metrics for this context.");
          continue;
        }

        auto tp = std::chrono::system_clock::now()
                        + std::chrono::duration_cast<std::chrono::system_clock::duration>(data_sample->sample_time - std::chrono::steady_clock::now());

        auto& data_measurement = data_ns_it->second;
        data_measurement
          .SetTime(tp)
          .SetMetric("enqueued_objs", data_sample->quic_sample->enqueued_objs)
          .SetMetric("tx_queue_size_min", data_sample->quic_sample->tx_queue_size.min)
          .SetMetric("tx_queue_size_max", data_sample->quic_sample->tx_queue_size.max)
          .SetMetric("tx_queue_size_avg", data_sample->quic_sample->tx_queue_size.avg)
          .SetMetric("rx_stream_bytes", data_sample->quic_sample->rx_stream_bytes)
          .SetMetric("rx_stream_cb", data_sample->quic_sample->rx_stream_cb)
          .SetMetric("tx_dgrams", data_sample->quic_sample->tx_dgrams)
          .SetMetric("tx_dgrams_bytes", data_sample->quic_sample->tx_dgrams_bytes)
          .SetMetric("tx_stream_objs", data_sample->quic_sample->tx_stream_objects)
          .SetMetric("tx_stream_bytes", data_sample->quic_sample->tx_stream_bytes)
          .SetMetric("tx_buffer_drops", data_sample->quic_sample->tx_buffer_drops)
          .SetMetric("tx_delayed_callback", data_sample->quic_sample->tx_delayed_callback)
          .SetMetric("tx_queue_discards", data_sample->quic_sample->tx_queue_discards)
          .SetMetric("tx_queue_expired", data_sample->quic_sample->tx_queue_expired)
          .SetMetric("tx_reset_wait", data_sample->quic_sample->tx_reset_wait)
          .SetMetric("tx_stream_cb", data_sample->quic_sample->tx_stream_cb)
          .SetMetric("tx_callback_ms_min", data_sample->quic_sample->tx_callback_ms.min)
          .SetMetric("tx_callback_ms_max", data_sample->quic_sample->tx_callback_ms.max)
          .SetMetric("tx_callback_ms_avg", data_sample->quic_sample->tx_callback_ms.avg)
          .SetMetric("tx_object_duration_us_min", data_sample->quic_sample->tx_object_duration_us.min)
          .SetMetric("tx_object_duration_us_max", data_sample->quic_sample->tx_object_duration_us.max)
          .SetMetric("tx_object_duration_us_avg", data_sample->quic_sample->tx_object_duration_us.avg);

        publishMeasurement(data_measurement);
      }
    }
    LOGGER_INFO(logger, "Finished metrics thread");
  });
}

Measurement&
ServerRawSession::addConnMeasurement(const std::string& endpoint_id,
                                     const qtransport::TransportConnId& id)
{
  if (conn_measurements.contains(id))
  {
    return conn_measurements.at(id);
  }

  conn_measurements[id] =
    Measurement("quic-connection")
      .AddAttribute("endpoint_id", endpoint_id)
      .AddAttribute("relay_id", relay_id)
      .AddAttribute("source", "server")
      .AddMetric("tx_retransmits", std::uint64_t(0))
      .AddMetric("tx_congested", std::uint64_t(0))
      .AddMetric("tx_lost_pkts", std::uint64_t(0))
      .AddMetric("tx_timer_losses", std::uint64_t(0))
      .AddMetric("tx_spurious_losses", std::uint64_t(0))
      .AddMetric("tx_dgram_lost", std::uint64_t(0))
      .AddMetric("tx_dgram_ack", std::uint64_t(0))
      .AddMetric("tx_dgram_cb", std::uint64_t(0))
      .AddMetric("tx_dgram_spurious", std::uint64_t(0))
      .AddMetric("rx_dgrams", std::uint64_t(0))
      .AddMetric("rx_dgrams_bytes", std::uint64_t(0))
      .AddMetric("cwin_congested", std::uint64_t(0))
      .AddMetric("tx_rate_bps_min", std::uint64_t(0))
      .AddMetric("tx_rate_bps_max", std::uint64_t(0))
      .AddMetric("tx_rate_bps_avg", std::uint64_t(0))
      .AddMetric("tx_in_transit_bytes_min", std::uint64_t(0))
      .AddMetric("tx_in_transit_bytes_max", std::uint64_t(0))
      .AddMetric("tx_in_transit_bytes_avg", std::uint64_t(0))
      .AddMetric("tx_cwin_bytes_min", std::uint64_t(0))
      .AddMetric("tx_cwin_bytes_max", std::uint64_t(0))
      .AddMetric("tx_cwin_bytes_avg", std::uint64_t(0))
      .AddMetric("rtt_us_min", std::uint64_t(0))
      .AddMetric("rtt_us_max", std::uint64_t(0))
      .AddMetric("rtt_us_avg", std::uint64_t(0))
      .AddMetric("srtt_us_min", std::uint64_t(0))
      .AddMetric("srtt_us_max", std::uint64_t(0))
      .AddMetric("srtt_us_avg", std::uint64_t(0));

  return conn_measurements.at(id);
}

Measurement&
ServerRawSession::addDataMeasurement(const qtransport::TransportConnId& conn_id,
                                     const qtransport::DataContextId& data_ctx_id,
                                     const std::string& type)
{
  if (data_measurements.contains(conn_id) && data_measurements.at(conn_id).contains(data_ctx_id))
  {
    return data_measurements.at(conn_id).at(data_ctx_id);
  }

  data_measurements[conn_id][data_ctx_id] =
    Measurement("quic-dataFlow")
      .AddAttribute(conn_measurements.at(conn_id).GetAttribute("endpoint_id"))
      .AddAttribute("relay_id", relay_id)
      .AddAttribute("source", "server")
      .AddAttribute("type", type)
      .AddMetric("enqueued_objs", std::uint64_t(0))
      .AddMetric("tx_queue_size_min", std::uint64_t(0))
      .AddMetric("tx_queue_size_max", std::uint64_t(0))
      .AddMetric("tx_queue_size_avg", std::uint64_t(0))
      .AddMetric("rx_buffer_drops", std::uint64_t(0))
      .AddMetric("rx_dgrams", std::uint64_t(0))
      .AddMetric("rx_dgrams_bytes", std::uint64_t(0))
      .AddMetric("rx_stream_objs", std::uint64_t(0))
      .AddMetric("rx_invalid_drops", std::uint64_t(0))
      .AddMetric("rx_stream_bytes", std::uint64_t(0))
      .AddMetric("rx_stream_cb", std::uint64_t(0))
      .AddMetric("tx_dgrams", std::uint64_t(0))
      .AddMetric("tx_dgrams_bytes", std::uint64_t(0))
      .AddMetric("tx_stream_objs", std::uint64_t(0))
      .AddMetric("tx_stream_bytes", std::uint64_t(0))
      .AddMetric("tx_buffer_drops", std::uint64_t(0))
      .AddMetric("tx_delayed_callback", std::uint64_t(0))
      .AddMetric("tx_queue_discards", std::uint64_t(0))
      .AddMetric("tx_queue_expired", std::uint64_t(0))
      .AddMetric("tx_reset_wait", std::uint64_t(0))
      .AddMetric("tx_stream_cb", std::uint64_t(0))
      .AddMetric("tx_callback_ms_min", std::uint64_t(0))
      .AddMetric("tx_callback_ms_max", std::uint64_t(0))
      .AddMetric("tx_callback_ms_avg", std::uint64_t(0))
      .AddMetric("tx_object_duration_us_min", std::uint64_t(0))
      .AddMetric("tx_object_duration_us_max", std::uint64_t(0))
      .AddMetric("tx_object_duration_us_avg", std::uint64_t(0));

    return data_measurements.at(conn_id).at(data_ctx_id);
}
} // namespace quicr
