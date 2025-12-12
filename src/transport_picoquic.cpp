// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "transport_picoquic.h"
#include "quicr/detail/transport.h"

// PicoQuic.
#include <autoqlog.h>
#include <picoquic.h>
#include <picoquic_config.h>
#include <picoquic_internal.h>
#include <picoquic_packet_loop.h>
#include <picoquic_utils.h>
#include <picosocks.h>
#include <spdlog/spdlog.h>
#include <tls_api.h>

// WebTransport includes
#include <h3zero_uri.h>
#include <pico_webtransport.h>

// PicoHTTP includes
#include <democlient.h>

// Transport.
#include <quicr/defer.h>
#include <quicr/detail/priority_queue.h>
#include <quicr/detail/quic_transport_metrics.h>
#include <quicr/detail/safe_queue.h>
#include <quicr/detail/stream_buffer.h>
#include <quicr/detail/time_queue.h>
#include <spdlog/logger.h>

// System.
#include "transport_picoquic.h"

#include "picoquic_bbr.h"
#include "picoquic_newreno.h"

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
    PicoQuicTransport::DataContext* data_ctx = static_cast<PicoQuicTransport::DataContext*>(v_stream_ctx);
    const auto conn_id = reinterpret_cast<uint64_t>(pq_cnx);

    bool is_fin = false;

    if (transport == NULL) {
        return PICOQUIC_ERROR_UNEXPECTED_ERROR;
    }

    switch (fin_or_event) {

        case picoquic_callback_prepare_datagram: {
            // length is the max allowed data length
            if (auto conn_ctx = transport->GetConnContext(conn_id)) {
                conn_ctx->metrics.tx_dgram_cb++;

                transport->SendNextDatagram(conn_ctx, bytes, length);

                if (picoquic_get_cwin(pq_cnx) < kPqCcLowCwin) { // Congested if less than 8K or near jumbo MTU size
                    conn_ctx->metrics.cwin_congested++;
                }
            }

            break;
        }

        case picoquic_callback_datagram_acked:
            //   bytes carries the original packet data
            if (auto conn_ctx = transport->GetConnContext(conn_id)) {
                conn_ctx->metrics.tx_dgram_ack++;
            }
            break;

        case picoquic_callback_datagram_spurious:
            if (auto conn_ctx = transport->GetConnContext(conn_id)) {
                conn_ctx->metrics.tx_dgram_spurious++;
            }
            break;

        case picoquic_callback_datagram_lost:
            if (auto conn_ctx = transport->GetConnContext(conn_id)) {
                conn_ctx->metrics.tx_dgram_lost++;
            }
            break;

        case picoquic_callback_datagram: {
            if (auto conn_ctx = transport->GetConnContext(conn_id)) {
                transport->OnRecvDatagram(conn_ctx, bytes, length);
            }
            break;
        }

        case picoquic_callback_prepare_to_send: {
            if (picoquic_get_cwin(pq_cnx) < kPqCcLowCwin) {
                // Congested if less than 8K or near jumbo MTU size
                if (auto conn_ctx = transport->GetConnContext(conn_id)) {
                    conn_ctx->metrics.cwin_congested++;
                } else {
                    break;
                }
            }

            if (data_ctx == NULL) {
                // picoquic calls this again even after reset/fin, here we ignore it
                SPDLOG_LOGGER_INFO(
                  transport->logger, "conn_id: {0} stream_id: {1} context is null", conn_id, stream_id);
                break;
            }

            data_ctx->metrics.tx_stream_cb++;
            transport->SendStreamBytes(data_ctx, bytes, length);
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

            if (auto conn_ctx = transport->GetConnContext(conn_id)) {
                transport->OnRecvStreamBytes(conn_ctx, data_ctx, stream_id, is_fin, std::span{ bytes, length });

                if (is_fin) {
                    SPDLOG_LOGGER_DEBUG(transport->logger, "Received FIN for stream {0}", stream_id);

                    picoquic_reset_stream_ctx(pq_cnx, stream_id);

                    if (auto conn_ctx = transport->GetConnContext(conn_id)) {
                        const auto rx_buf_it = conn_ctx->rx_stream_buffer.find(stream_id);
                        if (rx_buf_it != conn_ctx->rx_stream_buffer.end()) {
                            rx_buf_it->second.closed = true;
                            transport->OnStreamClosed(
                              conn_id, stream_id, rx_buf_it->second.rx_ctx, StreamClosedFlag::Fin);
                        }
                    }

                    if (data_ctx == NULL) {
                        break;
                    }

                    data_ctx->current_stream_id = std::nullopt;
                }
            }

            break;
        }

        case picoquic_callback_stream_reset: {
            SPDLOG_LOGGER_TRACE(
              transport->logger, "Received RESET stream conn_id: {0} stream_id: {1}", conn_id, stream_id);

            picoquic_reset_stream_ctx(pq_cnx, stream_id);

            if (auto conn_ctx = transport->GetConnContext(conn_id)) {
                const auto rx_buf_it = conn_ctx->rx_stream_buffer.find(stream_id);
                if (rx_buf_it != conn_ctx->rx_stream_buffer.end()) {
                    rx_buf_it->second.closed = true;
                    transport->OnStreamClosed(conn_id, stream_id, rx_buf_it->second.rx_ctx, StreamClosedFlag::Reset);
                }
            }

            if (data_ctx == NULL) {
                break;
            }

            data_ctx->current_stream_id = std::nullopt;

            SPDLOG_LOGGER_DEBUG(transport->logger,
                                "Received RESET stream; conn_id: {0} data_ctx_id: {1} stream_id: {2}",
                                data_ctx->conn_id,
                                data_ctx->data_ctx_id,
                                stream_id);
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
              "Pacing rate changed; conn_id: {0} rate Kbps: {1} cwin_bytes: {2} rtt_us: {3} rate Kbps: {4} cwin_bytes: "
              "{5} rtt_us: {6} rtt_max: {7} rtt_sample: {8} lost_pkts: {9} bytes_in_transit: {10} recv_rate_Kbps: {11}",
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
            SPDLOG_LOGGER_INFO(transport->logger, "Application closed conn_id: {0}", conn_id);
            [[fallthrough]];
        case picoquic_callback_close: {
            uint64_t app_reason_code = picoquic_get_application_error(pq_cnx);
            std::ostringstream log_msg;
            log_msg << "Closing connection conn_id: " << conn_id << " stream_id: " << stream_id;

            switch (picoquic_get_local_error(pq_cnx)) {
                case PICOQUIC_ERROR_IDLE_TIMEOUT:
                    log_msg << " Idle timeout";
                    app_reason_code = 1;
                    break;

                default:
                    log_msg << " local_error: " << picoquic_get_local_error(pq_cnx)
                            << " remote_error: " << picoquic_get_remote_error(pq_cnx)
                            << " app_error: " << picoquic_get_application_error(pq_cnx);
            }

            picoquic_set_callback(pq_cnx, NULL, NULL);

            if (auto conn_ctx = transport->GetConnContext(conn_id)) {
                log_msg << " remote: " << conn_ctx->peer_addr_text;
            }

            SPDLOG_LOGGER_INFO(transport->logger, log_msg.str());

            transport->Close(conn_id, app_reason_code);

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
                transport->CreateConnContext(pq_cnx);
                transport->OnNewConnection(conn_id);
            } else {
                // Client - for raw QUIC connections only, WebTransport connections use DefaultWebTransportCallback
                auto conn_ctx = transport->GetConnContext(conn_id);
                if (conn_ctx && conn_ctx->transport_mode == TransportMode::kQuic) {
                    transport->SetStatus(TransportStatus::kReady);
                    transport->OnConnectionStatus(conn_id, TransportStatus::kReady);
                }
                // WebTransport clients will get status updates via DefaultWebTransportCallback
            }

            (void)picoquic_mark_datagram_ready(pq_cnx, 1);

            break;
        }

        default:
            SPDLOG_LOGGER_DEBUG(transport->logger, "Got event {0}", static_cast<int>(fin_or_event));
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

    if (transport->Status() == TransportStatus::kDisconnected) {
        return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
    }

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

            if (targ->current_time - transport->pq_loop_metrics_prev_time >= kMetricsIntervalUs) {
                // Use this time to clean up streams that have been closed
                transport->RemoveClosedStreams();

                if (transport->pq_loop_metrics_prev_time) {
                    transport->EmitMetrics();
                }

                transport->pq_loop_metrics_prev_time = targ->current_time;
            }

            if (targ->current_time - transport->pq_loop_prev_time > kCongestionCheckInterval) {

                // transport->CheckConnsForCongestion();

                transport->pq_loop_prev_time = targ->current_time;
            }

            // Stop loop if done shutting down
            if (transport->Status() == TransportStatus::kShutdown) {
                return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
            }

            if (transport->Status() == TransportStatus::kShuttingDown) {
                SPDLOG_LOGGER_INFO(transport->logger, "picoquic is shutting down");

                picoquic_cnx_t* close_cnx = picoquic_get_first_cnx(quic);

                if (close_cnx == NULL) {
                    return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
                }

                while (close_cnx != NULL) {
                    SPDLOG_LOGGER_INFO(
                      transport->logger, "Closing connection id {0}", reinterpret_cast<uint64_t>(close_cnx));
                    transport->Close(reinterpret_cast<uint64_t>(close_cnx));
                    close_cnx = picoquic_get_next_cnx(close_cnx);
                }

                transport->SetStatus(TransportStatus::kShutdown);
            }

            break;
        }

        case picoquic_packet_loop_wake_up:
            transport->PqRunner();
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
static PicoQuicTransport::ConnectionContext*
GetConnCtxForWT(PicoQuicTransport* transport, TransportConnId conn_id, picohttp_call_back_event_t wt_event)
{
    auto conn_ctx = transport->GetConnContext(conn_id);
    if (!conn_ctx) {
        transport->logger->warn(
          "DefaultWT: {} No connection context for conn_id {}", WtEventToString(wt_event), conn_id);
    }
    return conn_ctx;
}

// Helper to get data context from WebTransport stream mapping
static PicoQuicTransport::DataContext*
GetDataCtxForWT(PicoQuicTransport::ConnectionContext* conn_ctx, uint64_t stream_id)
{
    if (!conn_ctx) {
        return nullptr;
    }
    auto stream_to_ctx_it = conn_ctx->wt_stream_to_data_ctx.find(stream_id);
    if (stream_to_ctx_it != conn_ctx->wt_stream_to_data_ctx.end()) {
        auto data_ctx_it = conn_ctx->active_data_contexts.find(stream_to_ctx_it->second);
        if (data_ctx_it != conn_ctx->active_data_contexts.end()) {
            return &data_ctx_it->second;
        }
    }
    return nullptr;
}

// Helper to clear data context stream and remove from WebTransport stream mapping
static void
ClearDataCtxStream(PicoQuicTransport::ConnectionContext* conn_ctx, uint64_t stream_id)
{
    if (!conn_ctx) {
        return;
    }
    auto stream_to_ctx_it = conn_ctx->wt_stream_to_data_ctx.find(stream_id);
    if (stream_to_ctx_it != conn_ctx->wt_stream_to_data_ctx.end()) {
        auto data_ctx_it = conn_ctx->active_data_contexts.find(stream_to_ctx_it->second);
        if (data_ctx_it != conn_ctx->active_data_contexts.end()) {
            data_ctx_it->second.current_stream_id = std::nullopt;
        }
        conn_ctx->wt_stream_to_data_ctx.erase(stream_to_ctx_it);
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

    auto conn_id = reinterpret_cast<TransportConnId>(cnx);
    int ret = 0;

    switch (wt_event) {
        case picohttp_callback_connecting:
            // Called when initiating WebTransport connect
            transport->logger->trace("DefaultWT: {} for connection {}", WtEventToString(wt_event), conn_id);
            break;

        case picohttp_callback_connect:
            /* A connect has been received on this stream, and could be accepted.
             */
            transport->logger->trace(
              "DefaultWT: {} connect received on path for connection {}", WtEventToString(wt_event), conn_id);

            if (transport->is_server_mode) {
                // Accept the incoming WebTransport connection
                // This initializes wt_context_, updates internal data structures,
                // and reports OnNewConnection() callback
                ret = transport->AcceptWebTransportConnection(cnx, bytes, length, stream_ctx);
                if (ret != 0) {
                    transport->logger->error("DefaultWT: Failed to accept WebTransport connection {}", conn_id);
                }
            }
            break;

        case picohttp_callback_connect_refused:
            transport->logger->warn("DefaultWT: {} for connection {}", WtEventToString(wt_event), conn_id);
            transport->OnConnectionStatus(conn_id, TransportStatus::kDisconnected);
            break;

        case picohttp_callback_connect_accepted:
            transport->logger->trace("DefaultWT: {} for connection {}, h3 stream {}",
                                     WtEventToString(wt_event),
                                     conn_id,
                                     stream_ctx->stream_id);

            transport->SetStatus(TransportStatus::kReady);
            transport->OnConnectionStatus(conn_id, TransportStatus::kReady);
            break;

        case picohttp_callback_post_data:
        case picohttp_callback_post_fin: {
            // Data received on a stream - similar to picoquic_callback_stream_data in PqEventCb
            if (!stream_ctx) {
                transport->logger->trace("DefaultWT: {} with null stream_ctx", WtEventToString(wt_event));
                return -1;
            }

            uint64_t stream_id = stream_ctx->stream_id;
            bool is_fin = (wt_event == picohttp_callback_post_fin);

            transport->logger->trace("DefaultWT: {} received {} bytes on stream {} for connection {}, is_fin {}",
                                     WtEventToString(wt_event),
                                     length,
                                     stream_id,
                                     conn_id,
                                     is_fin);

            auto conn_ctx = GetConnCtxForWT(transport, conn_id, wt_event);
            if (!conn_ctx) {
                return -1;
            }

            auto data_ctx = GetDataCtxForWT(conn_ctx, stream_id);

            // For bidir streams that are remotely initiated, create data context if needed
            if (data_ctx == nullptr) {
                // Check if this is a bidir stream (bit 0x2 == 0)
                if ((stream_id & 0x2) != 2) {
                    // Create bidir stream if it wasn't initiated by this instance (remote initiated it)
                    if (((stream_id & 0x1) == 1 && !transport->is_server_mode) ||
                        ((stream_id & 0x0) == 0 && transport->is_server_mode)) {

                        // Create the data context for new bidir streams created by remote side
                        data_ctx = transport->CreateDataContextBiDirRecv(conn_id, stream_id);

                        // Add to WebTransport stream mapping
                        if (data_ctx) {
                            conn_ctx->wt_stream_to_data_ctx[stream_id] = data_ctx->data_ctx_id;
                        }
                    }
                }
            }

            // Store the h3zero_stream_ctx_t* for WebTransport streams
            if (data_ctx && !data_ctx->wt_stream_ctx) {
                data_ctx->wt_stream_ctx = stream_ctx;
            }

            // Process received data
            if (length > 0) {
                transport->OnRecvStreamBytes(conn_ctx, data_ctx, stream_id, is_fin, std::span{ bytes, length });
            }

            if (is_fin) {
                transport->logger->trace("DefaultWT: {} Received FIN for connection{}, stream {}",
                                         WtEventToString(wt_event),
                                         conn_id,
                                         stream_id);

                picoquic_reset_stream_ctx(cnx, stream_id);

                auto rx_buf_it = conn_ctx->rx_stream_buffer.find(stream_id);
                if (rx_buf_it != conn_ctx->rx_stream_buffer.end()) {
                    rx_buf_it->second.closed = true;
                    transport->OnStreamClosed(conn_id, stream_id, rx_buf_it->second.rx_ctx, StreamClosedFlag::Fin);
                }

                if (data_ctx != nullptr) {
                    data_ctx->current_stream_id = std::nullopt;
                }
            }

            break;
        }

        case picohttp_callback_provide_data: {
            // Stack is ready to send data on a stream - similar to picoquic_callback_prepare_to_send in PqEventCb
            if (!stream_ctx) {
                transport->logger->warn("DefaultWT: {} with null stream_ctx", WtEventToString(wt_event));
                return -1;
            }

            uint64_t stream_id = stream_ctx->stream_id;

            transport->logger->trace(
              "DefaultWT: {} for connection {}, h3 stream {}", WtEventToString(wt_event), conn_id, stream_id);

            auto conn_ctx = GetConnCtxForWT(transport, conn_id, wt_event);
            if (!conn_ctx) {
                return -1;
            }

            auto data_ctx = GetDataCtxForWT(conn_ctx, stream_id);
            if (data_ctx == nullptr) {
                // No data context, nothing to send
                transport->logger->trace(
                  "DefaultWT: {} no data_ctx for stream {}", WtEventToString(wt_event), stream_id);
                break;
            }

            // Check congestion
            if (picoquic_get_cwin(cnx) < kPqCcLowCwin) {
                conn_ctx->metrics.cwin_congested++;
            }

            data_ctx->metrics.tx_stream_cb++;

            transport->logger->trace(
              "DefaultWT: {} Invoking to send stream bytes on stream {}", WtEventToString(wt_event), length, stream_id);

            // Send stream bytes - this will call picoquic_provide_stream_data_buffer internally
            transport->SendStreamBytes(data_ctx, bytes, length);
            break;
        }

        case picohttp_callback_post_datagram: {
            // Datagram received
            transport->logger->trace(
              "DefaultWT: {} received {} bytes for connection {}", WtEventToString(wt_event), length, conn_id);

            if (auto conn_ctx = GetConnCtxForWT(transport, conn_id, wt_event)) {
                transport->OnRecvDatagram(conn_ctx, bytes, length);
            }
            break;
        }

        case picohttp_callback_provide_datagram: {
            // Stack is ready to send a datagram
            if (auto conn_ctx = GetConnCtxForWT(transport, conn_id, wt_event)) {
                conn_ctx->metrics.tx_dgram_cb++;
                transport->SendNextDatagram(conn_ctx, bytes, length);

                if (picoquic_get_cwin(cnx) < kPqCcLowCwin) {
                    conn_ctx->metrics.cwin_congested++;
                }
            }
            break;
        }

        case picohttp_callback_reset: {
            // Stream has been abandoned
            if (!stream_ctx) {
                transport->logger->warn("DefaultWT: {} with null stream_ctx", WtEventToString(wt_event));
                return -1;
            }

            uint64_t stream_id = stream_ctx->stream_id;

            transport->logger->debug(
              "DefaultWT: {} for stream {} on connection {}", WtEventToString(wt_event), stream_id, conn_id);

            if (auto conn_ctx = transport->GetConnContext(conn_id)) {
                auto rx_buf_it = conn_ctx->rx_stream_buffer.find(stream_id);
                if (rx_buf_it != conn_ctx->rx_stream_buffer.end()) {
                    rx_buf_it->second.closed = true;
                    transport->OnStreamClosed(conn_id, stream_id, rx_buf_it->second.rx_ctx, StreamClosedFlag::Reset);
                }

                ClearDataCtxStream(conn_ctx, stream_id);
            }

            // Use picowt_reset_stream to properly reset the WebTransport stream
            picowt_reset_stream(cnx, stream_ctx, 0);

            break;
        }

        case picohttp_callback_stop_sending: {
            // Peer wants to abandon receiving on the stream
            if (!stream_ctx) {
                transport->logger->warn("DefaultWT: {} with null stream_ctx", WtEventToString(wt_event));
                return -1;
            }

            uint64_t stream_id = stream_ctx->stream_id;

            transport->logger->trace(
              "DefaultWT: {} for stream {} on connection {}", WtEventToString(wt_event), stream_id, conn_id);

            if (auto conn_ctx = transport->GetConnContext(conn_id)) {
                auto rx_buf_it = conn_ctx->rx_stream_buffer.find(stream_id);
                if (rx_buf_it != conn_ctx->rx_stream_buffer.end()) {
                    rx_buf_it->second.closed = true;
                    transport->OnStreamClosed(conn_id, stream_id, rx_buf_it->second.rx_ctx, StreamClosedFlag::Reset);
                }

                ClearDataCtxStream(conn_ctx, stream_id);
            }

            // Use picowt_reset_stream to properly reset the WebTransport stream
            picowt_reset_stream(cnx, stream_ctx, 0);

            break;
        }

        case picohttp_callback_free:
            // Clean up the stream
            transport->logger->trace("DefaultWT: {} callback for connection {}", WtEventToString(wt_event), conn_id);
            break;

        case picohttp_callback_deregister: {
            // The app context has been removed from the registry.
            // Its references should be removed from streams belonging to this session.
            transport->logger->trace("DefaultWT: {} callback for connection {}", WtEventToString(wt_event), conn_id);

            transport->DeregisterWebTransport(cnx);

            // For client mode, notify that connection is disconnected
            if (!transport->is_server_mode) {
                transport->OnConnectionStatus(conn_id, TransportStatus::kDisconnected);
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
    const char* moq_alpn = "moq-00";
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

TransportConnId
PicoQuicTransport::Start()
{
    uint64_t current_time = picoquic_current_time();

    if (debug) {
        debug_set_stream(stderr);
    }

    if (tconfig_.use_reset_wait_strategy) {
        SPDLOG_LOGGER_INFO(logger, "Using Reset and Wait congestion control strategy");
    }

    // Initialize WebTransport
    if (auto wt_ret = InitializeWebTransportContext(); wt_ret != 0) {
        SPDLOG_LOGGER_ERROR(logger, "Failed to initialize WebTransport");
        return 0;
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
      &config_, picoquic_option_CWIN_MIN, std::to_string(tconfig_.quic_cwin_minimum).c_str());
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
        picoquic_set_alpn_select_fn(quic_ctx_, PqAlpnSelectCb);
        picoquic_use_unique_log_names(quic_ctx_, 1);
    } else {
        if (transport_mode == TransportMode::kWebTransport) {
            SPDLOG_LOGGER_INFO(logger, "Client configured for WebTransport over QUIC");
            quic_ctx_ = picoquic_create_and_configure(&config_, NULL, NULL, current_time, NULL);
            if (quic_ctx_ != NULL) {
                // Set WebTransport default transport parameters (enables reset_stream_at)
                picowt_set_default_transport_parameters(quic_ctx_);
            }
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
    picoquic_init_transport_parameters(&local_tp_options_, 1);

    // TODO(tievens): revisit PMTU/GSO, removing this breaks some networks
    local_tp_options_.max_datagram_frame_size = 1280;

    local_tp_options_.max_idle_timeout = tconfig_.idle_timeout_ms;
    local_tp_options_.max_ack_delay = 100000;
    local_tp_options_.min_ack_delay = 1000;

    picoquic_set_default_handshake_timeout(quic_ctx_, (tconfig_.idle_timeout_ms * 1000) / 2);
    picoquic_set_default_tp(quic_ctx_, &local_tp_options_);
    picoquic_set_default_idle_timeout(quic_ctx_, tconfig_.idle_timeout_ms);
    picoquic_set_default_priority(quic_ctx_, 2);
    picoquic_set_default_datagram_priority(quic_ctx_, 1);

    SPDLOG_LOGGER_INFO(logger, "Setting idle timeout to {0}ms", tconfig_.idle_timeout_ms);

    picoquic_runner_queue_.SetLimit(tconfig_.callback_queue_size);

    cbNotifyQueue_.SetLimit(tconfig_.callback_queue_size);
    cbNotifyThread_ = std::thread(&PicoQuicTransport::CbNotifier, this);

    if (!tconfig_.quic_qlog_path.empty()) {
        SPDLOG_LOGGER_INFO(logger, "Enabling qlog using '{0}' path", tconfig_.quic_qlog_path);
        picoquic_set_qlog(quic_ctx_, tconfig_.quic_qlog_path.c_str());
    }

    TransportConnId cid = 0;
    std::ostringstream log_msg;

    if (is_server_mode) {

        SPDLOG_LOGGER_INFO(logger, "Starting server, listening on {0}:{1}", serverInfo_.host_or_ip, serverInfo_.port);
        Server();

    } else {
        SPDLOG_LOGGER_INFO(logger, "Connecting to server {0}:{1}", serverInfo_.host_or_ip, serverInfo_.port);

        if (ClientLoop()) {
            cid = StartClient();
        }
    }

    return cid;
}

bool
PicoQuicTransport::GetPeerAddrInfo(const TransportConnId& conn_id, sockaddr_storage* addr)
{
    std::lock_guard<std::mutex> _(state_mutex_);

    // Locate the specified transport connection context
    auto it = conn_context_.find(conn_id);

    // If not found, return false
    if (it == conn_context_.end())
        return false;

    // Copy the address
    std::memcpy(addr, &it->second.peer_addr, sizeof(sockaddr_storage));

    return true;
}

TransportError
PicoQuicTransport::Enqueue(const TransportConnId& conn_id,
                           const DataContextId& data_ctx_id,
                           std::uint64_t group_id,
                           std::shared_ptr<const std::vector<uint8_t>> bytes,
                           const uint8_t priority,
                           const uint32_t ttl_ms,
                           [[maybe_unused]] const uint32_t delay_ms,
                           const EnqueueFlags flags)
{
    SPDLOG_LOGGER_TRACE(logger,
                        "Enqueue conn_id: {0} data_ctx_id: {1}, size: {2}, new_stream ?: {3}",
                        conn_id,
                        data_ctx_id,
                        bytes->size(),
                        flags.new_stream);

    if (bytes->empty()) {
        SPDLOG_LOGGER_ERROR(
          logger, "enqueue dropped due bytes empty, conn_id: {0} data_ctx_id: {1}", conn_id, data_ctx_id);
        return TransportError::kNone;
    }

    std::lock_guard<std::mutex> _(state_mutex_);

    const auto conn_ctx_it = conn_context_.find(conn_id);
    if (conn_ctx_it == conn_context_.end()) {
        return TransportError::kInvalidConnContextId;
    }

    const auto data_ctx_it = conn_ctx_it->second.active_data_contexts.find(data_ctx_id);
    if (data_ctx_it == conn_ctx_it->second.active_data_contexts.end()) {
        return TransportError::kInvalidDataContextId;
    }

    data_ctx_it->second.priority = priority; // Match object priority for next stream create

    data_ctx_it->second.metrics.enqueued_objs++;

    if (flags.use_reliable) {
        StreamAction stream_action{ StreamAction::kNoAction };

        if (flags.new_stream) {
            data_ctx_it->second.tx_start_stream = true;

            if (flags.use_reset) {
                stream_action = StreamAction::kReplaceStreamUseReset;
            } else {
                stream_action = StreamAction::kReplaceStreamUseFin;
            }
        }

        if (flags.clear_tx_queue) {
            data_ctx_it->second.metrics.tx_queue_discards += data_ctx_it->second.tx_data->Size();
            data_ctx_it->second.tx_data->Clear();
        }

        ConnData cd{ conn_id, data_ctx_id, priority, stream_action, std::move(bytes), tick_service_->Microseconds() };
        data_ctx_it->second.tx_data->Push(group_id, std::move(cd), ttl_ms, priority, 0);

        if (!data_ctx_it->second.mark_stream_active) {
            data_ctx_it->second.mark_stream_active = true;

            RunPqFunction([this, conn_id, data_ctx_id]() {
                MarkStreamActive(conn_id, data_ctx_id);
                return 0;
            });
        }
    } else { // datagram
        ConnData cd{ conn_id,          data_ctx_id,
                     priority,         StreamAction::kNoAction,
                     std::move(bytes), tick_service_->Microseconds() };
        conn_ctx_it->second.dgram_tx_data->Push(group_id, std::move(cd), ttl_ms, priority, 0);

        if (!conn_ctx_it->second.mark_dgram_ready) {
            conn_ctx_it->second.mark_dgram_ready = true;

            RunPqFunction([this, conn_id]() {
                MarkDgramReady(conn_id);
                return 0;
            });
        }
    }

    return TransportError::kNone;
}

std::shared_ptr<StreamRxContext>
PicoQuicTransport::GetStreamRxContext(TransportConnId conn_id, uint64_t stream_id)
{
    std::lock_guard<std::mutex> _(state_mutex_);

    const auto conn_ctx_it = conn_context_.find(conn_id);
    if (conn_ctx_it == conn_context_.end()) {
        throw TransportException(TransportError::kInvalidConnContextId);
    }

    const auto sbuf_it = conn_ctx_it->second.rx_stream_buffer.find(stream_id);
    if (sbuf_it != conn_ctx_it->second.rx_stream_buffer.end()) {
        return sbuf_it->second.rx_ctx;
    }

    throw TransportException(TransportError::kInvalidStreamId);
}

std::shared_ptr<const std::vector<uint8_t>>
PicoQuicTransport::Dequeue(TransportConnId conn_id, [[maybe_unused]] std::optional<DataContextId> data_ctx_id)
{
    std::lock_guard<std::mutex> _(state_mutex_);

    const auto conn_ctx_it = conn_context_.find(conn_id);
    if (conn_ctx_it == conn_context_.end()) {
        return {};
    }

    auto data = conn_ctx_it->second.dgram_rx_data->Pop();
    if (data.has_value()) {
        return *data;
    }

    return {};
}

DataContextId
PicoQuicTransport::CreateDataContext(const TransportConnId conn_id,
                                     bool use_reliable_transport,
                                     uint8_t priority,
                                     bool bidir)
{
    std::lock_guard<std::mutex> _(state_mutex_);

    if (priority > 127) {
        auto new_priority = priority >> 1;
        SPDLOG_LOGGER_DEBUG(
          logger, "Priority is greater than allowed by picoquic, adjusting from {} to {}", priority, new_priority);
        priority = new_priority;
    }

    const auto conn_it = conn_context_.find(conn_id);
    if (conn_it == conn_context_.end()) {
        SPDLOG_LOGGER_ERROR(logger, "Invalid conn_id: {0}, cannot create data context", conn_id);
        // TODO (tim): Should we return an error code here instead of returning 0, as it might be
        // misleading to the caller.
        return 0;
    }

    const auto [data_ctx_it, is_new] =
      conn_it->second.active_data_contexts.emplace(conn_it->second.next_data_ctx_id, DataContext{});

    if (is_new) {
        // Init context
        data_ctx_it->second.conn_id = conn_id;
        data_ctx_it->second.is_bidir = bidir;
        data_ctx_it->second.data_ctx_id = conn_it->second.next_data_ctx_id++; // Set and bump next data_ctx_id

        data_ctx_it->second.priority = priority;

        data_ctx_it->second.uses_reset_wait = tconfig_.use_reset_wait_strategy;

        data_ctx_it->second.tx_data = std::make_unique<PriorityQueue<ConnData>>(tconfig_.time_queue_max_duration,
                                                                                tconfig_.time_queue_bucket_interval,
                                                                                tick_service_,
                                                                                tconfig_.time_queue_init_queue_size);

        // Create stream
        if (use_reliable_transport) {
            CreateStream(conn_it->second, &data_ctx_it->second);

            SPDLOG_LOGGER_DEBUG(logger,
                                "Created reliable data context id: {} stream_id: {}, pri: {}",
                                data_ctx_it->second.data_ctx_id,
                                data_ctx_it->second.current_stream_id.value(),
                                static_cast<int>(priority));
        } else {
            picoquic_set_datagram_priority(conn_it->second.pq_cnx, priority);
            SPDLOG_LOGGER_DEBUG(logger,
                                "Created DGRAM data context id: {} pri: {}",
                                data_ctx_it->second.data_ctx_id,
                                static_cast<int>(priority));
        }
    }

    return data_ctx_it->second.data_ctx_id;
}

void
PicoQuicTransport::Close(const TransportConnId& conn_id, uint64_t app_reason_code)
{
    std::lock_guard<std::mutex> _(state_mutex_);

    const auto conn_it = conn_context_.find(conn_id);

    if (conn_it == conn_context_.end())
        return;

    // Remove pointer references in picoquic for active streams
    for (const auto& [stream_id, rx_buf] : conn_it->second.rx_stream_buffer) {
        picoquic_mark_active_stream(conn_it->second.pq_cnx, stream_id, 0, NULL);
        picoquic_unlink_app_stream_ctx(conn_it->second.pq_cnx, stream_id);

        if (!rx_buf.closed) {
            picoquic_reset_stream(conn_it->second.pq_cnx, stream_id, 0);
        }
    }

    // Only one datagram context is per connection, if it's deleted, then the connection is to be terminated
    switch (app_reason_code) {
        case 1: // idle timeout
            OnConnectionStatus(conn_id, TransportStatus::kIdleTimeout);
            break;

        case 100: // Client shutting down connection
            OnConnectionStatus(conn_id, TransportStatus::kRemoteRequestClose);
            break;

        default:
            OnConnectionStatus(conn_id, TransportStatus::kDisconnected);
            break;
    }

    if (not is_server_mode) {
        SetStatus(TransportStatus::kShutdown);
    }

    // Cleanup client-owned WebTransport h3_ctx before closing connection
    // Server-side h3_ctx is managed by h3zero library and shared across connections
    if (conn_it->second.wt_h3_ctx_owned && conn_it->second.wt_h3_ctx) {
        SPDLOG_LOGGER_DEBUG(logger, "Cleaning up client-owned h3_ctx for connection {}", conn_id);
        // Note: h3zero_callback_delete_context may not exist in all versions
        // The h3zero library typically cleans this up automatically on connection close
        // So we just mark it as null here
        conn_it->second.wt_h3_ctx = nullptr;
    }

    picoquic_close(conn_it->second.pq_cnx, app_reason_code);

    conn_context_.erase(conn_it);
}

void
PicoQuicTransport::SetRemoteDataCtxId([[maybe_unused]] const TransportConnId conn_id,
                                      [[maybe_unused]] const DataContextId data_ctx_id,
                                      [[maybe_unused]] const DataContextId remote_data_ctx_id)
{
    return;
}

void
PicoQuicTransport::SetDataCtxPriority(const TransportConnId conn_id, DataContextId data_ctx_id, uint8_t priority)
{
    std::lock_guard<std::mutex> _(state_mutex_);

    const auto conn_it = conn_context_.find(conn_id);

    if (conn_it == conn_context_.end())
        return;

    const auto data_ctx_it = conn_it->second.active_data_contexts.find(data_ctx_id);
    if (data_ctx_it == conn_it->second.active_data_contexts.end())
        return;

    SPDLOG_LOGGER_DEBUG(logger,
                        "Set data context priority to {0}  conn_id: {1} data_ctx_id: {2}",
                        static_cast<int>(priority),
                        conn_id,
                        data_ctx_id);

    data_ctx_it->second.priority = priority;
}

void
PicoQuicTransport::SetStreamIdDataCtxId(const TransportConnId conn_id, DataContextId data_ctx_id, uint64_t stream_id)
{
    std::lock_guard<std::mutex> _(state_mutex_);

    const auto conn_it = conn_context_.find(conn_id);

    if (conn_it == conn_context_.end())
        return;

    const auto data_ctx_it = conn_it->second.active_data_contexts.find(data_ctx_id);
    if (data_ctx_it == conn_it->second.active_data_contexts.end())
        return;

    SPDLOG_LOGGER_DEBUG(logger,
                        "Set data context to stream conn_id: {0} data_ctx_id: {1} stream_id: {2}",
                        conn_id,
                        data_ctx_id,
                        stream_id);

    data_ctx_it->second.current_stream_id = stream_id;

    // For WebTransport mode, also update the stream-to-data-context mapping
    if (conn_it->second.transport_mode == TransportMode::kWebTransport) {
        conn_it->second.wt_stream_to_data_ctx[stream_id] = data_ctx_id;
    }

    RunPqFunction([=]() {
        if (conn_it->second.pq_cnx != nullptr)
            picoquic_set_app_stream_ctx(conn_it->second.pq_cnx, stream_id, &data_ctx_it->second);
        return 0;
    });
}

/* ============================================================================
 * Public internal methods used by picoquic
 * ============================================================================
 */

PicoQuicTransport::ConnectionContext*
PicoQuicTransport::GetConnContext(const TransportConnId& conn_id)
{
    // Locate the specified transport connection context
    auto it = conn_context_.find(conn_id);

    // If not found, return empty context
    if (it == conn_context_.end())
        return nullptr;

    return &it->second;
}

PicoQuicTransport::ConnectionContext&
PicoQuicTransport::CreateConnContext(picoquic_cnx_t* pq_cnx)
{
    /*
     * @note: This is thread safe because picoquic network thread is the only one that calls this
     */

    auto [conn_it, is_new] = conn_context_.emplace(reinterpret_cast<TransportConnId>(pq_cnx), pq_cnx);

    sockaddr* addr;

    auto& conn_ctx = conn_it->second;
    conn_ctx.conn_id = reinterpret_cast<TransportConnId>(pq_cnx);
    conn_ctx.pq_cnx = pq_cnx;

    // For servers, determine transport mode based on negotiated ALPN
    if (is_server_mode) {
        const char* negotiated_alpn = picoquic_tls_get_negotiated_alpn(pq_cnx);
        if (negotiated_alpn) {
            if (strcmp(negotiated_alpn, webtransport_alpn) == 0) {
                conn_ctx.transport_mode = TransportMode::kWebTransport;
                SPDLOG_LOGGER_INFO(logger, "Server connection using WebTransport (ALPN: {})", negotiated_alpn);
                // Notify the transport delegate about WebTransport mode
                if (auto transport = dynamic_cast<Transport*>(&delegate_)) {
                    transport->SetWebTransportMode(conn_ctx.conn_id, true);
                }
            } else if (strcmp(negotiated_alpn, quicr_alpn) == 0) {
                conn_ctx.transport_mode = TransportMode::kQuic;
                SPDLOG_LOGGER_INFO(logger, "Server connection using raw QUIC (ALPN: {})", negotiated_alpn);
            } else {
                conn_ctx.transport_mode = TransportMode::kQuic; // Default fallback
                SPDLOG_LOGGER_WARN(logger, "Unknown ALPN: {}, defaulting to raw QUIC", negotiated_alpn);
            }
        } else {
            conn_ctx.transport_mode = TransportMode::kQuic; // Default fallback
            SPDLOG_LOGGER_WARN(logger, "No ALPN negotiated, defaulting to raw QUIC");
        }
    } else {
        // For clients, use the configured transport mode
        conn_ctx.transport_mode = transport_mode; // Notify the transport delegate about WebTransport mode for clients
        if (transport_mode == TransportMode::kWebTransport) {
            if (auto transport = dynamic_cast<Transport*>(&delegate_)) {
                transport->SetWebTransportMode(conn_ctx.conn_id, true);
            }
        }
    }

    picoquic_get_peer_addr(pq_cnx, &addr);
    std::memset(conn_ctx.peer_addr_text, 0, sizeof(conn_ctx.peer_addr_text));
    std::memcpy(&conn_ctx.peer_addr, addr, sizeof(conn_ctx.peer_addr));

    switch (addr->sa_family) {
        case AF_INET:
            (void)inet_ntop(AF_INET,
                            &reinterpret_cast<struct sockaddr_in*>(addr)->sin_addr,
                            /*(const void*)(&((struct sockaddr_in*)addr)->sin_addr),*/
                            conn_ctx.peer_addr_text,
                            sizeof(conn_ctx.peer_addr_text));
            conn_ctx.peer_port = ntohs(((struct sockaddr_in*)addr)->sin_port); // NOLINT (include)
            break;

        case AF_INET6:
            (void)inet_ntop(AF_INET6,
                            &reinterpret_cast<struct sockaddr_in6*>(addr)->sin6_addr,
                            /*(const void*)(&((struct sockaddr_in6*)addr)->sin6_addr), */
                            conn_ctx.peer_addr_text,
                            sizeof(conn_ctx.peer_addr_text));
            conn_ctx.peer_port = ntohs(((struct sockaddr_in6*)addr)->sin6_port);
            break;
    }

    if (is_new) {
        SPDLOG_LOGGER_INFO(logger, "Created new connection context for conn_id: {0}", conn_ctx.conn_id);

        conn_ctx.dgram_rx_data->SetLimit(tconfig_.time_queue_rx_size);
        conn_ctx.dgram_tx_data = std::make_shared<PriorityQueue<ConnData>>(tconfig_.time_queue_max_duration,
                                                                           tconfig_.time_queue_bucket_interval,
                                                                           tick_service_,
                                                                           tconfig_.time_queue_init_queue_size);
    }

    return conn_ctx;
}

PicoQuicTransport::PicoQuicTransport(const TransportRemote& server,
                                     const TransportConfig& tcfg,
                                     TransportDelegate& delegate,
                                     bool is_server_mode,
                                     std::shared_ptr<TickService> tick_service,
                                     std::shared_ptr<spdlog::logger> logger,
                                     TransportMode transport_mode)
  : logger(std::move(logger))
  , is_server_mode(is_server_mode)
  , transport_mode(transport_mode)
  , stop_(false)
  , transportStatus_(TransportStatus::kConnecting)
  , serverInfo_(server)
  , delegate_(delegate)
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

PicoQuicTransport::DataContext*
PicoQuicTransport::CreateDataContextBiDirRecv(TransportConnId conn_id, uint64_t stream_id)
{
    std::lock_guard<std::mutex> _(state_mutex_);

    const auto conn_it = conn_context_.find(conn_id);
    if (conn_it == conn_context_.end()) {
        SPDLOG_LOGGER_ERROR(logger, "Invalid conn_id: {0}, cannot create data context", conn_id);
        return nullptr;
    }

    const auto [data_ctx_it, is_new] =
      conn_it->second.active_data_contexts.emplace(conn_it->second.next_data_ctx_id, DataContext{});

    if (is_new) {
        // Init context
        data_ctx_it->second.conn_id = conn_id;
        data_ctx_it->second.is_bidir = true;
        data_ctx_it->second.data_ctx_id = conn_it->second.next_data_ctx_id++; // Set and bump next data_ctx_id

        data_ctx_it->second.priority = 1; // TODO: Need to get priority from remote

        data_ctx_it->second.tx_data = std::make_unique<PriorityQueue<ConnData>>(tconfig_.time_queue_max_duration,
                                                                                tconfig_.time_queue_bucket_interval,
                                                                                tick_service_,
                                                                                tconfig_.time_queue_init_queue_size);

        data_ctx_it->second.current_stream_id = stream_id;

        cbNotifyQueue_.Push([=, data_ctx_id = data_ctx_it->second.data_ctx_id, this]() {
            delegate_.OnNewDataContext(conn_id, data_ctx_id);
        });

        SPDLOG_LOGGER_INFO(logger,
                           "Created new bidir data context conn_id: {0} data_ctx_id: {1} stream_id: {2}",
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
PicoQuicTransport::DeleteDataContextInternal(TransportConnId conn_id, DataContextId data_ctx_id, bool delete_on_empty)
{
    const auto conn_it = conn_context_.find(conn_id);

    if (conn_it == conn_context_.end())
        return;

    const auto data_ctx_it = conn_it->second.active_data_contexts.find(data_ctx_id);
    if (data_ctx_it == conn_it->second.active_data_contexts.end())
        return;

    SPDLOG_LOGGER_DEBUG(logger,
                        "Delete data context {} in conn_id: {} doe: {} / {}",
                        data_ctx_id,
                        conn_id,
                        delete_on_empty,
                        data_ctx_it->second.delete_on_empty);

    if (delete_on_empty && !data_ctx_it->second.tx_data->Empty()) {
        data_ctx_it->second.delete_on_empty = true;
    } else {
        SPDLOG_LOGGER_DEBUG(logger, "Delete data context {0} in conn_id: {1}", data_ctx_id, conn_id);

        CloseStream(conn_it->second, &data_ctx_it->second, false);
        conn_it->second.active_data_contexts.erase(data_ctx_it);
    }
}

void
PicoQuicTransport::DeleteDataContext(const TransportConnId& conn_id, DataContextId data_ctx_id, bool delete_on_empty)
{
    if (data_ctx_id == 0) {
        return; // use close() instead of deleting default/datagram context
    }

    /*
     * Race conditions exist with picoquic thread callbacks that will cause a problem if the context (pointer context)
     *    is deleted outside of the picoquic thread. Below schedules the delete to be done within the picoquic thread.
     */
    RunPqFunction([=, this]() {
        DeleteDataContextInternal(conn_id, data_ctx_id, delete_on_empty);
        return 0;
    });
}

void
PicoQuicTransport::SendNextDatagram(ConnectionContext* conn_ctx, uint8_t* bytes_ctx, size_t max_len)
{
    if (bytes_ctx == nullptr) {
        return;
    }

    const bool is_webtransport = conn_ctx->transport_mode == TransportMode::kWebTransport;

    // Helper lambda to get datagram buffer based on transport mode
    auto provide_buffer = [is_webtransport, bytes_ctx](size_t length, bool more_data) -> uint8_t* {
        if (is_webtransport) {
            return h3zero_provide_datagram_buffer(bytes_ctx, length, more_data ? 1 : 0);
        } else {
            return picoquic_provide_datagram_buffer_ex(
              bytes_ctx, length, more_data ? picoquic_datagram_active_any_path : picoquic_datagram_not_active);
        }
    };

    TimeQueueElement<ConnData> out_data;
    conn_ctx->dgram_tx_data->Front(out_data);
    if (out_data.has_value) {
        const auto data_ctx_it = conn_ctx->active_data_contexts.find(out_data.value.data_ctx_id);
        if (data_ctx_it == conn_ctx->active_data_contexts.end()) {
            SPDLOG_LOGGER_DEBUG(logger,
                                "send_next_dgram has no data context conn_id: {0} data len: {1} dropping",
                                conn_ctx->conn_id,
                                out_data.value.data->size());
            conn_ctx->metrics.tx_dgram_drops++;
            return;
        }

        CheckCallbackDelta(&data_ctx_it->second);

        if (out_data.value.data->size() == 0) {
            SPDLOG_LOGGER_ERROR(logger,
                                "conn_id: {0} data_ctx_id: {1} priority: {2} has ZERO data size",
                                data_ctx_it->second.conn_id,
                                data_ctx_it->second.data_ctx_id,
                                static_cast<int>(data_ctx_it->second.priority));
            data_ctx_it->second.tx_data->Pop();
            return;
        }

        data_ctx_it->second.metrics.tx_queue_expired += out_data.expired_count;

        if (out_data.value.data->size() <= max_len) {
            conn_ctx->dgram_tx_data->Pop();

            data_ctx_it->second.metrics.tx_object_duration_us.AddValue(tick_service_->Microseconds() -
                                                                       out_data.value.tick_microseconds);
            data_ctx_it->second.metrics.tx_dgrams_bytes += out_data.value.data->size();
            data_ctx_it->second.metrics.tx_dgrams++;

            bool more_data = !conn_ctx->dgram_tx_data->Empty();
            uint8_t* buf = provide_buffer(out_data.value.data->size(), more_data);

            if (buf != nullptr) {
                std::memcpy(buf, out_data.value.data->data(), out_data.value.data->size());
            }
        } else {
            RunPqFunction([this, conn_id = conn_ctx->conn_id]() {
                MarkDgramReady(conn_id);
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

bool
PicoQuicTransport::StreamActionCheck(DataContext* data_ctx, StreamAction stream_action)
{
    if (data_ctx == nullptr) {
        // Cannot proceed if there is no data context, this normal for receive streams
        return false;
    }

    switch (stream_action) {
        case StreamAction::kNoAction:
            [[fallthrough]];
        default:
            if (!data_ctx->current_stream_id.has_value()) {
                SPDLOG_LOGGER_INFO(logger, "Creating unset stream in conn_id: {0}", data_ctx->conn_id);
                const auto conn_ctx = GetConnContext(data_ctx->conn_id);
                CreateStream(*conn_ctx, data_ctx);

                return true; // Indicate that a new stream was created
            }
            break;

        case StreamAction::kReplaceStreamUseReset: {
            std::lock_guard<std::mutex> _(state_mutex_);
            const auto conn_ctx = GetConnContext(data_ctx->conn_id);

            auto existing_stream_id = data_ctx->current_stream_id.has_value() ? *data_ctx->current_stream_id : 0;
            CloseStream(*conn_ctx, data_ctx, true);
            CreateStream(*conn_ctx, data_ctx);

            SPDLOG_LOGGER_DEBUG(
              logger,
              "Replacing stream using RESET; conn_id: {} data_ctx_id: {} existing_stream: {} new_stream_id: {} "
              "write buf drops: {} tx_queue_discards: {}",
              data_ctx->conn_id,
              data_ctx->data_ctx_id,
              existing_stream_id,
              *data_ctx->current_stream_id,
              data_ctx->metrics.tx_buffer_drops,
              data_ctx->metrics.tx_queue_discards);

            data_ctx->tx_reset_wait_discard = false; // Allow new object to be sent
            data_ctx->mark_stream_active = false;
            return true; // New stream requires PQ to callback again using that stream
        }

        case StreamAction::kReplaceStreamUseFin: {
            if (data_ctx->stream_tx_object != nullptr) {
                data_ctx->metrics.tx_buffer_drops++;
            }

            SPDLOG_LOGGER_DEBUG(logger,
                                "Replacing stream using FIN; conn_id: {0} existing_stream: {1}",
                                data_ctx->conn_id,
                                data_ctx->current_stream_id.has_value() ? *data_ctx->current_stream_id : 0);

            std::lock_guard<std::mutex> _(state_mutex_);

            const auto conn_ctx = GetConnContext(data_ctx->conn_id);
            CloseStream(*conn_ctx, data_ctx, false);
            CreateStream(*conn_ctx, data_ctx);

            data_ctx->tx_reset_wait_discard = false; // Allow new object to be sent
            data_ctx->mark_stream_active = false;
            return true; // New stream requires PQ to callback again using that stream
        }
    }

    return false;
}

void
PicoQuicTransport::SendStreamBytes(DataContext* data_ctx, uint8_t* bytes_ctx, size_t max_len)
{
    if (bytes_ctx == NULL) {
        return;
    }

    if (max_len < 20 && data_ctx == nullptr && data_ctx->tx_start_stream) {
        return;
    }

    SPDLOG_LOGGER_TRACE(logger,
                        "SendStreamBytes conn_id: {} data_ctx_id: {} bytes_len: {}",
                        data_ctx->conn_id,
                        data_ctx->data_ctx_id,
                        max_len);

    uint32_t data_len = 0; /// Length of data to follow the 4 byte length
    size_t offset = 0;
    int is_still_active = 0;

    CheckCallbackDelta(data_ctx);

    TimeQueueElement<ConnData> obj;

    if (data_ctx != nullptr && data_ctx->tx_reset_wait_discard) { // Drop TX objects till next reset/new stream
        data_ctx->tx_data->Front(obj);
        if (obj.has_value) {
            data_ctx->metrics.tx_queue_discards++;

            if (obj.value.stream_action == StreamAction::kReplaceStreamUseFin ||
                obj.value.stream_action == StreamAction::kReplaceStreamUseReset) {

                std::lock_guard<std::mutex> _(state_mutex_);
                const auto conn_ctx = GetConnContext(data_ctx->conn_id);
                if (!conn_ctx->is_congested) {
                    data_ctx->tx_reset_wait_discard = false;
                    data_ctx->ResetTxObject();
                } else {
                    data_ctx->tx_data->Pop(); // discard when in current stream

                    if (!data_ctx->tx_data->Empty()) {
                        RunPqFunction([this, conn_id = data_ctx->conn_id, data_ctx_id = data_ctx->data_ctx_id]() {
                            MarkStreamActive(conn_id, data_ctx_id);
                            return 0;
                        });
                    }
                    return;
                }
            }
        }

        data_ctx->mark_stream_active = false;
    }

    defer(if (data_ctx->tx_data->Empty() && data_ctx->delete_on_empty) {
        DeleteDataContextInternal(data_ctx->conn_id, data_ctx->data_ctx_id, false);
    });

    if (data_ctx->stream_tx_object == nullptr) {
        SPDLOG_LOGGER_TRACE(logger,
                            "SendStreamBytes conn_id: {} data_ctx_id: {} stream_tx_object is nullptr",
                            data_ctx->conn_id,
                            data_ctx->data_ctx_id);

        data_ctx->tx_data->PopFront(obj);

        if (obj.expired_count) {
            data_ctx->tx_reset_wait_discard = true;
            data_ctx->metrics.tx_queue_expired += obj.expired_count;
            SPDLOG_LOGGER_DEBUG(logger,
                                "Send stream objects expired; conn_id: {} data_ctx_id: {} expired: {} queue_size: {}",
                                data_ctx->conn_id,
                                data_ctx->data_ctx_id,
                                obj.expired_count,
                                data_ctx->tx_data->Size());
            data_ctx->ResetTxObject();
            return;
        }

        if (obj.has_value) {
            if (obj.value.data->size() == 0) {
                SPDLOG_LOGGER_ERROR(logger,
                                    "conn_id: {0} data_ctx_id: {1} priority: {2} stream has ZERO data size",
                                    data_ctx->conn_id,
                                    data_ctx->data_ctx_id,
                                    static_cast<int>(data_ctx->priority));
                return;
            }

            data_ctx->stream_tx_object_offset = 0;
            data_ctx->metrics.tx_stream_objects++;
            data_ctx->metrics.tx_object_duration_us.AddValue(tick_service_->Microseconds() -
                                                             obj.value.tick_microseconds);

            if (StreamActionCheck(data_ctx, obj.value.stream_action)) {
                data_ctx->stream_tx_object = std::move(obj.value.data);
                SPDLOG_LOGGER_TRACE(logger,
                                    "New Stream conn_id: {} data_ctx_id: {} stream_id: {}, object size: {}",
                                    data_ctx->conn_id,
                                    data_ctx->data_ctx_id,
                                    *data_ctx->current_stream_id,
                                    data_ctx->stream_tx_object->size());
                return;
            } else if (obj.value.stream_action != StreamAction::kNoAction) {
                SPDLOG_LOGGER_TRACE(
                  logger,
                  "Object wants New Stream conn_id: {} data_ctx_id: {} stream_id: {}, object size: {} queue_size: {}",
                  data_ctx->conn_id,
                  data_ctx->data_ctx_id,
                  *data_ctx->current_stream_id,
                  obj.value.data->size(),
                  data_ctx->tx_data->Size());
            }

            data_ctx->stream_tx_object = std::move(obj.value.data);
            data_ctx->tx_start_stream = false;

        } else {
            picoquic_provide_stream_data_buffer(bytes_ctx, 0, 0, not data_ctx->tx_data->Empty());
            return;
        }
    }

    data_len = data_ctx->stream_tx_object->size() - data_ctx->stream_tx_object_offset;
    offset = data_ctx->stream_tx_object_offset;

    if (data_len > max_len) {
        data_ctx->stream_tx_object_offset += max_len;
        data_len = max_len;
        is_still_active = 1;

    } else {
        data_ctx->stream_tx_object_offset = 0;
    }

    data_ctx->metrics.tx_stream_bytes += data_len;

    if (!is_still_active && !data_ctx->tx_data->Empty())
        is_still_active = 1;

    uint8_t* buf = nullptr;

    buf = picoquic_provide_stream_data_buffer(bytes_ctx, data_len, 0, is_still_active);

    if (buf == NULL) {
        // Error allocating memory to write
        SPDLOG_LOGGER_ERROR(logger,
                            "conn_id: {0} data_ctx_id: {1} priority: {2} unable to allocate pq buffer size: {3}",
                            data_ctx->conn_id,
                            data_ctx->data_ctx_id,
                            static_cast<int>(data_ctx->priority),
                            data_len);
        return;
    }

    // Write data
    std::memcpy(buf, data_ctx->stream_tx_object->data() + offset, data_len);

    if (data_ctx->stream_tx_object_offset == 0 && data_ctx->stream_tx_object != nullptr) {
        // Zero offset at this point means the object was fully sent
        data_ctx->ResetTxObject();
    }
}

void
PicoQuicTransport::OnConnectionStatus(const TransportConnId conn_id, const TransportStatus status)
{
    if (status == TransportStatus::kReady) {
        auto conn_ctx = GetConnContext(conn_id);
        SPDLOG_LOGGER_INFO(logger, "Connection established to server {0}", conn_ctx->peer_addr_text);
    }

    cbNotifyQueue_.Push([=, this]() { delegate_.OnConnectionStatus(conn_id, status); });
}

void
PicoQuicTransport::OnNewConnection(const TransportConnId conn_id)
{
    auto conn_ctx = GetConnContext(conn_id);
    if (!conn_ctx)
        return;

    SPDLOG_LOGGER_INFO(
      logger, "New Connection {0} port: {1} conn_id: {2}", conn_ctx->peer_addr_text, conn_ctx->peer_port, conn_id);

    TransportRemote remote{ .host_or_ip = conn_ctx->peer_addr_text,
                            .port = conn_ctx->peer_port,
                            .proto = TransportProtocol::kQuic };

    picoquic_enable_keep_alive(conn_ctx->pq_cnx, tconfig_.idle_timeout_ms * 500);
    picoquic_set_feedback_loss_notification(conn_ctx->pq_cnx, 1);

#if 0
    // Setup WebTransport for server connections if needed
    if (conn_ctx->transport_mode == TransportMode::kWebTransport) {
        if (auto wt_ret = SetupWebTransportConnection(conn_ctx->pq_cnx); wt_ret != 0) {
            SPDLOG_LOGGER_ERROR(logger, "Failed to setup WebTransport connection for server");
        }
    } else {
        picoquic_set_callback(conn_ctx->pq_cnx, PqEventCb, this);
    }
#endif

    if (tconfig_.quic_priority_limit > 0) {
        SPDLOG_LOGGER_INFO(
          logger, "Setting priority bypass limit to {0}", static_cast<int>(tconfig_.quic_priority_limit));
        picoquic_set_priority_limit_for_bypass(conn_ctx->pq_cnx, tconfig_.quic_priority_limit);
    }

    cbNotifyQueue_.Push([=, this]() { delegate_.OnNewConnection(conn_id, remote); });
}

void
PicoQuicTransport::OnRecvDatagram(ConnectionContext* conn_ctx, uint8_t* bytes, size_t length)
try {
    if (length == 0) {
        return;
    }

    if (conn_ctx == nullptr) {
        SPDLOG_LOGGER_WARN(logger, "DGRAM received with NULL connection context; dropping length: {0}", length);
        return;
    }

    conn_ctx->dgram_rx_data->Push(std::make_shared<const std::vector<uint8_t>>(bytes, bytes + length));
    conn_ctx->metrics.rx_dgrams++;
    conn_ctx->metrics.rx_dgrams_bytes += length;

    if (cbNotifyQueue_.Size() > 1000) {
        SPDLOG_LOGGER_INFO(logger, "on_recv_datagram cbNotifyQueue size {0}", cbNotifyQueue_.Size());
    }

    if (conn_ctx->dgram_rx_data->Size() < 10 &&
        !cbNotifyQueue_.Push([=, this]() { delegate_.OnRecvDgram(conn_ctx->conn_id, std::nullopt); })) {
        SPDLOG_LOGGER_ERROR(logger, "conn_id: {0} DGRAM notify queue is full", conn_ctx->conn_id);
    }
} catch (const std::exception& e) {
    SPDLOG_LOGGER_ERROR(logger, "Caught exception in OnRecvDatagram. (error={})", e.what());
    // TODO(tievens): Add metrics to track if this happens
}

void
PicoQuicTransport::OnRecvStreamBytes(ConnectionContext* conn_ctx,
                                     DataContext* data_ctx,
                                     uint64_t stream_id,
                                     int is_fin,
                                     std::span<const uint8_t> bytes)
try {
    // Handle application stream data
    if (bytes.empty()) {
        SPDLOG_LOGGER_DEBUG(logger, "on_recv_stream_bytes length is ZERO");
        return;
    }

    std::lock_guard<std::mutex> l(state_mutex_);

    // Handle control stream message processing for WebTransport mode
    if (conn_ctx->transport_mode == TransportMode::kWebTransport && conn_ctx->wt_control_stream_ctx != nullptr &&
        stream_id == conn_ctx->wt_control_stream_ctx->stream_id) {

        SPDLOG_LOGGER_INFO(logger,
                           "OnRecvStreamBytes: Received data on control stream {} for conn_id={}, len={}",
                           stream_id,
                           conn_ctx->conn_id,
                           bytes.size());

        // Parse the capsule data using picowt_receive_capsule
        // This accumulates partial capsule data across multiple calls
        int ret = picowt_receive_capsule(conn_ctx->pq_cnx,
                                         conn_ctx->wt_control_stream_ctx,
                                         bytes.data(),
                                         bytes.data() + bytes.size(),
                                         &conn_ctx->wt_capsule);

        if (ret != 0) {
            SPDLOG_LOGGER_ERROR(logger,
                                "OnRecvStreamBytes: Failed to parse capsule on control stream {} for conn_id={}",
                                stream_id,
                                 conn_ctx->conn_id);
            picowt_release_capsule(&conn_ctx->wt_capsule);
            return;
        }

        // Check if capsule is fully received and stored
        if (conn_ctx->wt_capsule.h3_capsule.is_stored) {
            SPDLOG_LOGGER_INFO(
              logger,
              "OnRecvStreamBytes: Received capsule type={} error_code={} on control stream {} for conn_id={}",
              conn_ctx->wt_capsule.h3_capsule.capsule_type,
              conn_ctx->wt_capsule.error_code,
              stream_id,
              conn_ctx->conn_id);

            if (is_fin) {
                // Mark FIN received on control stream
                conn_ctx->wt_control_stream_ctx->ps.stream_state.is_fin_received = 1;

                if (!is_server_mode) {
                    // Client: close the connection
                    SPDLOG_LOGGER_INFO(
                      logger,
                      "OnRecvStreamBytes: Client received control stream capsule, closing connection {}",
                      conn_ctx->conn_id);
                    picoquic_close(conn_ctx->pq_cnx, 0);
                } else {
                    // Server: send FIN back on control stream if not already sent
                    if (!conn_ctx->wt_control_stream_ctx->ps.stream_state.is_fin_sent) {
                        SPDLOG_LOGGER_INFO(logger,
                                           "OnRecvStreamBytes: Server sending FIN on control stream {} for conn_id={}",
                                           stream_id,
                                           conn_ctx->conn_id);
                        picoquic_add_to_stream(conn_ctx->pq_cnx, stream_id, NULL, 0, 1);
                    }
                    // Delete the stream prefix for this WebTransport session
                    if (conn_ctx->wt_h3_ctx != nullptr) {
                        h3zero_delete_stream_prefix(conn_ctx->pq_cnx, conn_ctx->wt_h3_ctx, stream_id);
                    }
                }

                // Release the capsule resources
                picowt_release_capsule(&conn_ctx->wt_capsule);

                // Notify the delegate that the connection is closing
                OnConnectionStatus(conn_ctx->conn_id, TransportStatus::kDisconnected);
            }
        }

        return;
    }

    auto rx_buf_it = conn_ctx->rx_stream_buffer.find(stream_id);
    if (rx_buf_it == conn_ctx->rx_stream_buffer.end()) {
        if (bytes.size() < kMinStreamBytesForSend) {
            SPDLOG_LOGGER_DEBUG(logger,
                                "bytes received from picoquic stream {} len: {} is too small to process stream header",
                                stream_id,
                                bytes.size());
        }
        conn_ctx->rx_stream_buffer.try_emplace(stream_id);
        conn_ctx->rx_stream_buffer[stream_id].rx_ctx->data_queue.SetLimit(tconfig_.time_queue_rx_size);
    }

    auto& rx_buf = conn_ctx->rx_stream_buffer[stream_id];

    if (rx_buf.rx_ctx->unknown_expiry_tick_ms &&
        tick_service_->Milliseconds() > rx_buf.rx_ctx->unknown_expiry_tick_ms) {
        SPDLOG_LOGGER_DEBUG(logger,
                            "Stream is unknown and now has expired, resetting stream {} expiry {}ms > {}ms",
                            stream_id,
                            rx_buf.rx_ctx->unknown_expiry_tick_ms,
                            tick_service_->Milliseconds());
        picoquic_reset_stream_ctx(conn_ctx->pq_cnx, stream_id);
        picoquic_reset_stream(conn_ctx->pq_cnx, stream_id, static_cast<uint64_t>(StreamErrorCodes::kUnknownExpiry));
        rx_buf.closed = true;

        return;
    }

    rx_buf.rx_ctx->data_queue.Push(std::make_shared<const std::vector<uint8_t>>(bytes.begin(), bytes.end()));

    if (data_ctx != nullptr) {
        data_ctx->metrics.rx_stream_cb++;
        data_ctx->metrics.rx_stream_bytes += bytes.size();

        if (rx_buf.rx_ctx->data_queue.Size() < 10 && !cbNotifyQueue_.Push([=, this]() {
                delegate_.OnRecvStream(conn_ctx->conn_id, stream_id, data_ctx->data_ctx_id, data_ctx->is_bidir);
            })) {

            SPDLOG_LOGGER_ERROR(
              logger, "conn_id: {0} stream_id: {1} notify queue is full", conn_ctx->conn_id, stream_id);
        }

    } else {
        // When data_ctx is null, determine if stream is bidirectional from stream_id
        // QUIC stream IDs have bit 1 set to 0 for bidirectional streams
        bool is_bidir = (stream_id & 2) == 0;
        if (!cbNotifyQueue_.Push(
              [=, this]() { delegate_.OnRecvStream(conn_ctx->conn_id, stream_id, std::nullopt, is_bidir); })) {
            SPDLOG_LOGGER_ERROR(
              logger, "conn_id: {0} stream_id: {1} notify queue is full", conn_ctx->conn_id, stream_id);
        }
    }
} catch (const std::exception& e) {
    SPDLOG_LOGGER_ERROR(logger, "Caught exception in OnRecvStreamBytes. (error={})", e.what());
    // TODO(tievens): Add metrics to track if this happens
}

void
PicoQuicTransport::OnStreamClosed(TransportConnId conn_id,
                                  uint64_t stream_id,
                                  std::shared_ptr<StreamRxContext> rx_ctx,
                                  StreamClosedFlag flag)
{
    SPDLOG_DEBUG("Stream {} closed for connection {}", stream_id, conn_id);
    cbNotifyQueue_.Push([=, rx_ctx = std::move(rx_ctx), this]() {
        delegate_.OnStreamClosed(conn_id, stream_id, std::move(rx_ctx), flag);
    });
}

void
PicoQuicTransport::EmitMetrics()
{
    for (auto& [conn_id, conn_ctx] : conn_context_) {
        const auto sample_time = std::chrono::system_clock::now();

        delegate_.OnConnectionMetricsSampled(sample_time, conn_id, conn_ctx.metrics);

        for (auto& [data_ctx_id, data_ctx] : conn_ctx.active_data_contexts) {
            delegate_.OnDataMetricsStampled(sample_time, conn_id, data_ctx_id, data_ctx.metrics);
            data_ctx.metrics.ResetPeriod();
        }

        conn_ctx.metrics.ResetPeriod();
    }
}

void
PicoQuicTransport::RemoveClosedStreams()
{
    std::lock_guard<std::mutex> _(state_mutex_);

    for (auto& [conn_id, conn_ctx] : conn_context_) {
        std::vector<uint64_t> closed_streams;

        for (auto& [stream_id, rx_buf] : conn_ctx.rx_stream_buffer) {
            if (rx_buf.closed && (rx_buf.rx_ctx->data_queue.Empty() || rx_buf.checked_once)) {
                closed_streams.push_back(stream_id);
            }
            rx_buf.checked_once = true;
        }

        for (const auto stream_id : closed_streams) {
            conn_ctx.rx_stream_buffer.erase(stream_id);
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

    for (auto& [conn_id, conn_ctx] : conn_context_) {
        int congested_count{ 0 };
        uint16_t cwin_congested_count = conn_ctx.metrics.cwin_congested - conn_ctx.metrics.prev_cwin_congested;

        picoquic_path_quality_t path_quality;
        picoquic_get_path_quality(conn_ctx.pq_cnx, conn_ctx.pq_cnx->path[0]->unique_path_id, &path_quality);

        /*
         * Update metrics
         */
        conn_ctx.metrics.tx_lost_pkts = path_quality.lost;
        conn_ctx.metrics.tx_cwin_bytes.AddValue(path_quality.cwin);
        conn_ctx.metrics.tx_in_transit_bytes.AddValue(path_quality.bytes_in_transit);
        conn_ctx.metrics.tx_spurious_losses = path_quality.spurious_losses;
        conn_ctx.metrics.tx_timer_losses = path_quality.timer_losses;
        conn_ctx.metrics.rtt_us.AddValue(path_quality.rtt_sample);
        conn_ctx.metrics.srtt_us.AddValue(path_quality.rtt);
        conn_ctx.metrics.tx_rate_bps.AddValue(path_quality.pacing_rate * 8);
        conn_ctx.metrics.rx_rate_bps.AddValue(path_quality.receive_rate_estimate * 8);

        // Is CWIN congested?
        if (cwin_congested_count > 5 || (path_quality.cwin < kPqCcLowCwin && path_quality.bytes_in_transit)) {

            // congested_count++; /* TODO(tievens): DO NOT react to this right now, causing issue with low latency
            // wired networks */
        }
        conn_ctx.metrics.prev_cwin_congested = conn_ctx.metrics.cwin_congested;

        // All other data flows (streams)
        uint64_t reset_wait_data_ctx_id{ 0 }; // Positive value indicates the data_ctx_id that can be set to reset_wait

        for (auto& [data_ctx_id, data_ctx] : conn_ctx.active_data_contexts) {

            // Skip context that is in reset and wait
            if (data_ctx.tx_reset_wait_discard) {
                continue;
            }

            // Don't include control stream in delayed callbacks check. Control stream should be priority 0 or 1
            if (data_ctx.priority >= 2 &&
                data_ctx.metrics.tx_delayed_callback - data_ctx.metrics.prev_tx_delayed_callback > 1) {
                SPDLOG_LOGGER_DEBUG(logger,
                                    "CC: remote: {0} port: {1} conn_id: {2} queue_size: {3}",
                                    conn_ctx.peer_addr_text,
                                    conn_ctx.peer_port,
                                    conn_id,
                                    data_ctx.metrics.tx_delayed_callback - data_ctx.metrics.prev_tx_delayed_callback);

                congested_count++;
            }
            data_ctx.metrics.prev_tx_delayed_callback = data_ctx.metrics.tx_delayed_callback;

            data_ctx.metrics.tx_queue_size.AddValue(data_ctx.tx_data->Size());

            // TODO(tievens): size of TX is based on rate; adjust based on burst rates
            if (data_ctx.tx_data->Size() >= 50) {
                congested_count++;
                SPDLOG_LOGGER_DEBUG(logger,
                                    "CC: remote: {0} port: {1} conn_id: {2} queue_size: {3}",
                                    conn_ctx.peer_addr_text,
                                    conn_ctx.peer_port,
                                    conn_id,
                                    data_ctx.tx_data->Size());
            }

            if (data_ctx.priority >= kPqRestWaitMinPriority && data_ctx.uses_reset_wait &&
                reset_wait_data_ctx_id == 0 && !data_ctx.tx_reset_wait_discard) {

                reset_wait_data_ctx_id = data_ctx_id;
            }
        }

        if (cwin_congested_count && conn_ctx.pq_cnx->nb_retransmission_total - conn_ctx.metrics.tx_retransmits > 2) {
            SPDLOG_LOGGER_DEBUG(logger,
                                "CC: remote: {0} port: {1} conn_id: {2} retransmits increased, delta: {3} total: {4}",
                                conn_ctx.peer_addr_text,
                                conn_ctx.peer_port,
                                conn_id,
                                (conn_ctx.pq_cnx->nb_retransmission_total - conn_ctx.metrics.tx_retransmits),
                                conn_ctx.pq_cnx->nb_retransmission_total);

            conn_ctx.metrics.tx_retransmits = conn_ctx.pq_cnx->nb_retransmission_total;
            congested_count++;
        }

        // Act on congested
        if (congested_count) {
            conn_ctx.metrics.tx_congested++;

            conn_ctx.is_congested = true;
            SPDLOG_LOGGER_WARN(
              logger,
              "CC: conn_id: {0} has streams congested. congested_count: {1} retrans: {2} cwin_congested: {3}",
              conn_id,
              congested_count,
              conn_ctx.metrics.tx_retransmits,
              conn_ctx.metrics.cwin_congested);

            if (tconfig_.use_reset_wait_strategy && reset_wait_data_ctx_id > 0) {
                auto& data_ctx = conn_ctx.active_data_contexts[reset_wait_data_ctx_id];
                SPDLOG_LOGGER_INFO(logger,
                                   "CC: conn_id: {0} setting reset and wait to data_ctx_id: {1} priority: {2}",
                                   conn_id,
                                   reset_wait_data_ctx_id,
                                   static_cast<int>(data_ctx.priority));

                data_ctx.tx_reset_wait_discard = true;
                data_ctx.metrics.tx_reset_wait++;

                /*
                 * TODO(tievens) Submit an issue with picoquic to add an API to flush the stream of any
                 *      data stuck in retransmission or waiting for acks
                 */
                // close_stream(conn_ctx, &data_ctx, true);
            }

        } else if (conn_ctx.is_congested) {

            if (conn_ctx.not_congested_gauge > 8) {
                // No longer congested
                conn_ctx.is_congested = false;
                conn_ctx.not_congested_gauge = 0;
                SPDLOG_LOGGER_INFO(
                  logger, "CC: conn_id: {0} congested_count: {1} is no longer congested.", conn_id, congested_count);
            } else {
                conn_ctx.not_congested_gauge++;
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

TransportConnId
PicoQuicTransport::StartClient()
{
    std::condition_variable cv;
    TransportConnId conn_id{ 0 };
    std::mutex mtx;
    std::unique_lock lock(mtx);

    RunPqFunction([this, &conn_id, &cv, &mtx]() {
        auto notify_caller = [&cv, &conn_id, &mtx](uint64_t id) {
            std::lock_guard _(mtx);
            conn_id = id;

            // Notify calling thread of connection Id
            cv.notify_all();
        };

        sockaddr_storage server_address;
        char const* sni = "cisco.webex.com";
        int ret;
        int is_name = 0;
        ret = picoquic_get_server_address(serverInfo_.host_or_ip.c_str(), serverInfo_.port, &server_address, &is_name);
        if (ret != 0 || server_address.ss_family == 0) {
            SPDLOG_LOGGER_ERROR(
              logger, "Failed to resolve server: {0} port: {1}", serverInfo_.host_or_ip, serverInfo_.port);
            notify_caller(1);
            return 0;
        }

        if (is_name) {
            sni = serverInfo_.host_or_ip.c_str();
        }

        picoquic_cnx_t* cnx = NULL;
        if (transport_mode == TransportMode::kQuic) {
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
            CreateConnContext(cnx);

        } else if (transport_mode == TransportMode::kWebTransport) {
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
            picoquic_set_feedback_loss_notification(cnx, 1);
            picoquic_enable_keep_alive(cnx, tconfig_.idle_timeout_ms * 500);

            // Create connection context and store per-connection WebTransport context first
            auto& conn_ctx = CreateConnContext(cnx);
            conn_ctx.wt_h3_ctx = h3_ctx;
            conn_ctx.wt_control_stream_ctx = control_stream_ctx;
            conn_ctx.wt_h3_ctx_owned = true; // Client owns this and must free it
            conn_ctx.wt_authority = serverInfo_.host_or_ip + ":" + std::to_string(serverInfo_.port);

            SPDLOG_LOGGER_INFO(logger,
                               "StartClient:Webtransport Connect: Control Stream ID: {}, "
                               "authority: {}, path: {}",
                               control_stream_ctx->stream_id,
                               conn_ctx.wt_authority,
                               wt_config_->path);

            // Initiate the WebTransport connect
            ret = picowt_connect(cnx,
                                 h3_ctx,
                                 control_stream_ctx,
                                 conn_ctx.wt_authority.c_str(),
                                 wt_config_->path.c_str(),
                                 DefaultWebTransportCallback,
                                 this);
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
                               conn_ctx.wt_authority,
                               wt_config_->path);
        }

        if (tconfig_.quic_priority_limit > 0) {
            SPDLOG_LOGGER_INFO(
              logger, "Setting priority bypass limit to {0}", static_cast<int>(tconfig_.quic_priority_limit));
            picoquic_set_priority_limit_for_bypass(cnx, tconfig_.quic_priority_limit);
        } else {
            SPDLOG_LOGGER_INFO(logger, "No priority bypass");
        }

        notify_caller(reinterpret_cast<uint64_t>(cnx));

        return 0;
    });

    SPDLOG_LOGGER_DEBUG(logger, "Waiting for client connection context");

    cv.wait_for(lock, std::chrono::milliseconds(3000), [&]() { return conn_id > 0; });

    SPDLOG_LOGGER_DEBUG(logger, "Got client connection context conn_id: {}", conn_id);
    if (conn_id <= 1) {
        SPDLOG_LOGGER_DEBUG(logger, "Client connection to {}:{} failed", serverInfo_.host_or_ip, serverInfo_.port);
        SetStatus(TransportStatus::kDisconnected);
        return 0;
    }

    return conn_id;
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
        picoquic_wake_up_network_thread(quic_network_thread_ctx_);

        while (quic_network_thread_ctx_->thread_is_ready) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        picoquic_delete_network_thread(quic_network_thread_ctx_);
    }

    picoquic_runner_queue_.StopWaiting();
    cbNotifyQueue_.StopWaiting();

    if (cbNotifyThread_.joinable()) {
        SPDLOG_LOGGER_INFO(logger, "Closing transport callback notifier thread");
        cbNotifyThread_.join();
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

    const auto current_tick = tick_service_->Milliseconds();

    if (data_ctx->last_tx_tick == 0) {
        data_ctx->last_tx_tick = current_tick;
        return;
    }

    const auto delta_ms = current_tick - data_ctx->last_tx_tick;
    data_ctx->last_tx_tick = current_tick;

    data_ctx->metrics.tx_callback_ms.AddValue(delta_ms);

    if (data_ctx->priority > 0 && delta_ms > 50 && data_ctx->tx_data->Size() >= 20) {
        data_ctx->metrics.tx_delayed_callback++;

        picoquic_path_quality_t path_quality;

        if (const auto conn_it = GetConnContext(data_ctx->conn_id)) {
            picoquic_get_path_quality(conn_it->pq_cnx, conn_it->pq_cnx->path[0]->unique_path_id, &path_quality);
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

void
PicoQuicTransport::CreateStream(ConnectionContext& conn_ctx, DataContext* data_ctx)
{

    if (data_ctx->current_stream_id) {
        CloseStream(conn_ctx, data_ctx, false);
    }

    data_ctx->mark_stream_active = true;

    // Handle WebTransport and raw QUIC differently
    if (conn_ctx.transport_mode == TransportMode::kWebTransport) {
        // For WebTransport, create stream using picowt_create_local_stream
        // Use per-connection WebTransport context instead of global wt_context_
        if (!conn_ctx.wt_h3_ctx || !conn_ctx.wt_control_stream_ctx) {
            SPDLOG_LOGGER_ERROR(
              logger, "WebTransport context not initialized for connection {} stream creation", conn_ctx.conn_id);
            return;
        }

        h3zero_stream_ctx_t* stream_ctx = picowt_create_local_stream(
          conn_ctx.pq_cnx, data_ctx->is_bidir ? 1 : 0, conn_ctx.wt_h3_ctx, conn_ctx.wt_control_stream_ctx->stream_id);

        if (!stream_ctx) {
            SPDLOG_LOGGER_ERROR(logger, "Failed to create WebTransport stream");
            return;
        }

        data_ctx->current_stream_id = stream_ctx->stream_id;
        data_ctx->wt_stream_ctx = stream_ctx;
        conn_ctx.last_stream_id = stream_ctx->stream_id;
        conn_ctx.wt_stream_to_data_ctx[stream_ctx->stream_id] = data_ctx->data_ctx_id;

        // Set callback and context for the stream
        stream_ctx->path_callback = DefaultWebTransportCallback;
        stream_ctx->path_callback_ctx = this;
    } else {
        // For raw QUIC, use the traditional approach
        conn_ctx.last_stream_id = picoquic_get_next_local_stream_id(conn_ctx.pq_cnx, !data_ctx->is_bidir);
        data_ctx->current_stream_id = conn_ctx.last_stream_id;

        SPDLOG_LOGGER_INFO(logger,
                           "conn_id: {0} data_ctx_id: {1} create new stream with stream_id: {2}",
                           conn_ctx.conn_id,
                           data_ctx->data_ctx_id,
                           conn_ctx.last_stream_id);

        /*
         * Must call set_app_stream_ctx so that the stream will be created now and the next call to create
         *      stream will use a new stream ID. Marking the stream active and setting priority involves
         *      more state changes in picoquic which causes issues when both the picoquic thread and caller
         *      thread udpate state.
         */
        picoquic_set_app_stream_ctx(conn_ctx.pq_cnx, *data_ctx->current_stream_id, data_ctx);
    }

    RunPqFunction([this, conn_id = conn_ctx.conn_id, data_ctx_id = data_ctx->data_ctx_id]() {
        MarkStreamActive(conn_id, data_ctx_id);
        return 0;
    });
}

void
PicoQuicTransport::CloseStream(ConnectionContext& conn_ctx, DataContext* data_ctx, const bool send_reset)
{
    if (!data_ctx->current_stream_id.has_value()) {
        return; // stream already closed
    }

    SPDLOG_LOGGER_TRACE(logger,
                        "conn_id: {0} data_ctx_id: {1} closing stream stream_id: {2}",
                        conn_ctx.conn_id,
                        data_ctx->data_ctx_id,
                        *data_ctx->current_stream_id);

    if (send_reset) {
        SPDLOG_LOGGER_TRACE(
          logger, "Reset stream_id: {0} conn_id: {1}", *data_ctx->current_stream_id, conn_ctx.conn_id);

        picoquic_reset_stream_ctx(conn_ctx.pq_cnx, *data_ctx->current_stream_id);
        picoquic_reset_stream(conn_ctx.pq_cnx, *data_ctx->current_stream_id, 0);

    } else {
        SPDLOG_LOGGER_TRACE(
          logger, "Sending FIN for stream_id: {0} conn_id: {1}", *data_ctx->current_stream_id, conn_ctx.conn_id);

        picoquic_reset_stream_ctx(conn_ctx.pq_cnx, *data_ctx->current_stream_id);
        uint8_t empty{ 0 };
        picoquic_add_to_stream(conn_ctx.pq_cnx, *data_ctx->current_stream_id, &empty, 0, 1);
    }

    if (data_ctx->current_stream_id) {
        const auto rx_buf_it = conn_ctx.rx_stream_buffer.find(*data_ctx->current_stream_id);
        if (rx_buf_it != conn_ctx.rx_stream_buffer.end()) {
            std::lock_guard<std::mutex> _(state_mutex_);
            conn_ctx.rx_stream_buffer.erase(rx_buf_it);
        }
    }

    // For WebTransport mode, properly cleanup HTTP/3 stream context
    if (conn_ctx.transport_mode == TransportMode::kWebTransport && data_ctx->current_stream_id) {
        conn_ctx.wt_stream_to_data_ctx.erase(*data_ctx->current_stream_id);

        // Properly delete the h3zero_stream_ctx_t to avoid memory leaks
        if (data_ctx->wt_stream_ctx && conn_ctx.wt_h3_ctx) {
            h3zero_delete_stream(conn_ctx.pq_cnx, conn_ctx.wt_h3_ctx, data_ctx->wt_stream_ctx);
        }

        data_ctx->wt_stream_ctx = nullptr;
    }

    data_ctx->ResetTxObject();
    data_ctx->current_stream_id = std::nullopt;
}

void
PicoQuicTransport::RunPqFunction(std::function<int()>&& function)
{
    picoquic_runner_queue_.Push(std::move(function));
    picoquic_wake_up_network_thread(quic_network_thread_ctx_);
}

void
PicoQuicTransport::MarkStreamActive(const TransportConnId conn_id, const DataContextId data_ctx_id)
{
    std::lock_guard _(state_mutex_);

    const auto conn_it = conn_context_.find(conn_id);
    if (conn_it == conn_context_.end()) {
        return;
    }

    const auto data_ctx_it = conn_it->second.active_data_contexts.find(data_ctx_id);
    if (data_ctx_it == conn_it->second.active_data_contexts.end()) {
        return;
    }

    data_ctx_it->second.mark_stream_active = false;

    if (!data_ctx_it->second.current_stream_id.has_value()) {
        return;
    }

    // For WebTransport and raw QUIC, pass the correct stream context
    void* stream_ctx = nullptr;
    if (conn_it->second.transport_mode == TransportMode::kWebTransport) {
        stream_ctx = data_ctx_it->second.wt_stream_ctx;
    } else {
        // For raw QUIC, pass the DataContext pointer
        stream_ctx = &data_ctx_it->second;
    }

    picoquic_mark_active_stream(conn_it->second.pq_cnx, *data_ctx_it->second.current_stream_id, 1, stream_ctx);
    picoquic_set_stream_priority(
      conn_it->second.pq_cnx, *data_ctx_it->second.current_stream_id, (data_ctx_it->second.priority << 1));
}

void
PicoQuicTransport::MarkDgramReady(const TransportConnId conn_id)
{
    std::lock_guard<std::mutex> _(state_mutex_);

    const auto conn_it = conn_context_.find(conn_id);
    if (conn_it == conn_context_.end()) {
        return;
    }

    auto& conn_ctx = conn_it->second;

    if (conn_ctx.transport_mode == TransportMode::kWebTransport && conn_ctx.wt_control_stream_ctx) {
        // WebTransport requires using h3zero_set_datagram_ready to set the ready_to_send_datagrams
        // flag on the stream prefix, which triggers the picohttp_callback_provide_datagram callback
        h3zero_set_datagram_ready(conn_ctx.pq_cnx, conn_ctx.wt_control_stream_ctx->stream_id);
    } else {
        // Raw QUIC mode uses picoquic_mark_datagram_ready directly
        picoquic_mark_datagram_ready(conn_ctx.pq_cnx, 1);
    }

    conn_ctx.mark_dgram_ready = false;
}

const char*
PicoQuicTransport::GetAlpn() const
{
    switch (transport_mode) {
        case TransportMode::kWebTransport:
            return webtransport_alpn;
        case TransportMode::kQuic:
        default:
            return quicr_alpn;
    }
}

int
PicoQuicTransport::InitializeWebTransportContext()
{
    // For clients: only initialize if transport_mode is kWebTransport
    // For servers: always initialize to support both QUIC and WebTransport connections
    if (!is_server_mode && transport_mode != TransportMode::kWebTransport) {
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
    auto conn_id = reinterpret_cast<TransportConnId>(cnx);

    // For client connections, use proper WebTransport setup flow
    if (!is_server_mode) {
        // Get or create connection context
        auto conn_ctx = GetConnContext(conn_id);
        if (!conn_ctx) {
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
        conn_ctx->wt_h3_ctx = h3_ctx;
        conn_ctx->wt_control_stream_ctx = control_stream_ctx;
        conn_ctx->wt_h3_ctx_owned = true; // Client owns this and must free it
        conn_ctx->wt_authority = serverInfo_.host_or_ip + ":" + std::to_string(serverInfo_.port);

        // Initiate the WebTransport connect
        ret = picowt_connect(cnx,
                             conn_ctx->wt_h3_ctx,
                             conn_ctx->wt_control_stream_ctx,
                             conn_ctx->wt_authority.c_str(),
                             wt_config_->path.c_str(),
                             DefaultWebTransportCallback,
                             this);
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
    auto conn_id = reinterpret_cast<TransportConnId>(cnx);

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

    auto& conn_ctx = CreateConnContext(cnx);

    // Store the WebTransport control stream context for this connection
    // The stream_ctx parameter is the control stream for this WebTransport connection
    if (stream_ctx) {
        conn_ctx.wt_control_stream_ctx = stream_ctx;
        // Set the control stream ID in the stream context
        stream_ctx->ps.stream_state.control_stream_id = stream_ctx->stream_id;
        h3zero_callback_ctx_t* h3_ctx = (h3zero_callback_ctx_t*)picoquic_get_callback_context(cnx);

        // Store the h3_ctx in the connection context for per-connection WebTransport support
        conn_ctx.wt_h3_ctx = h3_ctx;

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

    // Notify application that a new connection is ready
    OnNewConnection(conn_id);

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
    if (transport_mode != TransportMode::kWebTransport) {
        SPDLOG_LOGGER_ERROR(logger, "CreateWebTransportStream called but not in WebTransport mode");
        return nullptr;
    }

    // Get per-connection WebTransport context
    auto conn_id = reinterpret_cast<TransportConnId>(cnx);
    auto* conn_ctx = GetConnContext(conn_id);
    if (!conn_ctx) {
        SPDLOG_LOGGER_ERROR(logger, "CreateWebTransportStream: Connection context not found for conn_id {}", conn_id);
        return nullptr;
    }

    if (!conn_ctx->wt_h3_ctx) {
        SPDLOG_LOGGER_ERROR(
          logger, "CreateWebTransportStream: WebTransport h3_ctx not initialized for conn_id {}", conn_id);
        return nullptr;
    }

    if (!conn_ctx->wt_control_stream_ctx) {
        SPDLOG_LOGGER_ERROR(logger, "CreateWebTransportStream: No control stream context for conn_id {}", conn_id);
        return nullptr;
    }

    // Use picowt_create_local_stream (pico_webtransport.h:94-95)
    h3zero_stream_ctx_t* stream_ctx = picowt_create_local_stream(
      cnx, is_bidir ? 1 : 0, conn_ctx->wt_h3_ctx, conn_ctx->wt_control_stream_ctx->stream_id);

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
    if (transport_mode != TransportMode::kWebTransport) {
        SPDLOG_LOGGER_ERROR(logger, "SendWebTransportCloseSession called but not in WebTransport mode");
        return -1;
    }

    // Get per-connection WebTransport context
    auto conn_id = reinterpret_cast<TransportConnId>(cnx);
    auto* conn_ctx = GetConnContext(conn_id);
    if (!conn_ctx) {
        SPDLOG_LOGGER_ERROR(
          logger, "SendWebTransportCloseSession: Connection context not found for conn_id {}", conn_id);
        return -1;
    }

    if (!conn_ctx->wt_control_stream_ctx) {
        SPDLOG_LOGGER_ERROR(logger, "SendWebTransportCloseSession: No control stream context for conn_id {}", conn_id);
        return -1;
    }

    // Use picowt_send_close_session_message (pico_webtransport.h:69)
    int ret = picowt_send_close_session_message(cnx, conn_ctx->wt_control_stream_ctx, error_code, error_msg);

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
    if (transport_mode != TransportMode::kWebTransport) {
        SPDLOG_LOGGER_ERROR(logger, "SendWebTransportDrainSession called but not in WebTransport mode");
        return -1;
    }

    // Get per-connection WebTransport context
    auto conn_id = reinterpret_cast<TransportConnId>(cnx);
    auto* conn_ctx = GetConnContext(conn_id);
    if (!conn_ctx) {
        SPDLOG_LOGGER_ERROR(
          logger, "SendWebTransportDrainSession: Connection context not found for conn_id {}", conn_id);
        return -1;
    }

    if (!conn_ctx->wt_control_stream_ctx) {
        SPDLOG_LOGGER_ERROR(logger, "SendWebTransportDrainSession: No control stream context for conn_id {}", conn_id);
        return -1;
    }

    // Use picowt_send_drain_session_message (pico_webtransport.h:72-73)
    int ret = picowt_send_drain_session_message(cnx, conn_ctx->wt_control_stream_ctx);

    if (ret == 0) {
        SPDLOG_LOGGER_INFO(logger, "WebTransport drain session sent");
    } else {
        SPDLOG_LOGGER_ERROR(logger, "Failed to send WebTransport drain session: ret={}", ret);
    }

    return ret;
}

// Public API implementation for CloseWebTransportSession
int
PicoQuicTransport::CloseWebTransportSession(TransportConnId conn_id, uint32_t error_code, const char* error_msg)
{
    picoquic_cnx_t* cnx = reinterpret_cast<picoquic_cnx_t*>(conn_id);
    return SendWebTransportCloseSession(cnx, error_code, error_msg);
}

// Public API implementation for DrainWebTransportSession
int
PicoQuicTransport::DrainWebTransportSession(TransportConnId conn_id)
{
    picoquic_cnx_t* cnx = reinterpret_cast<picoquic_cnx_t*>(conn_id);
    return SendWebTransportDrainSession(cnx);
}

void
PicoQuicTransport::DeregisterWebTransport(picoquic_cnx_t* cnx)
{
    if (transport_mode != TransportMode::kWebTransport) {
        SPDLOG_LOGGER_WARN(logger, "DeregisterWebTransport called but not in WebTransport mode");
        return;
    }

    // Get per-connection WebTransport context
    auto conn_id = reinterpret_cast<TransportConnId>(cnx);
    auto* conn_ctx = GetConnContext(conn_id);
    if (!conn_ctx) {
        SPDLOG_LOGGER_WARN(logger, "DeregisterWebTransport: Connection context not found for conn_id {}", conn_id);
        return;
    }

    if (!conn_ctx->wt_h3_ctx || !conn_ctx->wt_control_stream_ctx) {
        SPDLOG_LOGGER_WARN(logger, "DeregisterWebTransport: WebTransport context already null for conn_id {}", conn_id);
        return;
    }

    // Use picowt_deregister to clean up all streams associated with this control stream
    picowt_deregister(cnx, conn_ctx->wt_h3_ctx, conn_ctx->wt_control_stream_ctx);

    // Release any accumulated capsule memory
    picowt_release_capsule(&conn_ctx->wt_capsule);

    SPDLOG_LOGGER_INFO(logger, "WebTransport context deregistered for conn_id {}", conn_id);

    // Clear the per-connection context
    conn_ctx->wt_control_stream_ctx = nullptr;

    // Clear WebTransport stream mappings for this session
    conn_ctx->wt_stream_to_data_ctx.clear();
}
