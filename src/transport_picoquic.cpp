// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "transport_picoquic.h"

#include "picoquic_connection.h"
#include "quicr/containers/priority_queue.h"
#include "quicr/containers/safe_queue.h"
#include "quicr/containers/stream_buffer.h"
#include "quicr/session.h"
#include "quicr/transport_metrics.h"
#include "quicr/utilities/defer.h"

#include <autoqlog.h>
#include <democlient.h>
#include <h3zero_uri.h>
#include <pico_webtransport.h>
#include <picoquic.h>
#include <picoquic_bbr.h>
#include <picoquic_config.h>
#include <picoquic_internal.h>
#include <picoquic_newreno.h>
#include <picoquic_packet_loop.h>
#include <picoquic_utils.h>
#include <picosocks.h>
#include <spdlog/spdlog.h>
#include <timeq/time_queue.h>
#include <tls_api.h>

#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <net/ethernet.h>
#include <netpacket/packet.h>
#endif

using namespace quicr;

constexpr const char* kMoqtAlpn = "moqt-18";

/* ============================================================================
 * PicoQuic Callbacks
 * ============================================================================
 */

[[maybe_unused]]
static std::string
ToHex(std::span<const uint8_t>& data)
{
    std::stringstream hex(std::ios_base::out);
    hex.flags(std::ios::hex);
    for (const auto& byte : data) {
        hex << std::setw(2) << std::setfill('0') << int(byte);
    }
    return hex.str();
}

// Callback for PicoQuic events over Raw QUIC MoQ alpn
static int
PqEventCb(picoquic_cnx_t* pq_cnx,
          uint64_t stream_id,
          uint8_t* bytes,
          size_t length,
          picoquic_call_back_event_t fin_or_event,
          void* callback_ctx,
          void* v_stream_ctx)
{
    PicoQuicTransport* transport = static_cast<PicoQuicTransport*>(callback_ctx);
    DataContext* data_ctx = static_cast<DataContext*>(v_stream_ctx);
    const auto conn_id = reinterpret_cast<uint64_t>(pq_cnx);

    bool is_fin = false;

    if (transport == NULL) {
        return PICOQUIC_ERROR_UNEXPECTED_ERROR;
    }

    transport->pq_event_thread_id = std::this_thread::get_id();

    switch (fin_or_event) {

        case picoquic_callback_prepare_datagram: {
            // length is the max allowed data length
            if (auto connection = transport->GetConnection(conn_id)) {
                connection->metrics.tx_dgram_cb++;

                transport->SendNextDatagram(connection, bytes, length);

                if (picoquic_get_cwin(pq_cnx) < kPqCcLowCwin) { // Congested if less than 8K or near jumbo MTU size
                    connection->metrics.cwin_congested++;
                }
            }

            break;
        }

        case picoquic_callback_datagram_acked:
            //   bytes carries the original packet data
            if (auto connection = transport->GetConnection(conn_id)) {
                connection->metrics.tx_dgram_ack++;
            }
            break;

        case picoquic_callback_datagram_spurious:
            if (auto connection = transport->GetConnection(conn_id)) {
                connection->metrics.tx_dgram_spurious++;
            }
            break;

        case picoquic_callback_datagram_lost:
            if (auto connection = transport->GetConnection(conn_id)) {
                connection->metrics.tx_dgram_lost++;
            }
            break;

        case picoquic_callback_datagram: {
            if (auto connection = transport->GetConnection(conn_id)) {
                transport->OnRecvDatagram(connection, bytes, length);
            }
            break;
        }

        case picoquic_callback_prepare_to_send: {
            if (picoquic_get_cwin(pq_cnx) < kPqCcLowCwin) {
                // Congested if less than 8K or near jumbo MTU size
                if (auto connection = transport->GetConnection(conn_id)) {
                    connection->metrics.cwin_congested++;
                }
            }

            if (data_ctx == NULL) {
                // picoquic calls this again even after reset/fin, here we ignore it
                SPDLOG_LOGGER_INFO(transport->logger, "conn_id: {} stream_id: {} context is null", conn_id, stream_id);
                break;
            }

            data_ctx->metrics.tx_stream_cb++;
            transport->SendStreamBytes(data_ctx, stream_id, bytes, length);
            break;
        }

        case picoquic_callback_stream_fin:
            is_fin = true;
            [[fallthrough]];
        case picoquic_callback_stream_data: {
            if (data_ctx == NULL) {
                /*
                 * Bi-Directional streams do not require per data object data context ID.
                 *  Unidirectional streams do require it, which requires out of band negotiation
                 *  of the data context ID on remote/receive side (this side). Libquicr does this
                 *  via publish and subscribes.
                 */
                if (!((stream_id & 0x2) == 2) /* not unidir stream */) {

                    // Create bidir stream if it wasn't initiated by this instance (remote initiated it)
                    if (((stream_id & 0x1) == 1 && !transport->is_server_mode) ||
                        ((stream_id & 0x0) == 0 && transport->is_server_mode)) {

                        // Create the data context for new bidir streams created by remote side
                        data_ctx = transport->CreateDataContextBiDirRecv(conn_id, stream_id);
                        picoquic_set_app_stream_ctx(pq_cnx, stream_id, data_ctx);

                    } else {
                        // No data context and we initiated it, something isn't right...
                        break;
                    }
                }
            }

            if (auto connection = transport->GetConnection(conn_id)) {
                transport->OnRecvStreamBytes(connection, data_ctx, stream_id, is_fin, std::span{ bytes, length });

                if (is_fin) {
                    SPDLOG_LOGGER_DEBUG(transport->logger, "Received FIN for stream {}", stream_id);

                    picoquic_reset_stream_ctx(pq_cnx, stream_id);

                    const auto rx_buf_it = connection->rx_stream_buffer.find(stream_id);
                    if (rx_buf_it != connection->rx_stream_buffer.end()) {
                        rx_buf_it->second.closed = true;
                        transport->OnStreamClosed(
                          connection, stream_id, rx_buf_it->second.rx_ctx, std::nullopt, StreamClosedFlag::kFin);
                    }

                    if (data_ctx != nullptr) {
                        transport->OnStreamClosed(
                          connection, stream_id, nullptr, data_ctx->data_ctx_id, StreamClosedFlag::kFin);
                        transport->DeleteDataContext(connection, data_ctx->data_ctx_id, false);
                    }
                }
            }

            break;
        }

        case picoquic_callback_stop_sending:
            // Stop sending is basically a reset initiated by the other side. MOQT suggests to RESET on this
            SPDLOG_LOGGER_DEBUG(
              transport->logger, "Received STOP_SENDING stream conn_id: {} stream_id: {}", conn_id, stream_id);
            picoquic_reset_stream(pq_cnx, stream_id, 0);
            [[fallthrough]];

        case picoquic_callback_stream_reset: {
            SPDLOG_LOGGER_DEBUG(
              transport->logger, "Received RESET stream conn_id: {} stream_id: {}", conn_id, stream_id);

            picoquic_reset_stream_ctx(pq_cnx, stream_id);

            if (auto connection = transport->GetConnection(conn_id)) {
                const auto rx_buf_it = connection->rx_stream_buffer.find(stream_id);
                if (rx_buf_it != connection->rx_stream_buffer.end()) {
                    rx_buf_it->second.closed = true;
                    transport->OnStreamClosed(
                      connection, stream_id, rx_buf_it->second.rx_ctx, std::nullopt, StreamClosedFlag::kReset);
                }

                if (data_ctx != nullptr) {
                    SPDLOG_LOGGER_DEBUG(
                      transport->logger,
                      "Received RESET stream with data context; conn_id: {} data_ctx_id: {} stream_id: {}",
                      data_ctx->conn_id,
                      data_ctx->data_ctx_id,
                      stream_id);

                    // Cleanup the reset stream.
                    transport->OnStreamClosed(
                      connection, stream_id, nullptr, data_ctx->data_ctx_id, StreamClosedFlag::kReset);
                    if (auto connection = transport->GetConnection(conn_id)) {
                        transport->EraseStreamState(connection, data_ctx, stream_id);
                    }
                }
            }

            break;
        }

        case picoquic_callback_almost_ready:
            break;

        case picoquic_callback_path_suspended:
            break;

        case picoquic_callback_path_deleted:
            break;

        case picoquic_callback_path_available:
            break;

        case picoquic_callback_path_quality_changed:
            break;

        case picoquic_callback_pacing_changed: {
            const auto cwin_bytes = picoquic_get_cwin(pq_cnx);
            const auto rtt_us = picoquic_get_rtt(pq_cnx);
            picoquic_path_quality_t path_quality;
            picoquic_get_path_quality(pq_cnx, pq_cnx->path[0]->unique_path_id, &path_quality);

            SPDLOG_LOGGER_INFO(
              transport->logger,
              "Pacing rate changed; conn_id: {} rate Kbps: {} cwin_bytes: {} rtt_us: {} rate Kbps: {} cwin_bytes: "
              "{} rtt_us: {} rtt_max: {} rtt_sample: {} lost_pkts: {} bytes_in_transit: {} recv_rate_Kbps: {}",
              conn_id,
              stream_id * 8 / 1000,
              cwin_bytes,
              rtt_us,
              path_quality.pacing_rate * 8 / 1000,
              path_quality.cwin,
              path_quality.rtt,
              path_quality.rtt_max,
              path_quality.rtt_sample,
              path_quality.lost,
              path_quality.bytes_in_transit,
              path_quality.receive_rate_estimate * 8 / 1000);
            break;
        }

        case picoquic_callback_application_close:
            SPDLOG_LOGGER_INFO(transport->logger, "Application closed conn_id: {}", conn_id);
            [[fallthrough]];
        case picoquic_callback_close: {
            uint64_t app_reason_code = picoquic_get_application_error(pq_cnx);
            std::ostringstream log_msg;
            log_msg << "Closing connection conn_id: " << conn_id << " stream_id: " << stream_id;

            switch (picoquic_get_local_error(pq_cnx)) {
                case PICOQUIC_ERROR_IDLE_TIMEOUT:
                    log_msg << " Idle timeout";
                    app_reason_code = static_cast<uint64_t>(AppReasonForClose::kIdleTimeout);
                    break;

                default:
                    app_reason_code = picoquic_get_remote_error(pq_cnx);
                    log_msg << " local_error: " << picoquic_get_local_error(pq_cnx)
                            << " remote_error: " << picoquic_get_remote_error(pq_cnx)
                            << " app_error: " << picoquic_get_application_error(pq_cnx);
            }

            picoquic_set_callback(pq_cnx, NULL, NULL);

            auto connection = transport->GetConnection(conn_id);
            if (connection) {
                log_msg << " remote: " << connection->peer_addr_text;
            }

            SPDLOG_LOGGER_INFO(transport->logger, log_msg.str());

            switch (app_reason_code) {
                case static_cast<uint64_t>(AppReasonForClose::kIdleTimeout):
                case static_cast<uint64_t>(AppReasonForClose::kShutdown):
                case static_cast<uint64_t>(AppReasonForClose::kRemoteRequestClose):
                case static_cast<uint64_t>(AppReasonForClose::kNotAuthorized):
                case static_cast<uint64_t>(AppReasonForClose::kProtocolViolation):
                case static_cast<uint64_t>(AppReasonForClose::kInternalError):
                    transport->CloseInternal(connection, static_cast<AppReasonForClose>(app_reason_code));
                    break;
                default:
                    transport->CloseInternal(connection, AppReasonForClose::kUnknown);
                    break;
            }

            if (not transport->is_server_mode) {
                // TODO: Fix picoquic. Apparently picoquic is not processing return values for this callback
                return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
            }

            return 0;
        }

        case picoquic_callback_ready: { // Connection callback, not per stream
            if (transport->is_server_mode) {
                SPDLOG_LOGGER_INFO(transport->logger,
                                   "PqEventCb: Creating connection context in picoquic_callback_ready");
                transport->HandleNewConnection(transport->CreateConnection(pq_cnx));
            } else {
                // Client - for raw QUIC connections only, WebTransport connections use DefaultWebTransportCallback
                auto connection = transport->GetConnection(conn_id);
                if (connection && connection->GetAPI() == Connection::API::kNativeQuic) {
                    transport->SetStatus(TransportStatus::kReady);
                    transport->OnConnectionStatus(connection, TransportStatus::kReady);
                }
                // WebTransport clients will get status updates via DefaultWebTransportCallback
            }

            (void)picoquic_mark_datagram_ready(pq_cnx, 1);

            break;
        }

        default:
            SPDLOG_LOGGER_DEBUG(transport->logger, "Got event {}", static_cast<int>(fin_or_event));
            break;
    }

    return 0;
}

// Callback for picoquic packet loop
static int
PqLoopCb(picoquic_quic_t* quic, picoquic_packet_loop_cb_enum cb_mode, void* callback_ctx, void* callback_arg)
{
    PicoQuicTransport* transport = static_cast<PicoQuicTransport*>(callback_ctx);
    int ret = 0;

    if (transport == NULL) {
        std::cerr << "picoquic transport was called with NULL transport" << '\n';
        return PICOQUIC_ERROR_UNEXPECTED_ERROR;
    }

    transport->pq_runner_thread_id = std::this_thread::get_id();

    if (transport->Status() == TransportStatus::kDisconnected) {
        return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
    }

    transport->PqRunner();

    switch (cb_mode) {
        case picoquic_packet_loop_ready: {
            SPDLOG_LOGGER_INFO(transport->logger, "packet_loop_ready, waiting for packets");

            if (transport->is_server_mode)
                transport->SetStatus(TransportStatus::kReady);

            if (callback_arg != nullptr) {
                auto* options = static_cast<picoquic_packet_loop_options_t*>(callback_arg);
                options->do_time_check = 1;
            }

            break;
        }

        case picoquic_packet_loop_after_receive:
            //        log_msg << "packet_loop_after_receive";
            //        transport->logger.log(LogLevel::debug, log_msg.str());
            break;

        case picoquic_packet_loop_after_send:
            //        log_msg << "packet_loop_after_send";
            //        transport->logger.log(LogLevel::debug, log_msg.str());
            break;

        case picoquic_packet_loop_port_update:
            SPDLOG_LOGGER_DEBUG(transport->logger, "packet_loop_port_update");
            break;

        case picoquic_packet_loop_time_check: {
            packet_loop_time_check_arg_t* targ = static_cast<packet_loop_time_check_arg_t*>(callback_arg);

            if (targ->delta_t > kPqLoopMaxDelayUs) {
                targ->delta_t = kPqLoopMaxDelayUs;
            }

            if (!transport->pq_loop_prev_time) {
                transport->pq_loop_prev_time = targ->current_time;
            }

            if (targ->current_time - transport->pq_loop_metrics_prev_time >= transport->MetricsSampleIntervalUs()) {
                // Use this time to clean up streams that have been closed
                transport->RemoveClosedStreams();

                if (transport->pq_loop_metrics_prev_time) {
                    transport->EmitMetrics();
                }

                transport->pq_loop_metrics_prev_time = targ->current_time;
            }

            if (targ->current_time - transport->pq_loop_prev_time > kCongestionCheckInterval) {

                transport->CheckConnsForCongestion();

                transport->pq_loop_prev_time = targ->current_time;
            }

            break;
        }

        case picoquic_packet_loop_wake_up:
            // Stop loop if done shutting down
            if (transport->Status() == TransportStatus::kShutdown) {
                return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
            }

            if (transport->Status() == TransportStatus::kShuttingDown) {
                SPDLOG_LOGGER_INFO(transport->logger, "picoquic is shutting down");

                picoquic_cnx_t* close_cnx = picoquic_get_first_cnx(quic);

                if (close_cnx == NULL) {
                    transport->SetStatus(TransportStatus::kShutdown);
                    return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
                }

                while (close_cnx != NULL) {
                    SPDLOG_LOGGER_INFO(
                      transport->logger, "Closing connection id {}", reinterpret_cast<uint64_t>(close_cnx));
                    transport->CloseInternal(transport->GetConnection(reinterpret_cast<uint64_t>(close_cnx)),
                                             AppReasonForClose::kShutdown);
                    close_cnx = picoquic_get_next_cnx(close_cnx);
                }

                transport->SetStatus(TransportStatus::kShutdown);
                return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
            }

            break;

        default:
            // ret = PICOQUIC_ERROR_UNEXPECTED_ERROR;
            SPDLOG_LOGGER_WARN(transport->logger, "pq_loop_cb() does not implement ", std::to_string(cb_mode));
            break;
    }

    return ret;
}

// Helper function to convert WebTransport event enum to string
static const char*
WtEventToString(picohttp_call_back_event_t wt_event)
{
    switch (wt_event) {
        case picohttp_callback_get:
            return "picohttp_callback_get";
        case picohttp_callback_post:
            return "picohttp_callback_post";
        case picohttp_callback_connecting:
            return "picohttp_callback_connecting";
        case picohttp_callback_connect:
            return "picohttp_callback_connect";
        case picohttp_callback_connect_refused:
            return "picohttp_callback_connect_refused";
        case picohttp_callback_connect_accepted:
            return "picohttp_callback_connect_accepted";
        case picohttp_callback_post_data:
            return "picohttp_callback_post_data";
        case picohttp_callback_post_fin:
            return "picohttp_callback_post_fin";
        case picohttp_callback_provide_data:
            return "picohttp_callback_provide_data";
        case picohttp_callback_post_datagram:
            return "picohttp_callback_post_datagram";
        case picohttp_callback_provide_datagram:
            return "picohttp_callback_provide_datagram";
        case picohttp_callback_reset:
            return "picohttp_callback_reset";
        case picohttp_callback_stop_sending:
            return "picohttp_callback_stop_sending";
        case picohttp_callback_deregister:
            return "picohttp_callback_deregister";
        case picohttp_callback_free:
            return "picohttp_callback_free";
        default:
            return "unknown";
    }
}

// Helper to get connection context with logging on failure
static std::shared_ptr<PicoQuicConnection>
GetConnCtxForWT(PicoQuicTransport* transport, std::uint64_t conn_id, picohttp_call_back_event_t wt_event)
{
    auto connection = transport->GetConnection(conn_id);
    if (!connection) {
        SPDLOG_LOGGER_WARN(
          transport->logger, "DefaultWT: {} No connection context for conn_id {}", WtEventToString(wt_event), conn_id);
    }
    return connection;
}

// Helper to get data context from WebTransport stream mapping
static DataContext*
GetDataCtxForWT(const std::shared_ptr<PicoQuicConnection>& connection, uint64_t stream_id)
{
    if (!connection) {
        return nullptr;
    }
    auto stream_to_ctx_it = connection->wt_stream_to_data_ctx.find(stream_id);
    if (stream_to_ctx_it != connection->wt_stream_to_data_ctx.end()) {
        auto data_ctx_it = connection->active_data_contexts.find(stream_to_ctx_it->second);
        if (data_ctx_it != connection->active_data_contexts.end()) {
            return &data_ctx_it->second;
        }
    }
    return nullptr;
}

// Helper to clear data context stream and remove from WebTransport stream mapping
static void
ClearDataCtxStream(const std::shared_ptr<PicoQuicConnection>& connection, uint64_t stream_id)
{
    if (!connection) {
        return;
    }
    auto stream_to_ctx_it = connection->wt_stream_to_data_ctx.find(stream_id);
    if (stream_to_ctx_it != connection->wt_stream_to_data_ctx.end()) {
        auto data_ctx_it = connection->active_data_contexts.find(stream_to_ctx_it->second);
        if (data_ctx_it != connection->active_data_contexts.end()) {
            data_ctx_it->second.streams.erase(stream_id);
        }
        connection->wt_stream_to_data_ctx.erase(stream_to_ctx_it);
    }
}

// Callback for PicoQuic events over MoQ over Webtransport /relay path
static int
DefaultWebTransportCallback(picoquic_cnx_t* cnx,
                            uint8_t* bytes,
                            size_t length,
                            picohttp_call_back_event_t wt_event,
                            h3zero_stream_ctx_t* stream_ctx,
                            void* path_app_ctx)
{
    auto* transport = static_cast<PicoQuicTransport*>(path_app_ctx);
    if (!transport) {
        return -1;
    }

    auto conn_id = reinterpret_cast<std::uint64_t>(cnx);
    int ret = 0;

    switch (wt_event) {
        case picohttp_callback_connecting:
            // Called when initiating WebTransport connect
            SPDLOG_LOGGER_TRACE(
              transport->logger, "DefaultWT: {} for connection {}", WtEventToString(wt_event), conn_id);
            break;

        case picohttp_callback_connect:
            /* A connect has been received on this stream, and could be accepted.
             */
            SPDLOG_LOGGER_TRACE(transport->logger,
                                "DefaultWT: {} connect received on path for connection {}",
                                WtEventToString(wt_event),
                                conn_id);

            if (transport->is_server_mode) {
                // Accept the incoming WebTransport connection
                // This initializes wt_context_, updates internal data structures,
                // and reports OnNewConnection() callback
                ret = transport->AcceptWebTransportConnection(cnx, bytes, length, stream_ctx);
                if (ret != 0) {
                    SPDLOG_LOGGER_ERROR(
                      transport->logger, "DefaultWT: Failed to accept WebTransport connection {}", conn_id);
                }
            }
            break;

        case picohttp_callback_connect_refused:
            SPDLOG_LOGGER_WARN(
              transport->logger, "DefaultWT: {} for connection {}", WtEventToString(wt_event), conn_id);
            if (auto connection = transport->GetConnection(conn_id)) {
                transport->OnConnectionStatus(connection, TransportStatus::kDisconnected);
            }
            break;

        case picohttp_callback_connect_accepted:
            SPDLOG_LOGGER_TRACE(transport->logger,
                                "DefaultWT: {} for connection {}, h3 stream {}",
                                WtEventToString(wt_event),
                                conn_id,
                                stream_ctx->stream_id);

            transport->SetStatus(TransportStatus::kReady);
            if (auto connection = transport->GetConnection(conn_id)) {
                transport->OnConnectionStatus(connection, TransportStatus::kReady);
            }
            break;

        case picohttp_callback_post_data:
        case picohttp_callback_post_fin: {
            // Data received on a stream - similar to picoquic_callback_stream_data in PqEventCb
            if (!stream_ctx) {
                SPDLOG_LOGGER_TRACE(transport->logger, "DefaultWT: {} with null stream_ctx", WtEventToString(wt_event));
                return -1;
            }

            uint64_t stream_id = stream_ctx->stream_id;
            bool is_fin = (wt_event == picohttp_callback_post_fin);

            if (is_fin) {
                SPDLOG_LOGGER_DEBUG(transport->logger,
                                    "DefaultWT: {} conn_id: {} stream_id: {} FIN",
                                    WtEventToString(wt_event),
                                    conn_id,
                                    stream_id);
            }

            SPDLOG_LOGGER_TRACE(transport->logger,
                                "DefaultWT: {} received {} bytes on stream {} for connection {}, is_fin {}",
                                WtEventToString(wt_event),
                                length,
                                stream_id,
                                conn_id,
                                is_fin);

            auto connection = GetConnCtxForWT(transport, conn_id, wt_event);
            if (!connection) {
                return -1;
            }

            auto data_ctx = GetDataCtxForWT(connection, stream_id);

            // For bidir streams that are remotely initiated, create data context if needed
            if (data_ctx == nullptr) {
                // Check if this is a bidir stream (bit 0x2 == 0)
                if ((stream_id & 0x2) == 0) {
                    // Create bidir stream if it wasn't initiated by this instance (remote initiated it)
                    if (((stream_id & 0x1) == 1 && !transport->is_server_mode) ||
                        ((stream_id & 0x0) == 0 && transport->is_server_mode)) {

                        // Create the data context for new bidir streams created by remote side
                        data_ctx = transport->CreateDataContextBiDirRecv(conn_id, stream_id);

                        // Add to WebTransport stream mapping
                        if (data_ctx) {
                            connection->wt_stream_to_data_ctx[stream_id] = data_ctx->data_ctx_id;
                        }
                    }
                }
            }

            // Store the h3zero_stream_ctx_t* for WebTransport streams
            if (data_ctx) {
                auto stream_it = data_ctx->streams.find(stream_id);
                if (stream_it != data_ctx->streams.end() && stream_it->second.wt_stream_ctx == nullptr) {
                    stream_it->second.wt_stream_ctx = stream_ctx;
                }
            }

            // Process received data
            if (length > 0) {
                transport->OnRecvStreamBytes(connection, data_ctx, stream_id, is_fin, std::span{ bytes, length });
            }

            if (is_fin) {
                SPDLOG_LOGGER_TRACE(transport->logger,
                                    "DefaultWT: {} Received FIN for connection{}, stream {}",
                                    WtEventToString(wt_event),
                                    conn_id,
                                    stream_id);

                picoquic_reset_stream_ctx(cnx, stream_id);

                auto rx_buf_it = connection->rx_stream_buffer.find(stream_id);
                if (rx_buf_it != connection->rx_stream_buffer.end()) {
                    rx_buf_it->second.closed = true;
                    transport->OnStreamClosed(
                      connection, stream_id, rx_buf_it->second.rx_ctx, std::nullopt, StreamClosedFlag::kFin);
                }

                if (data_ctx != nullptr) {
                    transport->OnStreamClosed(
                      connection, stream_id, nullptr, data_ctx->data_ctx_id, StreamClosedFlag::kFin);
                }
            }

            break;
        }

        case picohttp_callback_provide_data: {
            // Stack is ready to send data on a stream - similar to picoquic_callback_prepare_to_send in PqEventCb
            if (!stream_ctx) {
                SPDLOG_LOGGER_WARN(transport->logger, "DefaultWT: {} with null stream_ctx", WtEventToString(wt_event));
                return -1;
            }

            uint64_t stream_id = stream_ctx->stream_id;

            SPDLOG_LOGGER_TRACE(transport->logger,
                                "DefaultWT: {} for connection {}, h3 stream {}",
                                WtEventToString(wt_event),
                                conn_id,
                                stream_id);

            auto connection = GetConnCtxForWT(transport, conn_id, wt_event);
            if (!connection) {
                return -1;
            }

            auto data_ctx = GetDataCtxForWT(connection, stream_id);
            if (data_ctx == nullptr) {
                // No data context, nothing to send
                SPDLOG_LOGGER_TRACE(
                  transport->logger, "DefaultWT: {} no data_ctx for stream {}", WtEventToString(wt_event), stream_id);
                break;
            }

            // Check congestion
            if (picoquic_get_cwin(cnx) < kPqCcLowCwin) {
                connection->metrics.cwin_congested++;
            }

            data_ctx->metrics.tx_stream_cb++;

            SPDLOG_LOGGER_TRACE(transport->logger,
                                "DefaultWT: {} Invoking to send stream bytes on stream {}",
                                WtEventToString(wt_event),
                                length,
                                stream_id);

            // Send stream bytes - this will call picoquic_provide_stream_data_buffer internally
            transport->SendStreamBytes(data_ctx, stream_id, bytes, length);
            break;
        }

        case picohttp_callback_post_datagram: {
            // Datagram received
            SPDLOG_LOGGER_TRACE(transport->logger,
                                "DefaultWT: {} received {} bytes for connection {}",
                                WtEventToString(wt_event),
                                length,
                                conn_id);

            if (auto connection = GetConnCtxForWT(transport, conn_id, wt_event)) {
                transport->OnRecvDatagram(connection, bytes, length);
            }
            break;
        }

        case picohttp_callback_provide_datagram: {
            // Stack is ready to send a datagram
            if (auto connection = GetConnCtxForWT(transport, conn_id, wt_event)) {
                connection->metrics.tx_dgram_cb++;
                transport->SendNextDatagram(connection, bytes, length);

                if (picoquic_get_cwin(cnx) < kPqCcLowCwin) {
                    connection->metrics.cwin_congested++;
                }
            }
            break;
        }

        case picohttp_callback_reset: {
            // Stream has been abandoned
            if (!stream_ctx) {
                SPDLOG_LOGGER_WARN(transport->logger, "DefaultWT: {} with null stream_ctx", WtEventToString(wt_event));
                return -1;
            }

            uint64_t stream_id = stream_ctx->stream_id;

            SPDLOG_LOGGER_DEBUG(transport->logger,
                                "DefaultWT: {} for stream {} on connection {}",
                                WtEventToString(wt_event),
                                stream_id,
                                conn_id);

            if (auto connection = transport->GetConnection(conn_id)) {
                auto rx_buf_it = connection->rx_stream_buffer.find(stream_id);
                if (rx_buf_it != connection->rx_stream_buffer.end()) {
                    rx_buf_it->second.closed = true;
                    transport->OnStreamClosed(
                      connection, stream_id, rx_buf_it->second.rx_ctx, std::nullopt, StreamClosedFlag::kReset);
                }

                if (const auto data_ctx = GetDataCtxForWT(connection, stream_id)) {
                    transport->OnStreamClosed(
                      connection, stream_id, nullptr, data_ctx->data_ctx_id, StreamClosedFlag::kReset);
                }

                ClearDataCtxStream(connection, stream_id);
            }

            // Use picowt_reset_stream to properly reset the WebTransport stream
            picowt_reset_stream(cnx, stream_ctx, 0);

            break;
        }

        case picohttp_callback_stop_sending: {
            // Peer wants to abandon receiving on the stream
            if (!stream_ctx) {
                SPDLOG_LOGGER_WARN(transport->logger, "DefaultWT: {} with null stream_ctx", WtEventToString(wt_event));
                return -1;
            }

            uint64_t stream_id = stream_ctx->stream_id;

            SPDLOG_LOGGER_DEBUG(transport->logger,
                                "DefaultWT: {} for stream {} on connection {}",
                                WtEventToString(wt_event),
                                stream_id,
                                conn_id);

            if (auto connection = transport->GetConnection(conn_id)) {
                auto rx_buf_it = connection->rx_stream_buffer.find(stream_id);
                if (rx_buf_it != connection->rx_stream_buffer.end()) {
                    rx_buf_it->second.closed = true;
                    transport->OnStreamClosed(
                      connection, stream_id, rx_buf_it->second.rx_ctx, std::nullopt, StreamClosedFlag::kReset);
                }

                if (const auto data_ctx = GetDataCtxForWT(connection, stream_id)) {
                    transport->OnStreamClosed(
                      connection, stream_id, nullptr, data_ctx->data_ctx_id, StreamClosedFlag::kReset);
                }

                ClearDataCtxStream(connection, stream_id);
            }

            // Use picowt_reset_stream to properly reset the WebTransport stream
            picowt_reset_stream(cnx, stream_ctx, 0);

            break;
        }

        case picohttp_callback_free:
            // Clean up the stream
            SPDLOG_LOGGER_DEBUG(
              transport->logger, "DefaultWT: {} callback for connection {}", WtEventToString(wt_event), conn_id);
            break;

        case picohttp_callback_deregister: {
            // The app context has been removed from the registry.
            // Its references should be removed from streams belonging to this session.
            SPDLOG_LOGGER_DEBUG(
              transport->logger, "DefaultWT: {} callback for connection {}", WtEventToString(wt_event), conn_id);

            transport->DeregisterWebTransport(cnx);

            if (auto connection = transport->GetConnection(conn_id)) {
                transport->OnConnectionStatus(connection, TransportStatus::kDisconnected);
            }

            break;
        }

        default:
            return -1;
    }

    return 0;
}

// ALPN selector function for server to support both raw QUIC and WebTransport
static size_t
PqAlpnSelectCb(picoquic_quic_t* quic, ptls_iovec_t* list, size_t count)
{
    size_t ret = count;
    picoquic_cnx_t* cnx = quic->cnx_in_progress;

    if (cnx == NULL) {
        return -1;
    }

    // Define supported ALPNs
    const char* moq_alpn = kMoqtAlpn;
    const char* h3_alpn = "h3";
    size_t moq_len = strlen(moq_alpn);
    size_t h3_len = strlen(h3_alpn);

    void* default_callback_ctx = picoquic_get_default_callback_context(quic);

    for (size_t i = 0; i < count; i++) {
        // Access the ptls_iovec_t structure using offsets
        // Structure: { void* base; size_t len; }
        void** list_ptr = (void**)list;
        void* base = list_ptr[i * 2];            // base is first element
        size_t len = ((size_t*)list)[i * 2 + 1]; // len is second element

        // Check for MOQ ALPN (raw QUIC)
        if (len == moq_len && memcmp(base, moq_alpn, moq_len) == 0) {
            ret = i;
            // For raw QUIC, we need the PicoQuicTransport pointer, not the HTTP server parameters.
            // The transport pointer is stored in path_table[0].path_app_ctx during server setup.
            auto* server_params = static_cast<picohttp_server_parameters_t*>(default_callback_ctx);
            void* transport_ctx =
              (server_params && server_params->path_table_nb > 0) ? server_params->path_table[0].path_app_ctx : nullptr;
            picoquic_set_callback(cnx, PqEventCb, transport_ctx);
            break;
        }
        // Check for H3 ALPN
        if (len == h3_len && memcmp(base, h3_alpn, h3_len) == 0) {
            picoquic_set_callback(cnx, h3zero_callback, default_callback_ctx);
            ret = i;
            break;
        }
    }

    return ret;
}

/* ============================================================================
 * Public API methods
 * ============================================================================
 */

TransportStatus
PicoQuicTransport::Status() const
{
    return transportStatus_;
}

std::shared_ptr<Connection>
PicoQuicTransport::Start()
{
    uint64_t current_time = picoquic_current_time();

#if 0
    if (debug) {
        debug_set_stream(stderr);
    }
#endif

    if (tconfig_.use_reset_wait_strategy) {
        SPDLOG_LOGGER_INFO(logger, "Using Reset and Wait congestion control strategy");
    }

    // Initialize WebTransport
    if (auto wt_ret = InitializeWebTransportContext(); wt_ret != 0) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to initialize WebTransport");
        return nullptr;
    }

    if (not tconfig_.use_bbr) {
        SPDLOG_LOGGER_INFO(logger, "Using NewReno congestion control");
        (void)picoquic_config_set_option(&config_, picoquic_option_CC_ALGO, "reno");
    }

    // For servers, don't set default ALPN - will use ALPN selector function
    if (!is_server_mode) {
        // Clients use single ALPN based on transport mode
        (void)picoquic_config_set_option(&config_, picoquic_option_ALPN, GetAlpn());
    }

    (void)picoquic_config_set_option(
      &config_, picoquic_option_MAX_CONNECTIONS, std::to_string(tconfig_.max_connections).c_str());

    if (is_server_mode) {
        SPDLOG_LOGGER_DEBUG(logger, "Start: As Server, configuring WebTransport Path Params");

        // Store path items in the class member to ensure memory persists after Start() returns
        wt_config_->path_items = { { serverInfo_.path.c_str(), 6, DefaultWebTransportCallback, this } };

        // Store server_params in class member so it persists for the ALPN callback
        // The ALPN callback uses path_table[0].path_app_ctx to get the transport pointer for raw QUIC
        memset(&wt_config_->server_params, 0, sizeof(picohttp_server_parameters_t));
        wt_config_->server_params.path_table = wt_config_->path_items.data();
        wt_config_->server_params.path_table_nb = wt_config_->path_items.size();
        quic_ctx_ = picoquic_create_and_configure(&config_, NULL, &wt_config_->server_params, current_time, NULL);

        if (quic_ctx_ == NULL) {
            throw TransportException(TransportError::kFailedToCreateQuicInstance);
        }

        picoquic_set_alpn_select_fn(quic_ctx_, PqAlpnSelectCb);
        picoquic_use_unique_log_names(quic_ctx_, 1);
    } else {
        if (connection_api == Connection::API::kWebTransport) {
            SPDLOG_LOGGER_INFO(logger, "Client configured for WebTransport over QUIC");
            quic_ctx_ = picoquic_create_and_configure(&config_, NULL, NULL, current_time, NULL);
        } else {
            SPDLOG_LOGGER_INFO(logger, "Client configured for Raw QUIC");
            quic_ctx_ = picoquic_create_and_configure(&config_, PqEventCb, this, current_time, NULL);
        }
    }

    if (quic_ctx_ == NULL) {
        SPDLOG_LOGGER_CRITICAL(logger, "Unable to create picoquic context, check certificate and key filenames");
        throw PicoQuicException("Unable to create picoquic context");
    }

    if (config_.enable_sslkeylog) {
        if (std::getenv("SSLKEYLOGFILE") == nullptr) {
            SPDLOG_LOGGER_WARN(logger, "Key log enabled but $SSLKEYLOGFILE not set");
        }
        picoquic_set_key_log_file_from_env(quic_ctx_);
    }

    /*
     * TODO doc: Apparently need to set some value to send datagrams. If not set,
     *    max datagram size is zero, preventing sending of datagrams. Setting this
     *    also triggers PMTUD to run. This value will be the initial value.
     */
    picoquic_init_transport_parameters(&local_tp_options_);

    // TODO(tievens): revisit PMTU/GSO, removing this breaks some networks
    local_tp_options_.max_datagram_frame_size = 1280;
    local_tp_options_.max_idle_timeout = tconfig_.idle_timeout_ms;
    local_tp_options_.max_ack_delay = 100000;
    local_tp_options_.min_ack_delay = 1000;

    if (tconfig_.initial_max_stream_data > 0) {
        local_tp_options_.initial_max_stream_data_uni = tconfig_.initial_max_stream_data;
        local_tp_options_.initial_max_stream_data_bidi_local = tconfig_.initial_max_stream_data;
        local_tp_options_.initial_max_stream_data_bidi_remote = tconfig_.initial_max_stream_data;
    }

    picoquic_set_default_handshake_timeout(quic_ctx_, (tconfig_.idle_timeout_ms * 1000) / 2);
    picoquic_set_default_tp(quic_ctx_, &local_tp_options_);

    // Must run after set_default_tp; WebTransport requires reset_stream_at in transport parameters.
    if (is_server_mode || connection_api == Connection::API::kWebTransport) {
        picowt_set_default_transport_parameters(quic_ctx_);
    }

    picoquic_set_default_idle_timeout(quic_ctx_, tconfig_.idle_timeout_ms);
    picoquic_set_default_priority(quic_ctx_, 2);
    picoquic_set_default_datagram_priority(quic_ctx_, 1);

    SPDLOG_LOGGER_INFO(logger, "Setting idle timeout to {}ms", tconfig_.idle_timeout_ms);

    picoquic_runner_queue_.SetLimit(tconfig_.callback_queue_size);

    cbNotifyQueue_.SetLimit(tconfig_.callback_queue_size);
    cbNotifyThread_ = std::thread(&PicoQuicTransport::CbNotifier, this);

    if (!tconfig_.quic_qlog_path.empty()) {
        SPDLOG_LOGGER_INFO(logger, "Enabling qlog using '{}' path", tconfig_.quic_qlog_path);
        picoquic_set_qlog(quic_ctx_, tconfig_.quic_qlog_path.c_str());
    }

    std::ostringstream log_msg;

    if (is_server_mode) {

        SPDLOG_LOGGER_INFO(logger, "Starting server, listening on {}:{}", serverInfo_.host_or_ip, serverInfo_.port);
        Server();

    } else {
        SPDLOG_LOGGER_INFO(logger, "Connecting to server {}:{}", serverInfo_.host_or_ip, serverInfo_.port);

        if (ClientLoop()) {
            return StartClient();
        }
    }

    return nullptr;
}

bool
PicoQuicTransport::GetPeerAddrInfo(const std::shared_ptr<Connection>& connection, sockaddr_storage* addr)
{
    std::lock_guard<std::mutex> _(state_mutex_);

    std::memcpy(addr, &std::static_pointer_cast<PicoQuicConnection>(connection)->peer_addr, sizeof(sockaddr_storage));

    return true;
}

TransportError
PicoQuicTransport::Enqueue(const std::shared_ptr<Connection>& connection,
                           const std::uint64_t& data_ctx_id,
                           std::uint64_t stream_id,
                           std::shared_ptr<const std::vector<uint8_t>> bytes,
                           const uint8_t priority,
                           const uint32_t ttl_ms,
                           [[maybe_unused]] const uint32_t delay_ms,
                           const EnqueueFlags flags)
{
    SPDLOG_LOGGER_TRACE(logger,
                        "Enqueue conn_id: {} data_ctx_id: {} stream_id: {} size: {}",
                        conn_id,
                        data_ctx_id,
                        stream_id,
                        bytes->size());

    std::lock_guard<std::mutex> _(state_mutex_);

    const auto pq_conn = std::static_pointer_cast<PicoQuicConnection>(connection);
    const auto data_ctx_it = pq_conn->active_data_contexts.find(data_ctx_id);
    if (data_ctx_it == pq_conn->active_data_contexts.end()) {
        return TransportError::kInvalidDataContextId;
    }

    data_ctx_it->second.metrics.enqueued_objs++;

    if (flags.use_reliable) {
        auto& streams = data_ctx_it->second.streams;

        decltype(streams.begin()) stream_it;

        if (data_ctx_it->second.is_bidir || (stream_id == 0 && priority == 0)) {
            if (streams.empty()) {
                return TransportError::kInvalidStreamId;
            }

            stream_id = streams.begin()->first;
            stream_it = streams.begin();

        } else {
            stream_it = streams.find(stream_id);
            if (stream_it == streams.end()) {
                return TransportError::kInvalidStreamId;
            }
        }

        auto& stream = stream_it->second;

        stream.priority = priority; // Match object priority for next stream create

        StreamAction stream_action{ StreamAction::kNoAction };

        std::lock_guard __(*stream.tx_data);

        if (flags.close_stream) {
            if (flags.use_reset) {
                stream_action = StreamAction::kCloseStreamUseReset;
            } else {
                stream_action = StreamAction::kCloseStreamUseFin;
            }
        }

        if (flags.clear_tx_queue) {
            data_ctx_it->second.metrics.tx_queue_discards += stream.tx_data->Size();
            stream.tx_data->Clear();
        }

        ConnData cd{
            connection->GetID(), data_ctx_id,      priority,
            stream_action,       std::move(bytes), static_cast<uint64_t>(tick_service_->get().count()),
        };

        stream.tx_data->Push(std::move(cd), ttl_ms, 0);

        if (stream.tx_data->Size() < 10) {
            RunPqFunction([=, this]() {
                MarkStreamActive(pq_conn, data_ctx_id, stream_id);
                return 0;
            });
        }
    } else { // datagram
        ConnData cd{
            connection->GetID(),     data_ctx_id,      priority,
            StreamAction::kNoAction, std::move(bytes), static_cast<uint64_t>(tick_service_->get().count()),
        };

        std::lock_guard __(*pq_conn->dgram_tx_data);

        pq_conn->dgram_tx_data->Push(0 /* FIXME: Phony group number */, std::move(cd), ttl_ms, priority, 0);

        if (!pq_conn->mark_dgram_ready) {
            pq_conn->mark_dgram_ready = true;

            RunPqFunction([=, this]() {
                MarkDgramReady(pq_conn);
                return 0;
            });
        }
    }

    return TransportError::kNone;
}

std::shared_ptr<StreamRxContext>
PicoQuicTransport::GetStreamRxContext(const std::shared_ptr<Connection>& connection, uint64_t stream_id)
{
    std::lock_guard<std::mutex> _(state_mutex_);

    const auto pq_conn = std::static_pointer_cast<PicoQuicConnection>(connection);
    const auto sbuf_it = pq_conn->rx_stream_buffer.find(stream_id);
    if (sbuf_it != pq_conn->rx_stream_buffer.end()) {
        return sbuf_it->second.rx_ctx;
    }

    throw TransportException(TransportError::kInvalidStreamId);
}

std::shared_ptr<const std::vector<uint8_t>>
PicoQuicTransport::Dequeue(const std::shared_ptr<Connection>& connection,
                           [[maybe_unused]] std::optional<std::uint64_t> data_ctx_id)
{
    std::lock_guard<std::mutex> _(state_mutex_);

    auto data = std::static_pointer_cast<PicoQuicConnection>(connection)->dgram_rx_data->Pop();
    if (data.has_value()) {
        return *data;
    }

    return {};
}

std::uint64_t
PicoQuicTransport::CreateDataContext(const std::shared_ptr<Connection>& connection,
                                     bool use_reliable_transport,
                                     uint8_t priority,
                                     bool bidir)
{
    std::unique_lock lock(state_mutex_);

    auto pri_initial = priority;
    priority <<= 1;

    if (pri_initial > 127) {
        priority += 64;
    }

    const auto pq_conn = std::static_pointer_cast<PicoQuicConnection>(connection);
    const auto [data_ctx_it, is_new] = pq_conn->active_data_contexts.emplace(pq_conn->next_data_ctx_id, DataContext{});

    if (is_new) {
        // Init context
        data_ctx_it->second.conn_id = connection->GetID();
        data_ctx_it->second.is_bidir = bidir;
        data_ctx_it->second.data_ctx_id = pq_conn->next_data_ctx_id++; // Set and bump next data_ctx_id

        data_ctx_it->second.uses_reset_wait = tconfig_.use_reset_wait_strategy;

        if (!use_reliable_transport) {
            picoquic_set_datagram_priority(pq_conn->pq_cnx, priority);
            SPDLOG_LOGGER_DEBUG(logger,
                                "Created DGRAM data context id: {} pri: {}",
                                data_ctx_it->second.data_ctx_id,
                                static_cast<int>(priority));
        }
    }

    return data_ctx_it->second.data_ctx_id;
}

void
PicoQuicTransport::Close(const std::shared_ptr<Connection>& connection, AppReasonForClose app_reason)
{
    RunPqFunction([=, this]() {
        CloseInternal(connection, app_reason);

        return 0;
    });
}

void
PicoQuicTransport::CloseInternal(const std::shared_ptr<Connection>& connection, AppReasonForClose app_reason)
{
    std::unique_lock<std::mutex> lock(state_mutex_);

    if (!connection || !connections_.contains(connection->GetID())) {
        return;
    }

    const auto pq_conn = std::static_pointer_cast<PicoQuicConnection>(connection);

    // Clear all stream TX queues and RX buffers to release shared pointers
    for (auto& [data_ctx_id, data_ctx] : pq_conn->active_data_contexts) {
        for (auto& [stream_id, stream_ctx] : data_ctx.streams) {
            if (stream_ctx.tx_data) {
                {
                    std::lock_guard __(*stream_ctx.tx_data);
                    stream_ctx.tx_data->Clear();
                }
            }
            stream_ctx.tx_object = nullptr;
        }
    }

    // Clear all RX stream buffers to release shared pointers
    for (auto& [stream_id, rx_buf] : pq_conn->rx_stream_buffer) {
        if (rx_buf.rx_ctx) {
            rx_buf.rx_ctx->data_queue.Clear();
        }
    }

    // Clear datagram RX and TX queues and reset shared pointers
    if (pq_conn->dgram_rx_data) {
        pq_conn->dgram_rx_data.reset();
    }
    if (pq_conn->dgram_tx_data) {
        {
            std::lock_guard _(*pq_conn->dgram_tx_data);
            pq_conn->dgram_tx_data->Clear();
        }
    }

    // Remove pointer references in picoquic for active streams
    for (const auto& [stream_id, rx_buf] : pq_conn->rx_stream_buffer) {
        picoquic_mark_active_stream(pq_conn->pq_cnx, stream_id, 0, NULL);
        picoquic_unlink_app_stream_ctx(pq_conn->pq_cnx, stream_id);

        if (!rx_buf.closed) {
            picoquic_reset_stream(pq_conn->pq_cnx, stream_id, 0);
        }
    }

    // Only one datagram context is per connection, if it's deleted, then the connection is to be terminated
    // TODO(trigaux): Figure out if this logic can live exclusively in Transport here instead of spread to Session.
    switch (app_reason) {
        case AppReasonForClose::kRemoteRequestClose:
            OnConnectionStatus(pq_conn, TransportStatus::kRemoteRequestClose);
            break;
        case AppReasonForClose::kIdleTimeout:
            OnConnectionStatus(pq_conn, TransportStatus::kIdleTimeout);
            break;
        case AppReasonForClose::kShutdown:
            OnConnectionStatus(pq_conn, TransportStatus::kShutdown);
            break;
        default:
            OnConnectionStatus(pq_conn, TransportStatus::kRemoteRequestClose);
            break;
    }

    if (not is_server_mode) {
        SetStatus(TransportStatus::kShutdown);
    }

    // Cleanup client-owned WebTransport h3_ctx before closing connection
    // Server-side h3_ctx is managed by h3zero library and shared across connections
    if (pq_conn->wt_h3_ctx_owned && pq_conn->wt_h3_ctx) {
        SPDLOG_LOGGER_DEBUG(logger, "Cleaning up client-owned h3_ctx for connection {}", connection->GetID());
        // Note: h3zero_callback_delete_context may not exist in all versions
        // The h3zero library typically cleans this up automatically on connection close
        // So we just mark it as null here
        pq_conn->wt_h3_ctx = nullptr;
    }

    picoquic_close(pq_conn->pq_cnx, static_cast<uint64_t>(app_reason));

    connections_.erase(connection->GetID());

    lock.unlock();

    if (OnConnectionClosed) {
        OnConnectionClosed(connection);
    }
}

/* ============================================================================
 * Public internal methods used by picoquic
 * ============================================================================
 */

std::shared_ptr<PicoQuicConnection>
PicoQuicTransport::GetConnection(const std::uint64_t& conn_id)
{
    // Locate the specified transport connection context
    auto it = connections_.find(conn_id);

    // If not found, return empty context
    if (it == connections_.end())
        return nullptr;

    return it->second;
}

const std::shared_ptr<PicoQuicConnection>&
PicoQuicTransport::CreateConnection(picoquic_cnx_t* pq_cnx, Connection::API api)
{
    /*
     * @note: This is thread safe because picoquic network thread is the only one that calls this
     */

    sockaddr* addr;

    // For servers, determine transport mode based on negotiated ALPN
    if (is_server_mode) {
        const char* negotiated_alpn = picoquic_tls_get_negotiated_alpn(pq_cnx);
        if (negotiated_alpn) {
            if (strcmp(negotiated_alpn, webtransport_alpn) == 0) {
                api = Connection::API::kWebTransport;
                SPDLOG_LOGGER_INFO(logger, "Server connection using WebTransport (ALPN: {})", negotiated_alpn);
            } else if (strcmp(negotiated_alpn, kMoqtAlpn) == 0) {
                api = Connection::API::kNativeQuic;
                SPDLOG_LOGGER_INFO(logger, "Server connection using raw QUIC (ALPN: {})", negotiated_alpn);
            } else {
                api = Connection::API::kNativeQuic; // Default fallback
                SPDLOG_LOGGER_WARN(logger, "Unknown ALPN: {}, defaulting to raw QUIC", negotiated_alpn);
            }
        } else {
            api = Connection::API::kNativeQuic; // Default fallback
            SPDLOG_LOGGER_WARN(logger, "No ALPN negotiated, defaulting to raw QUIC");
        }
    }

    auto [conn_it, is_new] = connections_.try_emplace(reinterpret_cast<std::uint64_t>(pq_cnx),
                                                      std::make_shared<PicoQuicConnection>(pq_cnx, api));
    const auto& connection = conn_it->second;

    picoquic_get_peer_addr(pq_cnx, &addr);
    std::memset(connection->peer_addr_text, 0, sizeof(connection->peer_addr_text));
    std::memcpy(&connection->peer_addr, addr, sizeof(connection->peer_addr));

    switch (addr->sa_family) {
        case AF_INET:
            (void)inet_ntop(AF_INET,
                            &reinterpret_cast<struct sockaddr_in*>(addr)->sin_addr,
                            /*(const void*)(&((struct sockaddr_in*)addr)->sin_addr),*/
                            connection->peer_addr_text,
                            sizeof(connection->peer_addr_text));
            connection->peer_port = ntohs(((struct sockaddr_in*)addr)->sin_port); // NOLINT (include)
            break;

        case AF_INET6:
            (void)inet_ntop(AF_INET6,
                            &reinterpret_cast<struct sockaddr_in6*>(addr)->sin6_addr,
                            /*(const void*)(&((struct sockaddr_in6*)addr)->sin6_addr), */
                            connection->peer_addr_text,
                            sizeof(connection->peer_addr_text));
            connection->peer_port = ntohs(((struct sockaddr_in6*)addr)->sin6_port);
            break;
    }

    if (is_new) {
        SPDLOG_LOGGER_INFO(logger, "Created new connection context for conn_id: {}", connection->GetID());

        connection->dgram_rx_data->SetLimit(tconfig_.time_queue_rx_size);
        connection->dgram_tx_data = std::make_shared<PriorityQueue<ConnData>>(tconfig_.time_queue_max_duration,
                                                                              tconfig_.time_queue_bucket_interval,
                                                                              tick_service_,
                                                                              tconfig_.time_queue_init_queue_size);
    }

    return connection;
}

PicoQuicTransport::PicoQuicTransport(const TransportRemote& server,
                                     const TransportConfig& tcfg,
                                     bool is_server_mode,
                                     std::shared_ptr<timeq::tick_service> tick_service,
                                     std::shared_ptr<spdlog::logger> logger,
                                     Connection::API connection_api)
  : logger(std::move(logger))
  , is_server_mode(is_server_mode)
  , connection_api(connection_api)
  , stop_(false)
  , transportStatus_(TransportStatus::kConnecting)
  , serverInfo_(server)
  , tconfig_(tcfg)
  , tick_service_(std::move(tick_service))
{
    debug = tcfg.debug;

    picoquic_config_init(&config_);

    if (is_server_mode && tcfg.tls_cert_filename.empty()) {
        throw InvalidConfigException("Missing cert filename");
    } else if (!tcfg.tls_cert_filename.empty()) {
        (void)picoquic_config_set_option(&config_, picoquic_option_CERT, tcfg.tls_cert_filename.c_str());

        if (!tcfg.tls_key_filename.empty()) {
            (void)picoquic_config_set_option(&config_, picoquic_option_KEY, tcfg.tls_key_filename.c_str());
        } else {
            throw InvalidConfigException("Missing cert key filename");
        }
    }
    if (tcfg.ssl_keylog == true) {
        (void)picoquic_config_set_option(&config_, picoquic_option_SSLKEYLOG, "1");
    }
}

PicoQuicTransport::~PicoQuicTransport()
{
    SetStatus(TransportStatus::kShuttingDown);

    // Cleanup per-connection WebTransport contexts
    // Note: h3zero library handles h3_ctx cleanup for server-side connections
    // For client connections that own their h3_ctx, cleanup is handled in Close()

    Shutdown();
}

void
PicoQuicTransport::SetStatus(TransportStatus status)
{
    transportStatus_ = status;
}

DataContext*
PicoQuicTransport::CreateDataContextBiDirRecv(std::uint64_t conn_id, uint64_t stream_id)
{
    std::lock_guard<std::mutex> _(state_mutex_);

    const auto conn_it = connections_.find(conn_id);
    if (conn_it == connections_.end()) {
        SPDLOG_LOGGER_ERROR(logger, "Invalid conn_id: {}, cannot create data context", conn_id);
        return nullptr;
    }

    auto [data_ctx_it, is_new] = conn_it->second->active_data_contexts.try_emplace(conn_it->second->next_data_ctx_id);

    if (is_new) {
        data_ctx_it->second.conn_id = conn_id;
        data_ctx_it->second.is_bidir = true;
        data_ctx_it->second.data_ctx_id = conn_it->second->next_data_ctx_id++; // Set and bump next data_ctx_id

        DataContext::StreamContext stream;
        stream.tx_data = std::make_unique<SafeTimeQueue<ConnData>>(tconfig_.time_queue_max_duration,
                                                                   tconfig_.time_queue_bucket_interval,
                                                                   tick_service_,
                                                                   tconfig_.time_queue_init_queue_size);
        data_ctx_it->second.streams[stream_id] = std::move(stream);

        SPDLOG_LOGGER_INFO(logger,
                           "Created new bidir data context conn_id: {} data_ctx_id: {} stream_id: {}",
                           conn_id,
                           data_ctx_it->second.data_ctx_id,
                           stream_id);

        return &data_ctx_it->second;
    }

    return nullptr;
}

int
PicoQuicTransport::PqRunner()
{
    if (picoquic_runner_queue_.Empty()) {
        return 0;
    }

    // note: check before running move of optional, which is more CPU taxing when empty
    while (auto cb = picoquic_runner_queue_.Pop()) {
        try {
            if (auto ret = (*cb)()) {
                SPDLOG_LOGGER_ERROR(logger, "PQ function resulted in error: {}", ret);
                return ret;
            }
        } catch (const std::exception& e) {
            SPDLOG_LOGGER_ERROR(
              logger, "Caught exception running callback via notify thread (error={}), ignoring", e.what());
            // TODO(tievens): Add metrics to track if this happens
        }
    }

    return 0;
}

void
PicoQuicTransport::DeleteDataContextInternal(const std::shared_ptr<PicoQuicConnection>& connection,
                                             std::uint64_t data_ctx_id,
                                             bool delete_on_empty)
{
    const auto data_ctx_it = connection->active_data_contexts.find(data_ctx_id);
    if (data_ctx_it == connection->active_data_contexts.end())
        return;

    const auto& streams = data_ctx_it->second.streams;
    SPDLOG_LOGGER_DEBUG(logger,
                        "Delete data context {} in conn_id: {} doe: {} / {} stream count: {}",
                        data_ctx_id,
                        connection->GetID(),
                        delete_on_empty,
                        data_ctx_it->second.delete_on_empty,
                        streams.size());

    if (delete_on_empty && !streams.empty()) {
        data_ctx_it->second.delete_on_empty = true;
        SPDLOG_LOGGER_DEBUG(
          logger, "Delete data context {} in conn_id: {} using delete on empty", data_ctx_id, connection->GetID());

        // Delegate removal of stream to SendStreamBytes() to ensure all data is transmitted before closing stream
        void* stream_ctx = nullptr;
        for (const auto& stream : streams) {

            if (connection->GetAPI() == Connection::API::kWebTransport) {
                stream_ctx = stream.second.wt_stream_ctx;
            } else {
                // For raw QUIC, pass the DataContext pointer
                stream_ctx = &data_ctx_it->second;
            }

            picoquic_mark_active_stream(connection->pq_cnx, stream.first, 1, stream_ctx);
        }

    } else {
        SPDLOG_LOGGER_DEBUG(logger, "Delete data context {} in conn_id: {}", data_ctx_id, connection->GetID());

        std::vector<std::uint64_t> stream_ids;
        stream_ids.reserve(streams.size());
        for (const auto& [stream_id, _] : streams) {
            stream_ids.push_back(stream_id);
        }

        for (const auto& stream_id : stream_ids) {
            CloseStream(connection, &data_ctx_it->second, stream_id, false);
        }

        connection->active_data_contexts.erase(data_ctx_it);
    }
}

void
PicoQuicTransport::DeleteDataContext(const std::shared_ptr<Connection>& connection,
                                     std::uint64_t data_ctx_id,
                                     bool delete_on_empty)
{
    if (data_ctx_id == 0) {
        return; // use close() instead of deleting default/datagram context
    }

    /*
     * Race conditions exist with picoquic thread callbacks that will cause a problem if the context (pointer context)
     *    is deleted outside of the picoquic thread. Below schedules the delete to be done within the picoquic thread.
     */
    RunPqFunction([=, this]() {
        DeleteDataContextInternal(
          std::static_pointer_cast<PicoQuicConnection>(connection), data_ctx_id, delete_on_empty);
        return 0;
    });
}

void
PicoQuicTransport::SendNextDatagram(const std::shared_ptr<PicoQuicConnection>& connection,
                                    uint8_t* bytes_ctx,
                                    size_t max_len)
{
    if (bytes_ctx == nullptr || connection->dgram_tx_data == nullptr) {
        return;
    }

    const bool is_webtransport = connection->GetAPI() == Connection::API::kWebTransport;

    // Helper lambda to get datagram buffer based on transport mode
    auto provide_buffer = [is_webtransport, bytes_ctx](size_t length, bool more_data) -> uint8_t* {
        if (is_webtransport) {
            return h3zero_provide_datagram_buffer(bytes_ctx, length, more_data ? 1 : 0);
        } else {
            return picoquic_provide_datagram_buffer_ex(
              bytes_ctx, length, more_data ? picoquic_datagram_active_any_path : picoquic_datagram_not_active);
        }
    };

    std::lock_guard _(*connection->dgram_tx_data);

    const auto [out_data, expired] = connection->dgram_tx_data->Front();
    if (out_data.has_value()) {
        const auto data_ctx_it = connection->active_data_contexts.find(out_data->get().data_ctx_id);
        if (data_ctx_it == connection->active_data_contexts.end()) {
            SPDLOG_LOGGER_DEBUG(logger,
                                "send_next_dgram has no data context conn_id: {} data len: {} dropping",
                                connection->GetID(),
                                out_data->get().data->size());
            connection->metrics.tx_dgram_drops++;
            return;
        }

        if (out_data->get().data == nullptr || out_data->get().data->size() == 0) {
            SPDLOG_LOGGER_ERROR(logger,
                                "conn_id: {} data_ctx_id: {} has ZERO data size",
                                data_ctx_it->second.conn_id,
                                data_ctx_it->second.data_ctx_id);
            connection->dgram_tx_data->Pop();
            return;
        }

        data_ctx_it->second.metrics.tx_queue_expired += expired;

        if (out_data->get().data->size() <= max_len) {

            data_ctx_it->second.metrics.tx_object_duration_us.AddValue(
              static_cast<uint64_t>(tick_service_->get().count()) - out_data->get().tick_microseconds);
            data_ctx_it->second.metrics.tx_dgrams_bytes += out_data->get().data->size();
            data_ctx_it->second.metrics.tx_dgrams++;

            bool more_data = !connection->dgram_tx_data->Empty();
            uint8_t* buf = provide_buffer(out_data->get().data->size(), more_data);

            if (buf != nullptr) {
                std::memcpy(buf, out_data->get().data->data(), out_data->get().data->size());
            }

            connection->dgram_tx_data->Pop();
        } else {
            RunPqFunction([this, connection]() {
                MarkDgramReady(connection);
                return 0;
            });

            /* TODO(tievens): picoquic_prepare_stream_and_datagrams() appears to ignore the
             *     below unless data was sent/provided
             */
            provide_buffer(0, true);
        }
    } else {
        provide_buffer(0, false);
    }
}

void
PicoQuicTransport::SendStreamBytes(DataContext* data_ctx, std::uint64_t stream_id, uint8_t* bytes_ctx, size_t max_len)
{
    if (bytes_ctx == NULL) {
        return;
    }

    auto stream_it = data_ctx->streams.find(stream_id);
    if (stream_it == data_ctx->streams.end()) {
        SPDLOG_LOGGER_WARN(logger,
                           "SendStreamBytes conn_id: {} data_ctx_id: {} stream_id: {} bytes_len: {}, stream not found",
                           data_ctx->conn_id,
                           data_ctx->data_ctx_id,
                           stream_id,
                           max_len);
        return;
    }

    auto& stream_ctx = stream_it->second;

    if (stream_ctx.tx_data == nullptr) {
        SPDLOG_LOGGER_WARN(logger,
                           "SendStreamBytes conn_id: {} data_ctx_id: {} stream_id: {} has no TX queue, skipping",
                           data_ctx->conn_id,
                           data_ctx->data_ctx_id,
                           stream_id);
        return;
    }

    SPDLOG_LOGGER_TRACE(logger,
                        "SendStreamBytes conn_id: {} data_ctx_id: {} stream_id: {} bytes_len: {}",
                        data_ctx->conn_id,
                        data_ctx->data_ctx_id,
                        stream_id,
                        max_len);

    uint32_t data_len = 0; /// Length of data to follow the 4 byte length
    size_t offset = 0;
    int is_still_active = 0;

    CheckCallbackDelta(data_ctx);

    const auto& connection = GetConnection(data_ctx->conn_id);

    bool should_reset = false;
    defer({
        const bool empty = [&] {
            std::lock_guard _(*stream_ctx.tx_data);
            return stream_ctx.tx_data->Empty() && stream_ctx.tx_object == nullptr;
        }();

        if (should_reset) {
            CloseStream(connection, data_ctx->data_ctx_id, stream_id, true);
            if (data_ctx->delete_on_empty && empty) {
                DeleteDataContextInternal(connection, data_ctx->data_ctx_id, false);
            }
            return;
        }

        if (data_ctx->delete_on_empty && empty) {
            DeleteDataContextInternal(connection, data_ctx->data_ctx_id, false);
        } else if (stream_ctx.close_on_empty && empty) {
            CloseStream(connection, data_ctx->data_ctx_id, stream_id, false);
        }
    });

    std::lock_guard _(*stream_ctx.tx_data);

    if (data_ctx != nullptr && stream_ctx.tx_reset_wait_discard) { // Drop TX objects till next reset/new stream

        const auto [value, _] = stream_ctx.tx_data->Front();
        if (value.has_value() && value->get().data) {
            data_ctx->metrics.tx_queue_discards++;

            if (value->get().stream_action == StreamAction::kCloseStreamUseFin ||
                value->get().stream_action == StreamAction::kCloseStreamUseReset) {

                std::lock_guard<std::mutex> _(state_mutex_);
                const auto connection = GetConnection(data_ctx->conn_id);
                if (!connection->is_congested) {
                    stream_ctx.tx_reset_wait_discard = false;
                    stream_ctx.ResetTxObject();
                } else {
                    stream_ctx.tx_data->Pop(); // discard when in current stream

                    if (!stream_ctx.tx_data->Empty()) {
                        RunPqFunction([=, this, data_ctx_id = data_ctx->data_ctx_id]() {
                            MarkStreamActive(connection, data_ctx_id, stream_id);
                            return 0;
                        });
                    }
                    return;
                }
            }
        }
    }

    if (stream_ctx.tx_object == nullptr) {
        SPDLOG_LOGGER_TRACE(logger,
                            "SendStreamBytes conn_id: {} data_ctx_id: {} stream_tx_object is nullptr",
                            data_ctx->conn_id,
                            data_ctx->data_ctx_id);

        auto obj = stream_ctx.tx_data->PopFront();

        if (obj.expired) {
            data_ctx->metrics.tx_queue_expired += obj.expired;
            SPDLOG_LOGGER_DEBUG(
              logger,
              "Send stream objects expired; conn_id: {} data_ctx_id: {} stream_id: {} expired: {} queue_size: {}",
              data_ctx->conn_id,
              data_ctx->data_ctx_id,
              stream_id,
              obj.expired,
              stream_ctx.tx_data->Size());

            should_reset = true;
            return;
        }

        if (!obj.value.has_value()) {
            return; // empty object means empty queue, nothing to do
        }

        auto conn_data = obj.value;

        switch (conn_data->stream_action) {
            case StreamAction::kCloseStreamUseFin:
                stream_ctx.close_on_empty = true;
                break;
            case StreamAction::kCloseStreamUseReset:
                stream_ctx.close_on_empty = false;
                stream_ctx.close_using_reset = true;
                should_reset = true;
                break;
            case StreamAction::kNoAction:
                break;
        }

        if (conn_data.has_value() && conn_data->data && conn_data->data->size() > 0) {

            stream_ctx.tx_object_offset = 0;
            data_ctx->metrics.tx_stream_objects++;
            data_ctx->metrics.tx_object_duration_us.AddValue(static_cast<uint64_t>(tick_service_->get().count()) -
                                                             obj.value->tick_microseconds);

            if (obj.value->stream_action != StreamAction::kNoAction) {
                SPDLOG_LOGGER_TRACE(
                  logger,
                  "Object wants New Stream conn_id: {} data_ctx_id: {} stream_id: {}, object size: {} queue_size: {}",
                  data_ctx->conn_id,
                  data_ctx->data_ctx_id,
                  *data_ctx->current_stream_id,
                  obj.value.data->size(),
                  data_ctx->tx_data->Size());
            }

            stream_ctx.tx_object = std::move(obj.value->data);

        } else {
            picoquic_provide_stream_data_buffer(
              bytes_ctx, 0, stream_ctx.close_on_empty, not stream_ctx.tx_data->Empty());
            return;
        }
    }

    data_len = stream_ctx.tx_object->size() - stream_ctx.tx_object_offset;
    offset = stream_ctx.tx_object_offset;

    if (data_len > max_len) {
        stream_ctx.tx_object_offset += max_len;
        data_len = max_len;
        is_still_active = 1;

    } else {
        stream_ctx.tx_object_offset = 0;
    }

    data_ctx->metrics.tx_stream_bytes += data_len;

    if (!is_still_active && !stream_ctx.tx_data->Empty())
        is_still_active = 1;

    uint8_t* buf = nullptr;

    buf = picoquic_provide_stream_data_buffer(bytes_ctx, data_len, 0, is_still_active);

    if (buf == NULL) {
        // Error allocating memory to write
        SPDLOG_LOGGER_ERROR(logger,
                            "conn_id: {} data_ctx_id: {} priority: {} unable to allocate pq buffer size: {}",
                            data_ctx->conn_id,
                            data_ctx->data_ctx_id,
                            static_cast<int>(stream_ctx.priority),
                            data_len);
        return;
    }

    // Write data
    std::memcpy(buf, stream_ctx.tx_object->data() + offset, data_len);

    if (stream_ctx.tx_object_offset == 0 && stream_ctx.tx_object != nullptr) {
        // Zero offset at this point means the object was fully sent
        stream_ctx.ResetTxObject();
    }
}

void
PicoQuicTransport::OnConnectionStatus(const std::shared_ptr<PicoQuicConnection>& connection,
                                      const TransportStatus status)
{
    if (!connection) {
        return;
    }

    SPDLOG_LOGGER_DEBUG(
      logger, "Connection changed conn_id: {} to status: {}", connection->GetID(), static_cast<int>(status));

    if (status == TransportStatus::kReady) {
        SPDLOG_LOGGER_INFO(logger, "Connection established to server {}", connection->peer_addr_text);
    }

    cbNotifyQueue_.Push([connection, status]() { connection->SetStatus(static_cast<Connection::Status>(status)); });
}

void
PicoQuicTransport::HandleNewConnection(const std::shared_ptr<PicoQuicConnection>& connection)
{
    SPDLOG_LOGGER_INFO(logger,
                       "New Connection {} port: {} conn_id: {}",
                       connection->peer_addr_text,
                       connection->peer_port,
                       connection->GetID());

    TransportRemote remote{ .host_or_ip = connection->peer_addr_text,
                            .port = connection->peer_port,
                            .proto = TransportProtocol::kQuic };

    picoquic_enable_keep_alive(connection->pq_cnx, tconfig_.idle_timeout_ms * 500);
    picoquic_set_feedback_loss_notification(connection->pq_cnx, 1);

#if 0
    // Setup WebTransport for server connections if needed
    if (connection->GetAPI() == Connection::API::kWebTransport) {
        if (auto wt_ret = SetupWebTransportConnection(connection->pq_cnx); wt_ret != 0) {
            SPDLOG_LOGGER_ERROR(logger, "Failed to setup WebTransport connection for server");
        }
    } else {
        picoquic_set_callback(connection->pq_cnx, PqEventCb, this);
    }
#endif

    if (tconfig_.quic_priority_limit > 0) {
        SPDLOG_LOGGER_INFO(
          logger, "Setting priority bypass limit to {}", static_cast<int>(tconfig_.quic_priority_limit));
        picoquic_set_priority_limit_for_bypass(connection->pq_cnx, tconfig_.quic_priority_limit);
    }

    if (OnNewConnection) {
        OnNewConnection(connection);
    }
}

void
PicoQuicTransport::OnRecvDatagram(const std::shared_ptr<PicoQuicConnection>& connection, uint8_t* bytes, size_t length)
try {
    if (length == 0) {
        return;
    }

    if (connection == nullptr) {
        SPDLOG_LOGGER_WARN(logger, "DGRAM received with NULL connection context; dropping length: {}", length);
        return;
    }

    connection->dgram_rx_data->Push(std::make_shared<const std::vector<uint8_t>>(bytes, bytes + length));
    connection->metrics.rx_dgrams++;
    connection->metrics.rx_dgrams_bytes += length;

    if (cbNotifyQueue_.Size() > 1000) {
        SPDLOG_LOGGER_INFO(logger, "on_recv_datagram cbNotifyQueue size {}", cbNotifyQueue_.Size());
    }

    if (connection->dgram_rx_data->Size() < 10 &&
        !cbNotifyQueue_.Push([=, this]() { connection->OnRecvDgram(std::nullopt); })) {
        SPDLOG_LOGGER_ERROR(logger, "conn_id: {} DGRAM notify queue is full", connection->GetID());
    }
} catch (const std::exception& e) {
    SPDLOG_LOGGER_ERROR(logger, "Caught exception in OnRecvDatagram. (error={})", e.what());
    // TODO(tievens): Add metrics to track if this happens
}

void
PicoQuicTransport::OnRecvStreamBytes(const std::shared_ptr<PicoQuicConnection>& connection,
                                     DataContext* data_ctx,
                                     uint64_t stream_id,
                                     int is_fin,
                                     std::span<const uint8_t> bytes)
try {
    // Handle application stream data
    if (bytes.empty()) {
        return;
    }

    // Handle control stream message processing for WebTransport mode
    if (connection->GetAPI() == Connection::API::kWebTransport && connection->wt_control_stream_ctx != nullptr &&
        stream_id == connection->wt_control_stream_ctx->stream_id) {

        SPDLOG_LOGGER_DEBUG(logger,
                            "OnRecvStreamBytes: Received data on control stream {} for conn_id={}, len={}",
                            stream_id,
                            connection->GetID(),
                            bytes.size());

        // Parse the capsule data using picowt_receive_capsule
        // This accumulates partial capsule data across multiple calls
        if (!is_fin) {
            int ret = picowt_receive_capsule(
              connection->pq_cnx, bytes.data(), bytes.data() + bytes.size(), &connection->wt_capsule);

            if (ret != 0) {
                SPDLOG_LOGGER_ERROR(logger,
                                    "OnRecvStreamBytes: Failed to parse capsule on control stream {} for conn_id={}",
                                    stream_id,
                                    connection->GetID());
                picowt_release_capsule(&connection->wt_capsule);
                return;
            }
        }

        // Check if capsule is fully received and stored
        if (connection->wt_capsule.h3_capsule.is_stored) {
            SPDLOG_LOGGER_INFO(
              logger,
              "OnRecvStreamBytes: Received capsule type={} error_code={} on control stream {} for conn_id={}",
              connection->wt_capsule.h3_capsule.capsule_type,
              connection->wt_capsule.error_code,
              stream_id,
              connection->GetID());

            if (is_fin) {
                // Mark FIN received on control stream
                connection->wt_control_stream_ctx->ps.stream_state.is_fin_received = 1;

                if (!is_server_mode) {
                    // Client: close the connection
                    SPDLOG_LOGGER_INFO(
                      logger,
                      "OnRecvStreamBytes: Client received control stream capsule, closing connection {}",
                      connection->GetID());
                    picoquic_close(connection->pq_cnx, 0);
                } else {
                    // Server: send FIN back on control stream if not already sent
                    if (!connection->wt_control_stream_ctx->ps.stream_state.is_fin_sent) {
                        SPDLOG_LOGGER_INFO(logger,
                                           "OnRecvStreamBytes: Server sending FIN on control stream {} for conn_id={}",
                                           stream_id,
                                           connection->GetID());
                        picoquic_add_to_stream(connection->pq_cnx, stream_id, NULL, 0, 1);
                    }
                    // Delete the stream prefix for this WebTransport session
                    if (connection->wt_h3_ctx != nullptr) {
                        h3zero_delete_stream_prefix(connection->pq_cnx, connection->wt_h3_ctx, stream_id);
                    }
                }

                // Release the capsule resources
                picowt_release_capsule(&connection->wt_capsule);

                // Notify the delegate that the connection is closing
                OnConnectionStatus(connection, TransportStatus::kDisconnected);
            }
        }

        return;
    }

    auto rx_buf_it = connection->rx_stream_buffer.find(stream_id);
    if (rx_buf_it == connection->rx_stream_buffer.end()) {
        if (bytes.size() < kMinStreamBytesForSend) {
            SPDLOG_LOGGER_DEBUG(logger,
                                "bytes received from picoquic stream {} len: {} is too small to process stream header",
                                stream_id,
                                bytes.size());
        }
        auto [it, _] = connection->rx_stream_buffer.try_emplace(stream_id);
        it->second.rx_ctx->data_queue.SetLimit(tconfig_.time_queue_rx_size);
        rx_buf_it = std::move(it);
    }

    auto& rx_buf = rx_buf_it->second;

    auto curr_ticks_ms =
      static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(tick_service_->get()).count());

    if (rx_buf.rx_ctx->unknown_expiry_tick_ms && curr_ticks_ms > rx_buf.rx_ctx->unknown_expiry_tick_ms) {
        SPDLOG_LOGGER_DEBUG(logger,
                            "Stream is unknown and now has expired, resetting stream {} expiry {}ms > {}ms",
                            stream_id,
                            rx_buf.rx_ctx->unknown_expiry_tick_ms,
                            curr_ticks_ms);
        picoquic_reset_stream_ctx(connection->pq_cnx, stream_id);
        picoquic_reset_stream(connection->pq_cnx, stream_id, static_cast<uint64_t>(StreamErrorCodes::kUnknownExpiry));
        rx_buf.closed = true;

        return;
    }

    rx_buf.rx_ctx->data_queue.Push(std::make_shared<const std::vector<uint8_t>>(bytes.begin(), bytes.end()));

    if (data_ctx != nullptr) {
        data_ctx->metrics.rx_stream_cb++;
        data_ctx->metrics.rx_stream_bytes += bytes.size();

        if (rx_buf.rx_ctx->data_queue.Size() < 10 &&
            !cbNotifyQueue_.Push([=, this, data_ctx_id = data_ctx->data_ctx_id]() {
                connection->OnRecvStream(stream_id, data_ctx_id, (stream_id & 2) == 0);
            })) {

            SPDLOG_LOGGER_ERROR(
              logger, "conn_id: {} stream_id: {} notify queue is full", connection->GetID(), stream_id);
        }

    } else {
        // When data_ctx is null, determine if stream is bidirectional from stream_id
        // QUIC stream IDs have bit 1 set to 0 for bidirectional streams
        if (!cbNotifyQueue_.Push(
              [=, this]() { connection->OnRecvStream(stream_id, std::nullopt, (stream_id & 2) == 0); })) {
            SPDLOG_LOGGER_ERROR(
              logger, "conn_id: {} stream_id: {} notify queue is full", connection->GetID(), stream_id);
        }
    }
} catch (const std::exception& e) {
    SPDLOG_LOGGER_ERROR(logger, "Caught exception in OnRecvStreamBytes. (error={})", e.what());
    // TODO(tievens): Add metrics to track if this happens
}

void
PicoQuicTransport::OnStreamClosed(const std::shared_ptr<PicoQuicConnection>& connection,
                                  uint64_t stream_id,
                                  std::shared_ptr<StreamRxContext> rx_ctx,
                                  std::optional<uint64_t> data_ctx_id,
                                  StreamClosedFlag flag)
{
    SPDLOG_DEBUG("Stream {} closed for connection {}", stream_id, connection->GetID());
    cbNotifyQueue_.Push([=, rx_ctx = std::move(rx_ctx)]() {
        connection->OnStreamClosed(stream_id, std::move(rx_ctx), data_ctx_id, flag);
    });
}

void
PicoQuicTransport::EmitMetrics()
{
    for (const auto& [conn_id, connection] : connections_) {
        const bool queue_space = cbNotifyQueue_.Size() < (tconfig_.callback_queue_size * 3) / 4;
        if (queue_space) {
            const auto sample_time = std::chrono::system_clock::now();
            cbNotifyQueue_.Push([=, c = connection]() {
                if (c) {
                    c->SampleMetrics(sample_time);
                }
            });
        }
    }
}

void
PicoQuicTransport::RemoveClosedStreams()
{
    std::lock_guard<std::mutex> _(state_mutex_);

    for (auto& [conn_id, connection] : connections_) {
        std::vector<uint64_t> closed_streams;

        for (auto& [stream_id, rx_buf] : connection->rx_stream_buffer) {
            if (rx_buf.closed && (rx_buf.rx_ctx->data_queue.Empty() || rx_buf.checked_once)) {
                closed_streams.push_back(stream_id);
            }
            rx_buf.checked_once = true;
        }

        for (const auto stream_id : closed_streams) {
            connection->rx_stream_buffer.erase(stream_id);
        }
    }
}

void
PicoQuicTransport::CheckConnsForCongestion()
{
    std::lock_guard<std::mutex> _(state_mutex_);

    /*
     * A sign of congestion is when transmit queues are not being serviced (e.g., have a backlog).
     * With no congestion, queues will be close to zero in size.
     *
     * Check each queue size to determine if there is possible congestion
     */

    for (auto& [conn_id, connection] : connections_) {
        int congested_count{ 0 };
        uint16_t cwin_congested_count = connection->metrics.cwin_congested - connection->metrics.prev_cwin_congested;

        picoquic_path_quality_t path_quality;
        picoquic_get_path_quality(connection->pq_cnx, connection->pq_cnx->path[0]->unique_path_id, &path_quality);

        /*
         * Update metrics
         */
        connection->metrics.tx_lost_pkts = path_quality.lost;
        connection->metrics.tx_cwin_bytes.AddValue(path_quality.cwin);
        connection->metrics.tx_in_transit_bytes.AddValue(path_quality.bytes_in_transit);
        connection->metrics.tx_spurious_losses = path_quality.spurious_losses;
        connection->metrics.tx_timer_losses = path_quality.timer_losses;
        connection->metrics.rtt_us.AddValue(path_quality.rtt_sample);
        connection->metrics.srtt_us.AddValue(path_quality.rtt);
        connection->metrics.tx_rate_bps.AddValue(path_quality.pacing_rate * 8);
        connection->metrics.rx_rate_bps.AddValue(path_quality.receive_rate_estimate * 8);

        // Is CWIN congested?
        if (cwin_congested_count > 5 || (path_quality.cwin < kPqCcLowCwin && path_quality.bytes_in_transit)) {

            // congested_count++; /* TODO(tievens): DO NOT react to this right now, causing issue with low latency
            // wired networks */
        }
        connection->metrics.prev_cwin_congested = connection->metrics.cwin_congested;

        // All other data flows (streams)
        uint64_t reset_wait_data_ctx_id{ 0 }; // Positive value indicates the data_ctx_id that can be set to reset_wait

        for (auto& [data_ctx_id, data_ctx] : connection->active_data_contexts) {
            for (auto& [stream_id, stream] : data_ctx.streams) {
                // Skip context that is in reset and wait
                if (stream.tx_reset_wait_discard) {
                    continue;
                }

                if (!stream.tx_data) {
                    continue;
                }

                // Don't include control stream in delayed callbacks check. Control stream should be priority 0 or 1
                if (stream.priority >= 2 &&
                    data_ctx.metrics.tx_delayed_callback - data_ctx.metrics.prev_tx_delayed_callback > 1) {
                    SPDLOG_LOGGER_DEBUG(logger,
                                        "CC: remote: {} port: {} conn_id: {} stream_id: {} queue_size: {}",
                                        connection->peer_addr_text,
                                        connection->peer_port,
                                        conn_id,
                                        stream_id,
                                        data_ctx.metrics.tx_delayed_callback -
                                          data_ctx.metrics.prev_tx_delayed_callback);

                    congested_count++;
                }
                data_ctx.metrics.prev_tx_delayed_callback = data_ctx.metrics.tx_delayed_callback;

                std::lock_guard __(*stream.tx_data);

                auto tx_data_size = stream.tx_data->Size();
                data_ctx.metrics.tx_queue_size.AddValue(tx_data_size);

                // TODO(tievens): size of TX is based on rate; adjust based on burst rates
                if (tx_data_size >= 50) {
                    congested_count++;
                    SPDLOG_LOGGER_DEBUG(logger,
                                        "CC: remote: {} port: {} conn_id: {} stream_id: {} queue_size: {}",
                                        connection->peer_addr_text,
                                        connection->peer_port,
                                        conn_id,
                                        stream_id,
                                        tx_data_size);
                }

                if (stream.priority >= kPqRestWaitMinPriority && data_ctx.uses_reset_wait &&
                    reset_wait_data_ctx_id == 0 && !stream.tx_reset_wait_discard) {

                    reset_wait_data_ctx_id = data_ctx_id;
                }
            }
        }

        if (cwin_congested_count &&
            connection->pq_cnx->nb_retransmission_total - connection->metrics.tx_retransmits > 2) {
            SPDLOG_LOGGER_DEBUG(logger,
                                "CC: remote: {} port: {} conn_id: {} retransmits increased, delta: {} total: {}",
                                connection->peer_addr_text,
                                connection->peer_port,
                                conn_id,
                                (connection->pq_cnx->nb_retransmission_total - connection->metrics.tx_retransmits),
                                connection->pq_cnx->nb_retransmission_total);

            connection->metrics.tx_retransmits = connection->pq_cnx->nb_retransmission_total;
            congested_count++;
        }

        // Act on congested
        if (congested_count) {
            connection->metrics.tx_congested++;

            connection->is_congested = true;
            SPDLOG_LOGGER_DEBUG(
              logger,
              "CC: conn_id: {} has streams congested. congested_count: {} retrans: {} cwin_congested: {}",
              conn_id,
              congested_count,
              connection->metrics.tx_retransmits,
              connection->metrics.cwin_congested);

            if (tconfig_.use_reset_wait_strategy && reset_wait_data_ctx_id > 0) {
                auto& data_ctx = connection->active_data_contexts[reset_wait_data_ctx_id];
                SPDLOG_LOGGER_INFO(
                  logger, "CC: conn_id: {} setting reset and wait to data_ctx_id: {}", conn_id, reset_wait_data_ctx_id);

                for (auto& [_, stream] : data_ctx.streams) {
                    stream.tx_reset_wait_discard = true;
                    data_ctx.metrics.tx_reset_wait++;
                }

                /*
                 * TODO(tievens) Submit an issue with picoquic to add an API to flush the stream of any
                 *      data stuck in retransmission or waiting for acks
                 */
                // close_stream(connection, &data_ctx, true);
            }

        } else if (connection->is_congested) {

            if (connection->not_congested_gauge > 8) {
                // No longer congested
                connection->is_congested = false;
                connection->not_congested_gauge = 0;
                SPDLOG_LOGGER_DEBUG(
                  logger, "CC: conn_id: {} congested_count: {} is no longer congested.", conn_id, congested_count);
            } else {
                connection->not_congested_gauge++;
            }
        }
    }
}

/* ============================================================================
 * Private methods
 * ============================================================================
 */
void
PicoQuicTransport::Server()
{
    quic_network_thread_params_.local_port = serverInfo_.port;
    quic_network_thread_params_.local_af = PF_UNSPEC;
    quic_network_thread_params_.dest_if = 0;
    quic_network_thread_params_.socket_buffer_size = tconfig_.socket_buffer_size;
    quic_network_thread_params_.do_not_use_gso = 0;
    quic_network_thread_params_.extra_socket_required = 0;
    quic_network_thread_params_.prefer_extra_socket = 0;
    quic_network_thread_params_.simulate_eio = 0;
    quic_network_thread_params_.send_length_max = 0;

    SPDLOG_LOGGER_DEBUG(logger, "Starting picoquic network thread");
    quic_network_thread_ctx_ =
      picoquic_start_network_thread(quic_ctx_, &quic_network_thread_params_, PqLoopCb, this, &quic_loop_return_value_);

    if (quic_ctx_ == NULL || quic_network_thread_ctx_ == NULL) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to start picoquic network thread");
        picoquic_free(quic_ctx_);
        quic_ctx_ = NULL;
        SetStatus(TransportStatus::kShutdown);
    }

    // Wait for something to happen with the thread
    while (!quic_network_thread_ctx_->thread_is_ready && !quic_network_thread_ctx_->return_code) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (quic_network_thread_ctx_->return_code) {
        SPDLOG_LOGGER_ERROR(
          logger, "Could not start quic network thread error: {}", quic_network_thread_ctx_->return_code);
        SetStatus(TransportStatus::kShutdown);
        return;
    }
}
std::shared_ptr<Connection>
PicoQuicTransport::StartClient()
{
    // Use shared state to avoid lifetime issues if timeout occurs before lambda executes
    struct SharedState
    {
        std::condition_variable cv;
        std::mutex mtx;
        std::uint64_t conn_id{ 0 };
        std::shared_ptr<PicoQuicConnection> connection;
    };
    auto state = std::make_shared<SharedState>();
    std::unique_lock lock(state->mtx);

    RunPqFunction([this, state]() {
        auto notify_caller = [state](uint64_t id) {
            std::lock_guard _(state->mtx);
            state->conn_id = id;

            // Notify calling thread of connection Id
            state->cv.notify_all();
        };

        sockaddr_storage server_address;
        char const* sni = "cisco.webex.com";
        int ret;
        int is_name = 0;
        ret = picoquic_get_server_address(serverInfo_.host_or_ip.c_str(), serverInfo_.port, &server_address, &is_name);
        if (ret != 0 || server_address.ss_family == 0) {
            SPDLOG_LOGGER_ERROR(
              logger, "Failed to resolve server: {} port: {}", serverInfo_.host_or_ip, serverInfo_.port);
            notify_caller(1);
            return 0;
        }

        if (is_name) {
            sni = serverInfo_.host_or_ip.c_str();
        }

        picoquic_cnx_t* cnx = NULL;
        if (connection_api == Connection::API::kNativeQuic) {
            cnx = picoquic_create_cnx(quic_ctx_,
                                      picoquic_null_connection_id,
                                      picoquic_null_connection_id,
                                      reinterpret_cast<struct sockaddr*>(&server_address),
                                      picoquic_current_time(),
                                      0,
                                      sni,
                                      config_.alpn,
                                      1);
            if (cnx == nullptr) {
                SPDLOG_LOGGER_ERROR(logger, "Could not create picoquic connection client context");
                notify_caller(1);
                return PICOQUIC_ERROR_DISCONNECTED;
            }

            picoquic_set_transport_parameters(cnx, &local_tp_options_);
            picoquic_set_feedback_loss_notification(cnx, 1);
            picoquic_enable_keep_alive(cnx, tconfig_.idle_timeout_ms * 500);
            picoquic_set_callback(cnx, PqEventCb, this);

            if (auto ret = picoquic_start_client_cnx(cnx)) {
                SPDLOG_LOGGER_ERROR(logger, "Could not activate connection ret: {}", ret);
                notify_caller(1);
                return PICOQUIC_ERROR_DISCONNECTED;
            }

            SPDLOG_LOGGER_INFO(logger, "StartClient: Creating connection context");
            state->connection = CreateConnection(cnx);

        } else if (connection_api == Connection::API::kWebTransport) {
            h3zero_callback_ctx_t* h3_ctx = nullptr;
            h3zero_stream_ctx_t* control_stream_ctx = nullptr;
            uint64_t current_time = picoquic_current_time();

            ret = picowt_prepare_client_cnx(
              quic_ctx_, (struct sockaddr*)&server_address, &cnx, &h3_ctx, &control_stream_ctx, current_time, sni);
            if (ret != 0) {
                SPDLOG_LOGGER_ERROR(logger, "picowt_prepare_client_cnx failed with ret: {}", ret);
                notify_caller(1);
                return ret;
            }

            picoquic_set_transport_parameters(cnx, &local_tp_options_);
            // Must run after set_transport_parameters; picowt_prepare_client_cnx sets WT params that local_tp_options_
            // overwrites.
            picowt_set_transport_parameters(cnx);
            picoquic_set_feedback_loss_notification(cnx, 1);
            picoquic_enable_keep_alive(cnx, tconfig_.idle_timeout_ms * 500);

            // Create connection context and store per-connection WebTransport context first
            state->connection = CreateConnection(cnx, Connection::API::kWebTransport);
            state->connection->wt_h3_ctx = h3_ctx;
            state->connection->wt_control_stream_ctx = control_stream_ctx;
            state->connection->wt_h3_ctx_owned = true; // Client owns this and must free it
            state->connection->wt_authority = serverInfo_.host_or_ip + ":" + std::to_string(serverInfo_.port);

            SPDLOG_LOGGER_INFO(logger,
                               "StartClient:Webtransport Connect: Control Stream ID: {}, "
                               "authority: {}, path: {}",
                               control_stream_ctx->stream_id,
                               state->connection->wt_authority,
                               wt_config_->path);

            // Initiate the WebTransport connect
            ret = picowt_connect(cnx,
                                 h3_ctx,
                                 control_stream_ctx,
                                 state->connection->wt_authority.c_str(),
                                 wt_config_->path.c_str(),
                                 DefaultWebTransportCallback,
                                 this,
                                 kMoqtAlpn);
            if (ret != 0) {
                SPDLOG_LOGGER_ERROR(logger, "Failed to initiate WebTransport connect");
                notify_caller(1);
                return ret;
            }

            ret = picoquic_start_client_cnx(cnx);

            if (ret != 0) {
                SPDLOG_LOGGER_ERROR(logger, "Failed to initiate WebTransport client connection");
                notify_caller(1);
                return ret;
            }

            picoquic_connection_id_t icid = picoquic_get_initial_cnxid(cnx);
            std::string icid_str;
            icid_str.reserve(icid.id_len * 2);
            for (uint8_t i = 0; i < icid.id_len; i++) {
                char hex_chars[3];
                snprintf(hex_chars, sizeof(hex_chars), "%02x", icid.id[i]);
                icid_str += hex_chars;
            }
            SPDLOG_LOGGER_INFO(logger, "WebTransport Initial connection ID: {}", icid_str);
            SPDLOG_LOGGER_INFO(logger,
                               "StartClient:Webtransport (after connect): Control Stream ID: {}, "
                               "authority: {}, path: {}",
                               control_stream_ctx->stream_id,
                               state->connection->wt_authority,
                               wt_config_->path);
        }

        if (tconfig_.quic_priority_limit > 0) {
            SPDLOG_LOGGER_INFO(
              logger, "Setting priority bypass limit to {}", static_cast<int>(tconfig_.quic_priority_limit));
            picoquic_set_priority_limit_for_bypass(cnx, tconfig_.quic_priority_limit);
        } else {
            SPDLOG_LOGGER_INFO(logger, "No priority bypass");
        }

        notify_caller(reinterpret_cast<uint64_t>(cnx));

        return 0;
    });

    SPDLOG_LOGGER_DEBUG(logger, "Waiting for client connection context");

    state->cv.wait_for(lock, std::chrono::milliseconds(3000), [&state]() { return state->conn_id > 0; });

    SPDLOG_LOGGER_DEBUG(logger, "Got client connection context conn_id: {}", state->conn_id);
    if (state->conn_id <= 1) {
        SPDLOG_LOGGER_DEBUG(logger, "Client connection to {}:{} failed", serverInfo_.host_or_ip, serverInfo_.port);
        SetStatus(TransportStatus::kDisconnected);
        return 0;
    }

    return state->connection;
}

bool
PicoQuicTransport::ClientLoop()
{
    SPDLOG_LOGGER_INFO(logger, "Thread client packet loop starting");

    quic_network_thread_params_.local_port = 0;
    quic_network_thread_params_.local_af = PF_UNSPEC;
    quic_network_thread_params_.dest_if = 0;
    quic_network_thread_params_.socket_buffer_size = tconfig_.socket_buffer_size;
#ifdef ESP_PLATFORM
    quic_network_thread_params_.socket_buffer_size = 0x2048;
#endif
    quic_network_thread_params_.do_not_use_gso = 0;
    quic_network_thread_params_.extra_socket_required = 0;
    quic_network_thread_params_.prefer_extra_socket = 0;
    quic_network_thread_params_.simulate_eio = 0;
    quic_network_thread_params_.send_length_max = 0;

    quic_network_thread_ctx_ =
      picoquic_start_network_thread(quic_ctx_, &quic_network_thread_params_, PqLoopCb, this, &quic_loop_return_value_);

    if (quic_ctx_ == nullptr || quic_network_thread_ctx_ == nullptr) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to create picoquic network thread");
        picoquic_free(quic_ctx_);
        quic_ctx_ = nullptr;
        return false;
    }

    // Wait for something to happen with the thread
    while (!quic_network_thread_ctx_->thread_is_ready && !quic_network_thread_ctx_->return_code) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (quic_network_thread_ctx_->return_code) {
        SPDLOG_LOGGER_ERROR(
          logger, "Could not start client quic network thread error: {}", quic_network_thread_ctx_->return_code);
        return false;
    }

    SPDLOG_LOGGER_DEBUG(logger, "Thread client packet loop started");

    return true;
}

void
PicoQuicTransport::Shutdown()
{
    if (stop_) // Already stopped
        return;

    stop_ = true;

    if (quic_network_thread_ctx_ != NULL) {
        SPDLOG_LOGGER_INFO(logger, "Closing transport picoquic thread");
        picoquic_delete_network_thread(quic_network_thread_ctx_);
        quic_network_thread_ctx_ = nullptr;
    }

    picoquic_runner_queue_.StopWaiting();
    cbNotifyQueue_.StopWaiting();

    if (cbNotifyThread_.joinable()) {
        SPDLOG_LOGGER_INFO(logger, "Closing transport callback notifier thread");
        cbNotifyThread_.join();
    }

    if (quic_ctx_ != nullptr) {
        picoquic_free(quic_ctx_);
        quic_ctx_ = nullptr;
    }

    tick_service_.reset();
    SPDLOG_LOGGER_INFO(logger, "done closing transport threads");

    picoquic_config_clear(&config_);
}

void
PicoQuicTransport::CheckCallbackDelta(DataContext* data_ctx, bool tx)
{
    if (!tx)
        return;

    const auto current_tick =
      static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(tick_service_->get()).count());

    std::lock_guard _(data_ctx->stream_mutex); // TODO: This doesn't seem to help.

    for (auto& [_, stream] : data_ctx->streams) {
        if (stream.last_tx_tick == 0) {
            stream.last_tx_tick = current_tick;
            continue;
        }

        const auto delta_ms = current_tick - stream.last_tx_tick;
        stream.last_tx_tick = current_tick;

        data_ctx->metrics.tx_callback_ms.AddValue(delta_ms);

        std::lock_guard __(*stream.tx_data);
        if (stream.priority > 0 && delta_ms > 50 && stream.tx_data->Size() >= 20) {
            data_ctx->metrics.tx_delayed_callback++;
        }
    }
}

void
PicoQuicTransport::CbNotifier()
{
    SPDLOG_LOGGER_INFO(logger, "Starting transport callback notifier thread");

    while (not stop_) {
        auto cb = cbNotifyQueue_.BlockPop();
        if (cb) {
            try {
                (*cb)();
            } catch (const std::exception& e) {
                SPDLOG_LOGGER_ERROR(
                  logger, "Caught exception running callback via notify thread (error={}), ignoring", e.what());
                // TODO(tievens): Add metrics to track if this happens
            }
        } else {
            SPDLOG_LOGGER_INFO(logger, "Notify callback is NULL");
        }
    }

    SPDLOG_LOGGER_INFO(logger, "Done with transport callback notifier thread");
}

std::uint64_t
PicoQuicTransport::CreateStream(const std::shared_ptr<Connection>& connection,
                                std::uint64_t data_ctx_id,
                                uint8_t priority)
{
    struct SharedState
    {
        std::condition_variable cv;
        std::mutex mtx;
        std::optional<uint64_t> stream_id{ std::nullopt };
    };
    auto state = std::make_shared<SharedState>();

    RunPqFunction([=, this]() {
        {
            std::lock_guard _(state->mtx);
            state->stream_id = CreateStreamInternal(connection, data_ctx_id, priority);
        }
        state->cv.notify_all();

        return 0;
    });

    std::unique_lock lk(state->mtx);
    state->cv.wait_for(lk, std::chrono::milliseconds(100), [&] { return state->stream_id.has_value(); });

    if (!state->stream_id.has_value()) {
        throw PicoQuicException("Unable to create stream");
    }

    SPDLOG_LOGGER_DEBUG(logger,
                        "Created reliable data context id: {} stream_id: {}, pri: {}",
                        data_ctx_id,
                        state->stream_id.value(),
                        static_cast<int>(priority));

    return state->stream_id.value();
}

std::uint64_t
PicoQuicTransport::CreateStreamInternal(const std::shared_ptr<Connection>& connection,
                                        std::uint64_t data_ctx_id,
                                        uint8_t priority)
{
    std::unique_lock lock(state_mutex_);

    const auto pq_conn = std::static_pointer_cast<PicoQuicConnection>(connection);
    const auto data_ctx_it = pq_conn->active_data_contexts.find(data_ctx_id);
    if (data_ctx_it == pq_conn->active_data_contexts.end()) {
        throw PicoQuicException("Unable to find data context");
    }

    DataContext::StreamContext stream;
    std::uint64_t stream_id = 0;

    stream.tx_data = std::make_unique<SafeTimeQueue<ConnData>>(tconfig_.time_queue_max_duration,
                                                               tconfig_.time_queue_bucket_interval,
                                                               tick_service_,
                                                               tconfig_.time_queue_init_queue_size);

    // Handle WebTransport and raw QUIC differently
    if (pq_conn->GetAPI() == Connection::API::kWebTransport) {
        // For WebTransport, create stream using picowt_create_local_stream
        // Use per-connection WebTransport context instead of global wt_context_
        if (!pq_conn->wt_h3_ctx || !pq_conn->wt_control_stream_ctx) {
            SPDLOG_LOGGER_ERROR(
              logger, "WebTransport context not initialized for connection {} stream creation", pq_conn->GetID());
            throw PicoQuicException("WebTransport context not initialized for connection");
        }

        h3zero_stream_ctx_t* stream_ctx = picowt_create_local_stream(pq_conn->pq_cnx,
                                                                     data_ctx_it->second.is_bidir ? 1 : 0,
                                                                     pq_conn->wt_h3_ctx,
                                                                     pq_conn->wt_control_stream_ctx->stream_id);

        if (!stream_ctx) {
            SPDLOG_LOGGER_ERROR(logger, "Failed to create WebTransport stream");
            throw PicoQuicException("Failed to create WebTransport stream");
        }

        stream_id = stream_ctx->stream_id;
        stream.wt_stream_ctx = stream_ctx;
        pq_conn->last_stream_id = stream_ctx->stream_id;
        pq_conn->wt_stream_to_data_ctx[stream_ctx->stream_id] = data_ctx_it->second.data_ctx_id;

        // Set callback and context for the stream
        stream_ctx->path_callback = DefaultWebTransportCallback;
        stream_ctx->path_callback_ctx = this;
    } else {
        // For raw QUIC, use the traditional approach
        pq_conn->last_stream_id = picoquic_get_next_local_stream_id(pq_conn->pq_cnx, !data_ctx_it->second.is_bidir);
        stream_id = pq_conn->last_stream_id;

        SPDLOG_LOGGER_DEBUG(logger,
                            "conn_id: {} data_ctx_id: {} create new stream with stream_id: {}",
                            connection->GetID(),
                            data_ctx_id,
                            pq_conn->last_stream_id);

        picoquic_set_app_stream_ctx(pq_conn->pq_cnx, stream_id, &data_ctx_it->second);
    }

    data_ctx_it->second.streams[stream_id] = std::move(stream);

    picoquic_set_stream_priority(pq_conn->pq_cnx, stream_id, (priority << 1));

    return stream_id;
}

void
PicoQuicTransport::CloseStream(const std::shared_ptr<Connection>& connection,
                               uint64_t data_ctx_id,
                               uint64_t stream_id,
                               bool use_reset)
{
    RunPqFunction([=, this, conn = std::static_pointer_cast<PicoQuicConnection>(connection)]() {
        auto data_ctx = conn->active_data_contexts.find(data_ctx_id);
        if (data_ctx == conn->active_data_contexts.end()) {
            CloseStream(conn, nullptr, stream_id, use_reset);
        } else {
            CloseStream(conn, std::addressof(data_ctx->second), stream_id, use_reset);
        }
        return 0;
    });
}

void
PicoQuicTransport::CloseStream(const std::shared_ptr<PicoQuicConnection>& connection,
                               DataContext* data_ctx,
                               std::uint64_t stream_id,
                               const bool use_reset)
{
    if (data_ctx) {
        if (!data_ctx->streams.contains(stream_id)) {
            SPDLOG_ERROR("Failed to close stream as it does not exist (conn_id={}, data_ctx_id={}, stream_id={})",
                         connection->GetID(),
                         data_ctx->data_ctx_id,
                         stream_id);
            return;
        }
    }

    SPDLOG_LOGGER_DEBUG(logger, "conn_id: {} closing stream stream_id: {}", connection->GetID(), stream_id);

    if (use_reset) {
        picoquic_reset_stream_ctx(connection->pq_cnx, stream_id);
        picoquic_reset_stream(connection->pq_cnx, stream_id, 0);
    } else {
        // TODO: PQ doesn't have a method to call to FIN a stream correctly, so we FIN it in SendStreamBytes()

        // Below doesn't work correctly, results in loss of data inflight
        uint8_t empty{ 0 };
        picoquic_add_to_stream(connection->pq_cnx, stream_id, &empty, 0, 1);
    }

    EraseStreamState(connection, data_ctx, stream_id);

    const auto rx_buf_it = connection->rx_stream_buffer.find(stream_id);
    if (rx_buf_it != connection->rx_stream_buffer.end()) {
        std::lock_guard<std::mutex> _(state_mutex_);

        connection->rx_stream_buffer.erase(rx_buf_it);
    }
}

void
PicoQuicTransport::EraseStreamState(const std::shared_ptr<PicoQuicConnection>& connection,
                                    DataContext* data_ctx,
                                    const std::uint64_t stream_id)
{
    if (data_ctx) {
        const auto stream_it = data_ctx->streams.find(stream_id);
        if (stream_it != data_ctx->streams.end()) {
            if (connection->GetAPI() == Connection::API::kWebTransport) {
                if (stream_it->second.wt_stream_ctx && connection->wt_h3_ctx) {
                    h3zero_delete_stream(connection->pq_cnx, connection->wt_h3_ctx, stream_it->second.wt_stream_ctx);
                }
            }
            data_ctx->streams.erase(stream_it);
        }
    }

    if (connection->GetAPI() == Connection::API::kWebTransport) {
        connection->wt_stream_to_data_ctx.erase(stream_id);
    }
}

void
PicoQuicTransport::RunPqFunction(std::function<int()>&& function)
{
    if (std::this_thread::get_id() == pq_event_thread_id || std::this_thread::get_id() == pq_runner_thread_id) {
        function();
        return;
    }

    bool should_wake = picoquic_runner_queue_.Empty();
    picoquic_runner_queue_.Push(std::move(function));

    if (should_wake) {
        picoquic_wake_up_network_thread(quic_network_thread_ctx_);
    }
}

void
PicoQuicTransport::MarkStreamActive(const std::shared_ptr<PicoQuicConnection>& connection,
                                    const std::uint64_t data_ctx_id,
                                    std::uint64_t stream_id)
{
    const auto data_ctx_it = connection->active_data_contexts.find(data_ctx_id);
    if (data_ctx_it == connection->active_data_contexts.end()) {
        return;
    }

    auto stream_it = data_ctx_it->second.streams.find(stream_id);
    if (stream_it == data_ctx_it->second.streams.end()) {
        return;
    }

    // For WebTransport and raw QUIC, pass the correct stream context
    void* stream_ctx = nullptr;
    if (connection->GetAPI() == Connection::API::kWebTransport) {
        stream_ctx = stream_it->second.wt_stream_ctx;
    } else {
        // For raw QUIC, pass the DataContext pointer
        stream_ctx = &data_ctx_it->second;
    }

    picoquic_mark_active_stream(connection->pq_cnx, stream_id, 1, stream_ctx);
}

void
PicoQuicTransport::MarkDgramReady(const std::shared_ptr<PicoQuicConnection>& connection)
{
    if (connection->GetAPI() == Connection::API::kWebTransport && connection->wt_control_stream_ctx) {
        // WebTransport requires using h3zero_set_datagram_ready to set the ready_to_send_datagrams
        // flag on the stream prefix, which triggers the picohttp_callback_provide_datagram callback
        h3zero_set_datagram_ready(connection->pq_cnx, connection->wt_control_stream_ctx->stream_id);
    } else {
        // Raw QUIC mode uses picoquic_mark_datagram_ready directly
        picoquic_mark_datagram_ready(connection->pq_cnx, 1);
    }

    connection->mark_dgram_ready = false;
}

const char*
PicoQuicTransport::GetAlpn() const
{
    switch (connection_api) {
        case Connection::API::kWebTransport:
            return webtransport_alpn;
        case Connection::API::kNativeQuic:
        default:
            return kMoqtAlpn;
    }
}

int
PicoQuicTransport::InitializeWebTransportContext()
{
    // For clients: only initialize if connection_api is kWebTransport
    // For servers: always initialize to support both QUIC and WebTransport connections
    if (!is_server_mode && connection_api != Connection::API::kWebTransport) {
        return 0; // Not WebTransport mode, nothing to do
    }

    if (!wt_config_) {
        wt_config_ = WebTransportConfig{};
        wt_config_->path = serverInfo_.path; // Only accept WebTransport connections to /relay path
        wt_config_->path_callback = DefaultWebTransportCallback;
        wt_config_->path_app_ctx = this; // Default app context is this transport instance
    }

    return 0;
}

int
PicoQuicTransport::SetupWebTransportConnection(picoquic_cnx_t* cnx)
{
    // This function is only called for WebTransport connections (checked by caller)
    // Just verify that WebTransport config is initialized
    if (!wt_config_) {
        SPDLOG_LOGGER_ERROR(logger, "WebTransport config not initialized");
        return -1;
    }

    int ret = 0;
    auto conn_id = reinterpret_cast<std::uint64_t>(cnx);

    // For client connections, use proper WebTransport setup flow
    if (!is_server_mode) {
        // Get or create connection context
        auto connection = GetConnection(conn_id);
        if (!connection) {
            SPDLOG_LOGGER_ERROR(logger, "Failed to get connection context for client WebTransport setup");
            return -1;
        }

        picoquic_cnx_t* prepared_cnx = cnx;
        h3zero_callback_ctx_t* h3_ctx = nullptr;
        h3zero_stream_ctx_t* control_stream_ctx = nullptr;

        // Get server address for picowt_prepare_client_cnx
        sockaddr_storage server_addr;
        int is_name = 0;
        ret = picoquic_get_server_address(serverInfo_.host_or_ip.c_str(), serverInfo_.port, &server_addr, &is_name);
        if (ret != 0) {
            SPDLOG_LOGGER_ERROR(logger, "Failed to get server address for WebTransport");
            return ret;
        }

        uint64_t current_time = picoquic_current_time();
        const char* sni = serverInfo_.host_or_ip.c_str();

        ret = picowt_prepare_client_cnx(
          quic_ctx_, (struct sockaddr*)&server_addr, &prepared_cnx, &h3_ctx, &control_stream_ctx, current_time, sni);
        if (ret != 0) {
            SPDLOG_LOGGER_ERROR(logger, "picowt_prepare_client_cnx failed with ret: {}", ret);
            return ret;
        }

        // Store per-connection h3_ctx and control stream in connection context
        connection->wt_h3_ctx = h3_ctx;
        connection->wt_control_stream_ctx = control_stream_ctx;
        connection->wt_h3_ctx_owned = true; // Client owns this and must free it
        connection->wt_authority = serverInfo_.host_or_ip + ":" + std::to_string(serverInfo_.port);

        // Initiate the WebTransport connect
        ret = picowt_connect(cnx,
                             connection->wt_h3_ctx,
                             connection->wt_control_stream_ctx,
                             connection->wt_authority.c_str(),
                             wt_config_->path.c_str(),
                             DefaultWebTransportCallback,
                             this,
                             kMoqtAlpn);
        if (ret != 0) {
            SPDLOG_LOGGER_ERROR(logger, "Failed to initiate WebTransport connect");
            return ret;
        }

        picoquic_connection_id_t icid = picoquic_get_initial_cnxid(cnx);
        std::string icid_str;
        icid_str.reserve(icid.id_len * 2);
        for (uint8_t i = 0; i < icid.id_len; i++) {
            char hex_chars[3];
            snprintf(hex_chars, sizeof(hex_chars), "%02x", icid.id[i]);
            icid_str += hex_chars;
        }
        SPDLOG_LOGGER_INFO(logger, "WebTransport Initial connection ID: {}", icid_str);
        SPDLOG_LOGGER_INFO(
          logger, "WebTransport client connect initiated to {}:{}", serverInfo_.host_or_ip, serverInfo_.port);
    } else {
        // Server mode: h3zero_callback will create per-connection h3_ctx automatically
        // when invoked with the picohttp_server_parameters_t (set in ALPN selection).
        // We just need to set WebTransport transport parameters.

        // Set WebTransport transport parameters
        picowt_set_transport_parameters(cnx);

        SPDLOG_LOGGER_INFO(
          logger, "WebTransport server connection setup - h3_ctx will be created per-connection by h3zero_callback");
    }

    SPDLOG_LOGGER_INFO(logger, "WebTransport connection setup completed");
    return ret;
}

// Accept an incoming WebTransport connection
int
PicoQuicTransport::AcceptWebTransportConnection(picoquic_cnx_t* cnx,
                                                uint8_t* path,
                                                size_t path_length,
                                                h3zero_stream_ctx_t* stream_ctx)
{
    int ret = 0;
    auto conn_id = reinterpret_cast<std::uint64_t>(cnx);

    // Validate path parameters
    if (path != nullptr && path_length > 0) {
        std::string path_str(reinterpret_cast<char*>(path), path_length);
        SPDLOG_LOGGER_INFO(logger, "AcceptWebTransportConnection: received path '{}'", path_str);

        // Get the path portion (before query parameters)
        size_t query_offset = h3zero_query_offset(path, path_length);
        std::string path_only(reinterpret_cast<char*>(path), query_offset);

        // Validate the path matches the expected path
        std::string expected_path = wt_config_ ? wt_config_->path : "/relay";
        if (path_only != expected_path) {
            SPDLOG_LOGGER_ERROR(logger,
                                "AcceptWebTransportConnection: path '{}' does not match expected path '{}'",
                                path_only,
                                expected_path);
            return -1;
        }
        // Parse query parameters if present
        if (query_offset < path_length) {
            const uint8_t* queries = path + query_offset;
            size_t queries_length = path_length - query_offset;
            SPDLOG_LOGGER_DEBUG(logger,
                                "AcceptWebTransportConnection: query string '{}'",
                                std::string(reinterpret_cast<const char*>(queries), queries_length));

            // Example: Parse a "version" parameter if needed in the future
            // uint64_t version = 0;
            // if (h3zero_query_parameter_number(queries, queries_length, "version", 7, &version, 1) != 0) {
            //     SPDLOG_LOGGER_ERROR(logger, "AcceptWebTransportConnection: failed to parse version parameter");
            //     return -1;
            // }
        }
    } else {
        SPDLOG_LOGGER_INFO(logger, "AcceptWebTransportConnection: no path provided");
    }

    auto& connection = CreateConnection(cnx);

    // Store the WebTransport control stream context for this connection
    // The stream_ctx parameter is the control stream for this WebTransport connection
    if (stream_ctx) {
        connection->wt_control_stream_ctx = stream_ctx;
        // Set the control stream ID in the stream context
        stream_ctx->ps.stream_state.control_stream_id = stream_ctx->stream_id;
        h3zero_callback_ctx_t* h3_ctx = (h3zero_callback_ctx_t*)picoquic_get_callback_context(cnx);

        // Store the h3_ctx in the connection context for per-connection WebTransport support
        connection->wt_h3_ctx = h3_ctx;

        // Register the stream prefix for this WebTransport session
        ret = h3zero_declare_stream_prefix(h3_ctx, stream_ctx->stream_id, DefaultWebTransportCallback, this);

        if (ret != 0) {
            SPDLOG_LOGGER_ERROR(
              logger,
              "AcceptWebTransportConnection: Failed to register stream prefix for WebTransport connection {}",
              conn_id);
            return ret;
        }

        SPDLOG_LOGGER_INFO(logger,
                           "AcceptWebTransportConnection: Registered control stream (stream_id: {}) for connection {}",
                           stream_ctx->stream_id,
                           conn_id);
    } else {
        SPDLOG_LOGGER_ERROR(
          logger, "AcceptWebTransportConnection: No stream context provided for WebTransport connection {}", conn_id);
        return -1;
    }

    // Set the callback on the stream context
    stream_ctx->path_callback = DefaultWebTransportCallback;
    stream_ctx->path_callback_ctx = this;

    SPDLOG_LOGGER_INFO(logger, "AcceptWebTransportConnection: Done accepting WebTransport connection {}", conn_id);

    HandleNewConnection(connection);

    return ret;
}

void
PicoQuicTransport::SetWebTransportPathCallback(const std::string& path,
                                               picohttp_post_data_cb_fn callback,
                                               void* app_ctx)
{

    if (!wt_config_) {
        wt_config_ = WebTransportConfig{};
    }

    wt_config_->path = path;
    wt_config_->path_callback = callback;
    wt_config_->path_app_ctx = app_ctx ? app_ctx : this;

    // Clear existing path items to force recreation with new settings
    wt_config_->path_items.clear();

    SPDLOG_LOGGER_INFO(
      logger, "WebTransport path callback configured: path={}, callback={}", path, callback ? "custom" : "default");
}

h3zero_stream_ctx_t*
PicoQuicTransport::CreateWebTransportStream(picoquic_cnx_t* cnx, bool is_bidir)
{
    if (connection_api != Connection::API::kWebTransport) {
        SPDLOG_LOGGER_ERROR(logger, "CreateWebTransportStream called but not in WebTransport mode");
        return nullptr;
    }

    // Get per-connection WebTransport context
    auto conn_id = reinterpret_cast<std::uint64_t>(cnx);
    auto connection = GetConnection(conn_id);
    if (!connection) {
        SPDLOG_LOGGER_ERROR(logger, "CreateWebTransportStream: Connection context not found for conn_id {}", conn_id);
        return nullptr;
    }

    if (!connection->wt_h3_ctx) {
        SPDLOG_LOGGER_ERROR(
          logger, "CreateWebTransportStream: WebTransport h3_ctx not initialized for conn_id {}", conn_id);
        return nullptr;
    }

    if (!connection->wt_control_stream_ctx) {
        SPDLOG_LOGGER_ERROR(logger, "CreateWebTransportStream: No control stream context for conn_id {}", conn_id);
        return nullptr;
    }

    // Use picowt_create_local_stream (pico_webtransport.h:94-95)
    h3zero_stream_ctx_t* stream_ctx = picowt_create_local_stream(
      cnx, is_bidir ? 1 : 0, connection->wt_h3_ctx, connection->wt_control_stream_ctx->stream_id);

    if (stream_ctx) {
        stream_ctx->path_callback = DefaultWebTransportCallback;
        stream_ctx->path_callback_ctx = this;

        SPDLOG_LOGGER_DEBUG(logger,
                            "Created WebTransport {} stream: {}",
                            is_bidir ? "bidirectional" : "unidirectional",
                            stream_ctx->stream_id);
    } else {
        SPDLOG_LOGGER_ERROR(
          logger, "Failed to create WebTransport {} stream", is_bidir ? "bidirectional" : "unidirectional");
    }

    return stream_ctx;
}

int
PicoQuicTransport::SendWebTransportCloseSession(picoquic_cnx_t* cnx, uint32_t error_code, const char* error_msg)
{
    if (connection_api != Connection::API::kWebTransport) {
        SPDLOG_LOGGER_ERROR(logger, "SendWebTransportCloseSession called but not in WebTransport mode");
        return -1;
    }

    // Get per-connection WebTransport context
    auto conn_id = reinterpret_cast<std::uint64_t>(cnx);
    auto connection = GetConnection(conn_id);
    if (!connection) {
        SPDLOG_LOGGER_ERROR(
          logger, "SendWebTransportCloseSession: Connection context not found for conn_id {}", conn_id);
        return -1;
    }

    if (!connection->wt_control_stream_ctx) {
        SPDLOG_LOGGER_ERROR(logger, "SendWebTransportCloseSession: No control stream context for conn_id {}", conn_id);
        return -1;
    }

    // Use picowt_send_close_session_message (pico_webtransport.h:69)
    int ret = picowt_send_close_session_message(cnx, connection->wt_control_stream_ctx, error_code, error_msg);

    if (ret == 0) {
        SPDLOG_LOGGER_INFO(
          logger, "WebTransport close session sent: code={}, msg={}", error_code, error_msg ? error_msg : "");
    } else {
        SPDLOG_LOGGER_ERROR(logger, "Failed to send WebTransport close session: ret={}", ret);
    }

    return ret;
}

int
PicoQuicTransport::SendWebTransportDrainSession(picoquic_cnx_t* cnx)
{
    if (connection_api != Connection::API::kWebTransport) {
        SPDLOG_LOGGER_ERROR(logger, "SendWebTransportDrainSession called but not in WebTransport mode");
        return -1;
    }

    // Get per-connection WebTransport context
    auto conn_id = reinterpret_cast<std::uint64_t>(cnx);
    auto connection = GetConnection(conn_id);
    if (!connection) {
        SPDLOG_LOGGER_ERROR(
          logger, "SendWebTransportDrainSession: Connection context not found for conn_id {}", conn_id);
        return -1;
    }

    if (!connection->wt_control_stream_ctx) {
        SPDLOG_LOGGER_ERROR(logger, "SendWebTransportDrainSession: No control stream context for conn_id {}", conn_id);
        return -1;
    }

    // Use picowt_send_drain_session_message (pico_webtransport.h:72-73)
    int ret = picowt_send_drain_session_message(cnx, connection->wt_control_stream_ctx);

    if (ret == 0) {
        SPDLOG_LOGGER_INFO(logger, "WebTransport drain session sent");
    } else {
        SPDLOG_LOGGER_ERROR(logger, "Failed to send WebTransport drain session: ret={}", ret);
    }

    return ret;
}

// Public API implementation for CloseWebTransportSession
int
PicoQuicTransport::CloseWebTransportSession(const std::shared_ptr<Connection>& connection,
                                            uint32_t error_code,
                                            const char* error_msg)
{
    picoquic_cnx_t* cnx = reinterpret_cast<picoquic_cnx_t*>(connection->GetID());
    return SendWebTransportCloseSession(cnx, error_code, error_msg);
}

// Public API implementation for DrainWebTransportSession
int
PicoQuicTransport::DrainWebTransportSession(const std::shared_ptr<Connection>& connection)
{
    picoquic_cnx_t* cnx = reinterpret_cast<picoquic_cnx_t*>(connection->GetID());
    return SendWebTransportDrainSession(cnx);
}

void
PicoQuicTransport::DeregisterWebTransport(picoquic_cnx_t* cnx)
{
    if (!is_server_mode && connection_api != Connection::API::kWebTransport) {
        SPDLOG_LOGGER_WARN(logger, "DeregisterWebTransport called but not in WebTransport mode");
        return;
    }

    // Get per-connection WebTransport context
    auto conn_id = reinterpret_cast<std::uint64_t>(cnx);
    auto connection = GetConnection(conn_id);
    if (!connection) {
        SPDLOG_LOGGER_WARN(logger, "DeregisterWebTransport: Connection context not found for conn_id {}", conn_id);
        return;
    }

    if (!connection->wt_h3_ctx || !connection->wt_control_stream_ctx) {
        SPDLOG_LOGGER_WARN(logger, "DeregisterWebTransport: WebTransport context already null for conn_id {}", conn_id);
        return;
    }

    // Use picowt_deregister to clean up all streams associated with this control stream
    picowt_deregister(cnx, connection->wt_h3_ctx, connection->wt_control_stream_ctx);

    // Release any accumulated capsule memory
    picowt_release_capsule(&connection->wt_capsule);

    SPDLOG_LOGGER_INFO(logger, "WebTransport context deregistered for conn_id {}", conn_id);

    // Clear the per-connection context
    connection->wt_control_stream_ctx = nullptr;

    // Clear WebTransport stream mappings for this session
    connection->wt_stream_to_data_ctx.clear();

    CloseInternal(connection, AppReasonForClose::kShutdown);
}
