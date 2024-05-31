/*
 *  quicr_client_raw_session.cpp
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

#include "quicr_client_raw_session.h"

#include "quicr/encode.h"
#include "quicr/gap_check.h"
#include "quicr/message_buffer.h"
#include "quicr/quicr_client.h"
#include "quicr/quicr_client_delegate.h"
#include "quicr/quicr_common.h"

#include <nlohmann/json.hpp>
#include <transport/transport_metrics.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>

using json = nlohmann::json;

namespace quicr {

namespace {

qtransport::TransportRemote
to_TransportRemote(const RelayInfo& info) noexcept
{
  return {
    .host_or_ip = info.hostname,
    .port = info.port,
    .proto = info.proto == RelayInfo::Protocol::UDP
               ? qtransport::TransportProtocol::UDP
               : qtransport::TransportProtocol::QUIC,
  };
}

} // namespace

/*===========================================================================*/
// ClientRawSession
/*===========================================================================*/

ClientRawSession::ClientRawSession(const RelayInfo& relay_info,
                                   const std::string& endpoint_id,
                                   size_t chunk_size,
                                   const qtransport::TransportConfig& tconfig,
                                   const cantina::LoggerPointer& logger,
                                   std::optional<Namespace> metrics_ns)
  : logger(std::make_shared<cantina::Logger>("QSES", logger))
  , _endpoint_id(endpoint_id)
  , _chunk_size(chunk_size)
{
  this->logger->Log("Initialize Client");

  if (relay_info.proto == RelayInfo::Protocol::UDP) {
    transport_needs_fragmentation = true;
  }

  if (_chunk_size && _chunk_size < max_transport_data_size) {
    _chunk_size = max_transport_data_size;
  }

  const auto server = to_TransportRemote(relay_info);
  transport = qtransport::ITransport::make_client_transport(
    server, tconfig, *this, this->logger);

  logger->info << "Starting metrics exporter" << std::flush;

  metrics_conn_samples = std::make_shared<qtransport::safe_queue<qtransport::MetricsConnSample>>(qtransport::MAX_METRICS_SAMPLES_QUEUE);
  metrics_data_samples = std::make_shared<qtransport::safe_queue<qtransport::MetricsDataSample>>(qtransport::MAX_METRICS_SAMPLES_QUEUE);

  if (metrics_ns)
  {
    metrics_namespace = *metrics_ns;
  }

  connection_measurement =
    Measurement("quic-connection")
      .AddAttribute("endpoint_id", endpoint_id)
      .AddAttribute("relay_id", relay_info.relay_id)
      .AddAttribute("source", "client")
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
}

ClientRawSession::ClientRawSession(
  std::shared_ptr<qtransport::ITransport> transport_in,
  const cantina::LoggerPointer& logger)
  : has_shared_transport{ true }
  , logger(std::make_shared<cantina::Logger>("QSES", logger))
  , transport(std::move(transport_in))
{
}

ClientRawSession::~ClientRawSession()
{
  if (!transport)
    return;

  if (!has_shared_transport &&
      transport->status() != qtransport::TransportStatus::Disconnected) {
    // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall)
    disconnect();
  }

  _metrics_thread.join();
}

bool
ClientRawSession::connect()
{
  if (!transport) {
    throw std::runtime_error(
      "Transport has been destroyed, create a new session object!");
  }

  transport_conn_id =
    transport->start(metrics_conn_samples, metrics_data_samples);

  LOGGER_INFO(logger, "Connecting session " << *transport_conn_id << "...");

  while (!stopping &&
         transport->status() == qtransport::TransportStatus::Connecting) {
    LOGGER_DEBUG(logger, "Connecting... " << int(stopping.load()));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  if (stopping || !transport) {
    LOGGER_INFO(logger, "Cancelling connecting session " << *transport_conn_id);
    return false;
  }

  if (!connected()) {
    std::ostringstream msg;
    msg << "Session " << *transport_conn_id
        << " failed to connect to server, transport status: "
        << int(transport->status());
    logger->Log(cantina::LogLevel::Critical, msg.str());

    throw std::runtime_error(msg.str());
  }

  // Create reliable bidirectional control stream
  transport_ctrl_data_ctx_id = transport->createDataContext(*transport_conn_id, true, 0, true);

  addDataMeasurement(*transport_ctrl_data_ctx_id, "publish");

  // Send connect message
  auto msg = messages::MessageBuffer{ sizeof(messages::Subscribe) + _endpoint_id.size() };
  const auto connect = messages::Connect{ 0x1, _endpoint_id };
  msg << connect;
  enqueue_ctrl_msg(msg.take());

  return true;
}

bool
ClientRawSession::disconnect()
{
  // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall)
  if (stopping || !(connected() || connecting())) {
    // Not connected or already stopping/stopped
    return true;
  }

  const auto& conn_id =
    transport_conn_id ? transport_conn_id.value() : 0;
  LOGGER_DEBUG(logger, "Disconnecting session " << conn_id << "...");

  stopping = true;
  try {
    transport->close(conn_id);
  } catch (const std::exception& e) {
    LOGGER_ERROR(
      logger, "Error disconnecting session " << conn_id << ": " << e.what());
    return false;
  } catch (...) {
    LOGGER_ERROR(logger, "Unknown error disconnecting session " << conn_id);
    return false;
  }

  LOGGER_INFO(logger, "Disconnected session " << conn_id << "!");

  transport_conn_id = std::nullopt;
  transport_ctrl_data_ctx_id = std::nullopt;

  return true;
}

bool
ClientRawSession::connected() const
{
  return transport && transport->status() == qtransport::TransportStatus::Ready;
}

bool
ClientRawSession::connecting() const
{
  return transport && transport->status() == qtransport::TransportStatus::Connecting;
}

void
ClientRawSession::runPublishMeasurements()
{
  LOGGER_INFO(logger, "Starting metrics thread");
  _metrics_thread = std::thread([this]{
    while (!stopping)
    {
      // TODO(trigaux): Currently for adapting previous version of transport metrics, should be removed once new changes for MOQT are in.
      while (!metrics_conn_samples->empty())
      {
        const auto conn_sample = metrics_conn_samples->block_pop();
        auto tp = std::chrono::system_clock::now()
                        + std::chrono::duration_cast<std::chrono::system_clock::duration>(conn_sample->sample_time - std::chrono::steady_clock::now());
        connection_measurement
          .SetTime(tp)
          .SetAttribute("relay_id", _relay_id)
          .SetMetric("tx_retransmits", conn_sample->quic_sample->tx_retransmits)
          .SetMetric("tx_congested", conn_sample->quic_sample->tx_congested)
          .SetMetric("tx_lost_pkts", conn_sample->quic_sample->tx_lost_pkts)
          .SetMetric("tx_timer_losses", conn_sample->quic_sample->tx_timer_losses)
          .SetMetric("tx_spurious_losses", conn_sample->quic_sample->tx_spurious_losses)
          .SetMetric("tx_dgram_lost", conn_sample->quic_sample->tx_dgram_lost)
          .SetMetric("tx_dgram_ack", conn_sample->quic_sample->tx_dgram_ack)
          .SetMetric("tx_dgram_cb", conn_sample->quic_sample->tx_dgram_cb)
          .SetMetric("tx_dgram_spurious", conn_sample->quic_sample->tx_dgram_spurious)
          .SetMetric("dgram_invalid_ctx_id", conn_sample->quic_sample->dgram_invalid_ctx_id)
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

        publishMeasurement(connection_measurement);
      }

      // TODO(trigaux): Currently for adapting previous version of transport metrics, should be removed once new changes for MOQT are in.
      while (!metrics_data_samples->empty())
      {
        const auto data_sample = metrics_data_samples->block_pop();
        auto data_ns_it = data_measurements.find(data_sample->data_ctx_id);
        if (data_ns_it == data_measurements.end())
        {
          LOGGER_WARNING(logger, "Failed to set metrics for data context '" << data_sample->data_ctx_id << "', skipping sending metrics for this context.");
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
          .SetMetric("rx_buffer_drops", data_sample->quic_sample->rx_buffer_drops)
          .SetMetric("rx_dgrams", data_sample->quic_sample->rx_dgrams)
          .SetMetric("rx_dgrams_bytes", data_sample->quic_sample->rx_dgrams_bytes)
          .SetMetric("rx_stream_objs", data_sample->quic_sample->rx_stream_objects)
          .SetMetric("rx_invalid_drops", data_sample->quic_sample->rx_invalid_drops)
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

void
ClientRawSession::publishMeasurement(const Measurement& measurement)
{
  const json measurement_json = measurement;
  const std::string measurement_str = measurement_json.dump();
  quicr::bytes measurement_bytes(measurement_str.begin(), measurement_str.end());

  const auto start_time = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::steady_clock::now());
  std::vector<qtransport::MethodTraceItem> trace;
  trace.reserve(10);
  trace.push_back({"libquicr:publishMetrics:begin", start_time});

  publishNamedObject(metrics_namespace, 31, 500, std::move(measurement_bytes), std::move(trace));
}

/*===========================================================================*/
// Transport Delegate Events
/*===========================================================================*/

void
ClientRawSession::on_connection_status(
  const qtransport::TransportConnId& conn_id,
  const qtransport::TransportStatus status)
{
  {
    LOGGER_DEBUG(logger,
                 "connection_status: cid: " << conn_id
                                            << " status: " << int(status));
  }

  switch (status) {
    case qtransport::TransportStatus::Connecting:
      [[fallthrough]];
    case qtransport::TransportStatus::Ready:
      stopping = false;
      break;
    case qtransport::TransportStatus::Disconnected:
      LOGGER_INFO(logger,
                  "Received disconnect from transport for context: "
                    << conn_id);
      [[fallthrough]];
    case qtransport::TransportStatus::Shutdown:
      [[fallthrough]];
    case qtransport::TransportStatus::RemoteRequestClose:
      LOGGER_INFO(logger, "Shutting down context: " << conn_id);
      stopping = true;
      break;
  }
}

void
ClientRawSession::on_new_connection(
  const qtransport::TransportConnId& /* conn_id */,
  const qtransport::TransportRemote& /* remote */)
{
}

void
ClientRawSession::on_new_data_context(const qtransport::TransportConnId &conn_id, const qtransport::DataContextId &data_ctx_id) {
  LOGGER_INFO(logger, "New BiDir data context conn_id: " << conn_id << " data_ctx_id: " << data_ctx_id);
}

void ClientRawSession::on_recv_dgram(const qtransport::TransportConnId& conn_id,
                                     std::optional<qtransport::DataContextId> data_ctx_id)
{
  if (!transport) {
    return;
  }

  for (int i = 0; i < 150; i++) {
    auto data = transport->dequeue(conn_id, data_ctx_id);

    if (!data) {
      return;
    }

    messages::MessageBuffer msg_buffer{ *data };

    try {
      handle(std::nullopt, std::nullopt, std::move(msg_buffer));
    } catch (const std::exception& e) {
      LOGGER_DEBUG(logger, "Dropping malformed message: " << e.what());
      return;
    } catch (...) {
      LOGGER_CRITICAL(logger,
                      "Received malformed message with unknown fatal error");
      throw;
    }
  }
}

void ClientRawSession::on_recv_stream(const qtransport::TransportConnId& conn_id,
                                      uint64_t stream_id,
                                      std::optional<qtransport::DataContextId> data_ctx_id,
                                      [[maybe_unused]] const bool is_bidir)
{
  if (!transport) {
    return;
  }

  auto stream_buf = transport->getStreamBuffer(conn_id, stream_id);

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
          handle(stream_id, data_ctx_id, std::move(msg_buffer));
        } catch (const std::exception& e) {
          LOGGER_DEBUG(logger, "Dropping malformed message: " << e.what());
          return;
        } catch (...) {
          LOGGER_CRITICAL(
            logger, "Received malformed message with unknown fatal error");
          throw;
        }
      } else {
        break;
      }
    } else {
      break;
    }
  }
}


/*===========================================================================*/
// QuicrClientSession API Methods
/*===========================================================================*/

bool
ClientRawSession::publishIntent(std::shared_ptr<PublisherDelegate> pub_delegate,
                                const quicr::Namespace& quicr_namespace,
                                const std::string& /* origin_url */,
                                const std::string& /* auth_token */,
                                bytes&& payload,
                                const TransportMode transport_mode,
                                uint8_t priority)
{
  if (pub_delegates.contains(quicr_namespace)) {
    return true;
  }

  if (transport_mode == TransportMode::UsePublisher) {
    logger->error << "Publish Intent for ns: " << quicr_namespace << " cannot use 'UsePublisher'" << std::flush;
    throw std::runtime_error("Transport mode cannot be 'UsePublisher' for publish intents");
  }

  pub_delegates[quicr_namespace] = std::move(pub_delegate);

  const auto& conn_id = transport_conn_id.value();
  auto data_ctx_id = get_or_create_data_ctx_id(conn_id, transport_mode, priority);

  auto& measurement = addDataMeasurement(data_ctx_id, "publish");
  measurement.AddAttribute("namespace", std::string(quicr_namespace));

  logger->info << "Publish Intent ns: " << quicr_namespace
               << " data_ctx_id: " << data_ctx_id
               << " priority: " << static_cast<int>(priority)
               << " mode: " << static_cast<int>(transport_mode)
               << std::flush;

  publish_state[quicr_namespace] = {
    .state = PublishContext::State::Pending,
    .transport_conn_id = conn_id,
    .transport_data_ctx_id = data_ctx_id,
    .transport_mode = transport_mode,
  };

  const auto intent = messages::PublishIntent{
    messages::MessageType::PublishIntent,
    messages::create_transaction_id(),
    quicr_namespace,
    std::move(payload),
    data_ctx_id,
    1,
    transport_mode,
  };

  messages::MessageBuffer msg{ sizeof(messages::PublishIntent) +
                               intent.payload.size() };
  msg << intent;

  auto error =   enqueue_ctrl_msg(msg.take());

  return error == qtransport::TransportError::None;
}

void
ClientRawSession::publishIntentEnd(
  const quicr::Namespace& quicr_namespace,
  [[maybe_unused]] const std::string& auth_token)
{
  // TODO(trigaux): Authenticate token.

  if (!pub_delegates.contains(quicr_namespace)) {
    return;
  }

  pub_delegates.erase(quicr_namespace);

  const auto ps_it = publish_state.find(quicr_namespace);
  if (ps_it != publish_state.end()) {
    const auto intent_end = messages::PublishIntentEnd{
      messages::MessageType::PublishIntentEnd,
      quicr_namespace,
      {} // TODO(trigaux): Figure out payload.
    };

    messages::MessageBuffer msg;
    msg << intent_end;

    enqueue_ctrl_msg(msg.take());

    transport->deleteDataContext(ps_it->second.transport_conn_id, ps_it->second.transport_data_ctx_id);

    data_measurements.erase(ps_it->second.transport_data_ctx_id);

    publish_state.erase(ps_it);
  }
}

void
ClientRawSession::subscribe(
  std::shared_ptr<SubscriberDelegate> subscriber_delegate,
  const quicr::Namespace& quicr_namespace,
  const SubscribeIntent& intent,
  const TransportMode transport_mode,
  [[maybe_unused]] const std::string& origin_url,
  [[maybe_unused]] const std::string& auth_token,
  [[maybe_unused]] bytes&& e2e_token,
  const uint8_t priority)
{
  std::lock_guard<std::mutex> _(session_mutex);

  const auto& conn_id = transport_conn_id.value();
  auto transaction_id = messages::create_transaction_id();

  if (!sub_delegates.contains(quicr_namespace)) {
    sub_delegates[quicr_namespace] = std::move(subscriber_delegate);

    logger->info << "Subscribe ns: " << quicr_namespace
                 << " priority: " << static_cast<int>(priority)
           << " mode: " << static_cast<int>(transport_mode)
           << std::flush;

    const auto data_ctx_id = get_or_create_data_ctx_id(conn_id, transport_mode, priority);
    subscribe_state[quicr_namespace] = SubscribeContext{
      .state = SubscriptionState::Pending,
      .transport_conn_id = conn_id,
      .transport_data_ctx_id = data_ctx_id,
      .transport_mode = transport_mode,
      .transaction_id = transaction_id,
    };

    auto& measurement = addDataMeasurement(data_ctx_id, "subscribe");
    measurement.AddAttribute("namespace", std::string(quicr_namespace));
  }

  // We allow duplicate subscriptions, so we always send
  const auto sub_it = subscribe_state.find(quicr_namespace);
  if (sub_it == subscribe_state.end()) {
    // TODO(tievens): Should never hit this, but probably should erase sub_delegate if we do hit this
    return;
  }

  sub_it->second.transport_mode = transport_mode;

  if (transport_mode == TransportMode::Pause) {
    if (sub_it->second.state != SubscriptionState::Ready) {
          logger->error << "Failed to pause ns: " << quicr_namespace << " due to state not being ready" << std::flush;
      return;
    }
    sub_it->second.state = SubscriptionState::Paused;
  }
  else if (transport_mode == TransportMode::Resume) {
    if (sub_it->second.state != SubscriptionState::Paused) {
          logger->info << "Ignoring Resume ns: " << quicr_namespace << " due to state not being Paused" << std::flush;
      return;
    }
    sub_it->second.state = SubscriptionState::Ready;
  }

  auto msg = messages::MessageBuffer{ sizeof(messages::Subscribe) };
  const auto subscribe =
    messages::Subscribe{ 0x1, transaction_id, quicr_namespace, intent,
                         transport_mode, sub_it->second.transport_data_ctx_id };
  msg << subscribe;

  enqueue_ctrl_msg(msg.take());
}

void
ClientRawSession::unsubscribe(const quicr::Namespace& quicr_namespace,
                              const std::string& /* origin_url */,
                              const std::string& /* auth_token */)
{
  // The removal of the delegate is done on receive of subscription ended
  auto msg = messages::MessageBuffer{};
  const auto unsub = messages::Unsubscribe{ 0x1, quicr_namespace };
  msg << unsub;

  const auto state_it = subscribe_state.find(quicr_namespace);
  if (state_it != subscribe_state.end()) {
    enqueue_ctrl_msg(msg.take());
  }

  std::lock_guard<std::mutex> _(session_mutex);

  data_measurements.erase(state_it->second.transport_data_ctx_id);

  removeSubscription(quicr_namespace,
                     SubscribeResult::SubscribeStatus::UnSubscribed);
}

SubscriptionState ClientRawSession::getSubscriptionState(const quicr::Namespace &quicr_namespace)
{
  const auto state_it = subscribe_state.find(quicr_namespace);
  if (state_it != subscribe_state.end()) {
    return state_it->second.state;
  }

  return SubscriptionState::Unknown;
}

void
ClientRawSession::publishNamedObject(const quicr::Name& quicr_name,
  uint8_t priority,
  uint16_t expiry_age_ms,
  bytes&& data,
                                     std::vector<qtransport::MethodTraceItem> &&trace)
{
  const auto trace_start_time = trace.front().start_time;
  trace.push_back({"libquicr:publishNamedObject:begin", trace_start_time});

  // start populating message to encode
  auto datagram = messages::PublishDatagram{};

  auto ps_it = publish_state.find(quicr_name);

  if (ps_it == publish_state.end()) {
    LOGGER_INFO(
      logger, "No publish intent for '" << quicr_name << "' missing, dropping");

    return;
  }

  auto& [ns, context] = *ps_it;

  /*
    if (context.state != PublishContext::State::Ready) {
      LOGGER_INFO(logger, "Publish intent NOT READY for ns: " << ns << " got state: " << static_cast<int>(context.state));
    }
  */

  // IMPORTANT - Gap check updates the last_group_id and last_object_id to be current group/object
  const auto prev_group_id = context.group_id;
  auto gap_log = gap_check(true, quicr_name, context.group_id, context.object_id);
  if (!gap_log.empty()) {
    logger->Log(gap_log);
  }

  datagram.header.name = quicr_name;
  datagram.header.media_id = context.transport_data_ctx_id;
  datagram.header.group_id = context.group_id;
  datagram.header.object_id = context.object_id;
  datagram.header.priority = priority;
  datagram.header.offset_and_fin = 1ULL;
  datagram.media_type = messages::MediaType::RealtimeMedia;

  qtransport::ITransport::EnqueueFlags eflags;

  switch (context.transport_mode) {
    case TransportMode::ReliablePerGroup: {
      eflags.use_reliable = true;

      if (context.group_id && context.group_id != prev_group_id) {
        eflags.new_stream = true;
        eflags.use_reset = true;
        eflags.clear_tx_queue = true;
      }
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

  const size_t fragment_size = transport_needs_fragmentation
      || context.transport_mode == TransportMode::Unreliable ? max_transport_data_size : _chunk_size;

  // Fragment the payload if needed
  if (data.size() <= quicr::max_transport_data_size
      || !fragment_size
      || data.size() <= fragment_size) {
    messages::MessageBuffer msg;

    datagram.media_data_length = data.size();
    datagram.media_data = std::move(data);

    msg << datagram;

    trace.push_back({"libquicr:publishNamedObject:afterEnqueue:NoFrags", trace_start_time});

    // No fragmenting needed
    transport->enqueue(context.transport_conn_id, context.transport_data_ctx_id, msg.take(), std::move(trace),
                       priority, expiry_age_ms, 0, eflags);
  } else {
    auto frag_num = data.size() / fragment_size;
    // Fragments required. At this point this only counts whole blocks
    const auto frag_remaining_bytes = data.size() % fragment_size;

    auto offset = size_t(0);

    const auto objs_per_ms = frag_num / 20 + 1;
    uint32_t objs_per_ms_sent = 0;
    uint32_t pop_delay_ms = 0;

    trace.push_back({"libquicr:publishNamedObject:afterEnqueue:Frags", trace_start_time});

    while (frag_num-- > 0) {
      auto trace_copy = trace;
      auto msg = messages::MessageBuffer{};

      if (frag_num == 0 && frag_remaining_bytes == 0) {
        datagram.header.offset_and_fin = (offset << 1U) + 1U;
      } else {
        datagram.header.offset_and_fin = offset << 1U;
      }

      const auto ptr_offset = static_cast<ptrdiff_t>(offset);
      bytes frag_data(data.begin() + ptr_offset,
                      data.begin() + ptr_offset +
                        fragment_size);

      datagram.media_data_length = frag_data.size();
      datagram.media_data = std::move(frag_data);

      msg << datagram;

      offset += fragment_size;

      if (objs_per_ms_sent == objs_per_ms) {
        pop_delay_ms++;
        objs_per_ms_sent = 0;
      }
      objs_per_ms_sent++;

      if (transport->enqueue(context.transport_conn_id, context.transport_data_ctx_id, msg.take(), std::move(trace_copy),
                             priority, expiry_age_ms, pop_delay_ms, eflags) !=
          qtransport::TransportError::None) {
        // No point in finishing fragment if one is dropped
        return;
      }
      eflags.new_stream = false;
      eflags.use_reset = false;
      eflags.clear_tx_queue = false;

    }

    // Send last fragment, which will be less than max_transport_data_size
    if (frag_remaining_bytes > 0) {
      messages::MessageBuffer msg;
      datagram.header.offset_and_fin = uintVar_t((offset << 1U) + 1U);

      const auto ptr_offset = static_cast<ptrdiff_t>(offset);
      bytes frag_data(data.begin() + ptr_offset, data.end());
      datagram.media_data_length = static_cast<uintVar_t>(frag_data.size());
      datagram.media_data = std::move(frag_data);

      msg << datagram;

      trace.push_back({"libquicr:publishNamedObject:afterEnqueue:LasgFrag", trace_start_time});

      if (auto err = transport->enqueue(
            context.transport_conn_id, context.transport_data_ctx_id, msg.take(), std::move(trace), priority,
            expiry_age_ms, pop_delay_ms, eflags);
          err != qtransport::TransportError::None) {
        LOGGER_WARNING(logger,
                       "Published object delayed due to enqueue error "
                         << static_cast<unsigned>(err));
      }
    }
  }
}

void
ClientRawSession::publishNamedObjectFragment(
  const quicr::Name& /* quicr_name */,
  uint8_t /* priority */,
  uint16_t /* expiry_age_ms */,
  const uint64_t& /* offset */,
  bool /* is_last_fragment */,
  bytes&& /* data */)
{
  throw std::runtime_error("UnImplemented");
}

/*===========================================================================*/
// Internal Helper Methods
/*===========================================================================*/

qtransport::DataContextId ClientRawSession::get_or_create_data_ctx_id(
  const qtransport::TransportConnId conn_id, const TransportMode transport_mode, const uint8_t priority) {

  switch (transport_mode) {
    case TransportMode::ReliablePerTrack: {
      return transport->createDataContext(conn_id, true, priority, true);
    }

    case TransportMode::ReliablePerGroup:
      [[fallthrough]];
    case TransportMode::ReliablePerObject: {
      return transport->createDataContext(conn_id, true, priority, false);
    }

    case TransportMode::Unreliable:
      [[fallthrough]];
    default: { // Treat as unreliable
      return transport->createDataContext(conn_id, false, priority, false);
    }
  }
}

void
ClientRawSession::removeSubscription(
  const quicr::Namespace& quicr_namespace,
  const SubscribeResult::SubscribeStatus& reason)
{
  auto state_it = subscribe_state.find(quicr_namespace);
  if (state_it != subscribe_state.end()) {
    transport->deleteDataContext(state_it->second.transport_conn_id, state_it->second.transport_data_ctx_id);

    data_measurements.erase(state_it->second.transport_data_ctx_id);

    subscribe_state.erase(state_it);
  }

  if (sub_delegates.contains(quicr_namespace)) {
    if (const auto& sub_delegate = sub_delegates[quicr_namespace]) {
      sub_delegate->onSubscriptionEnded(quicr_namespace, reason);
    }

    sub_delegates.erase(quicr_namespace);
  }
}

bool
ClientRawSession::notify_pub_fragment(
  const messages::PublishDatagram& datagram,
  const std::shared_ptr<SubscriberDelegate>& delegate,
  const MsgFragment& fragment)
{
  if ((fragment.data.rbegin()->first & 1U) != 0x1) {
    return false; // Not complete, return false that this can NOT be deleted
  }

  auto reassembled = bytes{};
  auto seq_bytes = size_t(0);
  for (const auto& [sequence_num, data] : fragment.data) {
    const auto offset = sequence_num >> 1U;
    if (offset - seq_bytes != 0) {
      // Gap in offsets, missing data, return false that this can NOT be deleted
      return false;
    }

    reassembled.insert(reassembled.end(),
                       data.begin(),
                       data.end());

    seq_bytes += data.size();
  }

  delegate->onSubscribedObject(
    datagram.header.name, 0x0, std::move(reassembled));

  return true;
}

void
ClientRawSession::handle_pub_fragment(
  messages::PublishDatagram&& datagram,
  const std::shared_ptr<SubscriberDelegate>& delegate)
{
  // Search from the current `circular_index` backwards to find a buffer that
  // knows about this datagram's name
  const auto name = datagram.header.name;
  auto curr_index = circular_index;
  auto not_found = false;
  while (!fragments.at(circular_index).contains(name)) {
    curr_index = (curr_index > 0) ? curr_index - 1 : fragments.size() - 1;
    if (curr_index == circular_index) {
      // We have completed a cycle without finding our name
      not_found = true;
      break;
    }
  }

  // If we looped back to `circular_index`, then there is no current entry for
  // this name, so we should insert in the current buffer.  If inserting here
  // would cause overflow, we proactively move to the next buffer.
  if (not_found) {
    auto curr_buffer_size = fragments.at(curr_index).size();
    if (curr_buffer_size >= max_pending_per_buffer - 1) {
      circular_index += 1;
      if (circular_index >= fragments.size()) {
        circular_index = 0;
      }

      fragments.at(circular_index).clear();
      curr_index = circular_index;
    }

    fragments.at(curr_index).insert({ name, {} });
  }

  // Insert into the appropriate buffer and see whether we have a complete
  // datagram
  auto& buffer = fragments.at(curr_index);
  auto& msg_fragments = buffer.at(name);
  msg_fragments.data.emplace(datagram.header.offset_and_fin,
                             std::move(datagram.media_data));
  if (notify_pub_fragment(datagram, delegate, msg_fragments)) {
    buffer.erase(name);
  }
}

void
ClientRawSession::handle(std::optional<uint64_t> stream_id,
                         std::optional<qtransport::DataContextId> data_ctx_id,
                         messages::MessageBuffer&& msg)
{
  if (msg.empty()) {
    std::cout << "Transport Reported Empty Data" << std::endl;
    return;
  }

  auto chdr = msg.front(5); // msg_len + type
  auto msg_type = static_cast<messages::MessageType>(chdr.back());
  switch (msg_type) {
    case messages::MessageType::ConnectResponse: {
      auto response = messages::ConnectResponse{};
      msg >> response;

      _relay_id = response.relay_id;
      data_measurements.at(1).SetAttribute("relay_id", _relay_id);

      logger->info << "Received connection response with"
                   << " relay_id: " << response.relay_id
                   << std::flush;

      publishIntent(std::make_shared<MetricsPublishDelegate>(*this), metrics_namespace, "", "", {}, TransportMode::ReliablePerTrack, 31);
      break;
    }
    case messages::MessageType::SubscribeResponse: {
      auto response = messages::SubscribeResponse{};
      msg >> response;

      const auto result = SubscribeResult{ .status = response.response };

      if (sub_delegates.contains(response.quicr_namespace)) {
        auto& context = subscribe_state[response.quicr_namespace];
        context.state = SubscriptionState::Ready;

        if (const auto& sub_delegate =
              sub_delegates[response.quicr_namespace]) {
          sub_delegate->onSubscribeResponse(response.quicr_namespace, result);
        }
      } else {
        std::cout << "Got SubscribeResponse: No delegate found for namespace"
                  << response.quicr_namespace << std::endl;
      }

      break;
    }

    case messages::MessageType::SubscribeEnd: {
      auto subEnd = messages::SubscribeEnd{};
      msg >> subEnd;

      const auto _ = std::lock_guard<std::mutex>(session_mutex);
      removeSubscription(subEnd.quicr_namespace, subEnd.reason);
      break;
    }

    case messages::MessageType::Publish: {
      auto datagram = messages::PublishDatagram{};
      msg >> datagram;

      if (auto found = sub_delegates.find(datagram.header.name);
          found != sub_delegates.end()) {
        const auto& [ns, delegate] = *found;

        auto& context = subscribe_state[ns];

        if (stream_id.has_value() && not data_ctx_id.has_value() ) {
          /* NOTE:
           * In picoquic there isn't a data_ctx_id because there isn't a
           * stream context for datagram.
           */
          transport->setStreamIdDataCtxId(context.transport_conn_id,
                                          context.transport_data_ctx_id,
                                          *stream_id);
        }

        auto gap_log = gap_check(false,
                                 datagram.header.name,
                                 context.group_id,
                                 context.object_id);

        if (!gap_log.empty()) {
          logger->info << "conn_id: " << context.transport_conn_id
                       << " data_ctx_id: " << context.transport_data_ctx_id
                       << " " << gap_log << std::flush;
        }

        if (datagram.header.offset_and_fin != uintVar_t(0x1)) {
          handle_pub_fragment(std::move(datagram), delegate);
          break;
        }

        // No-fragment, process as single object
        delegate->onSubscribedObject(datagram.header.name,
                                     0x0,
                                     std::move(datagram.media_data));
      }
      break;
    }

    case messages::MessageType::PublishIntentResponse: {
      auto response = messages::PublishIntentResponse{};
      msg >> response;

      if (!pub_delegates.contains(response.quicr_namespace)) {
        std::cout
          << "Got PublishIntentResponse: No delegate found for namespace "
          << response.quicr_namespace << std::endl;
        return;
      }

      auto ps_it = publish_state.find(response.quicr_namespace);

      if (ps_it == publish_state.end()) {
        LOGGER_ERROR(logger, "No publish intent for '" << response.quicr_namespace << "' missing, dropping");
        break;
      }

      // Update/set the remote data context ID
      ps_it->second.remote_data_ctx_id = response.remote_data_ctx_id;
    transport->setRemoteDataCtxId(ps_it->second.transport_conn_id, ps_it->second.transport_data_ctx_id,
                                    response.remote_data_ctx_id);

      data_measurements[ps_it->second.remote_data_ctx_id].SetAttribute("relay_id", _relay_id);

      if (ps_it->second.state != PublishContext::State::Ready) {
        logger->info << "Publish intent ready for ns: " << response.quicr_namespace
                     << " data_ctx_id: " << ps_it->second.transport_data_ctx_id
                     << " remote_data_ctx_id: " << ps_it->second.remote_data_ctx_id
                     << std::flush;
        ps_it->second.state = PublishContext::State::Ready;
      }


      if (const auto& delegate = pub_delegates[response.quicr_namespace]) {
        const auto result = PublishIntentResult{
          .status = response.response,
          .redirectInfo = {},
          .reassignedName = {},
        };
        delegate->onPublishIntentResponse(response.quicr_namespace, result);
      }

      break;
    }

    default:
      break;
  }
}

Measurement&
ClientRawSession::addDataMeasurement(const qtransport::DataContextId& id, const std::string& type)
{
  data_measurements[id] =
    Measurement("quic-dataFlow")
      .AddAttribute("endpoint_id", _endpoint_id)
      .AddAttribute("relay_id", _relay_id)
      .AddAttribute("source", "client")
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

  return data_measurements[id];
}

void
ClientRawSession::MetricsPublishDelegate::onPublishIntentResponse(
  const quicr::Namespace&,
  const PublishIntentResult& result)
{
  if (result.status != messages::Response::Ok)
    return;

  session.runPublishMeasurements();
}
} // namespace quicr
