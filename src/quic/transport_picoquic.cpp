// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "transport_picoquic.h"

// PicoQuic.
#include <picoquic.h>
#include <picoquic_internal.h>
#include <picoquic_packet_loop.h>
#include <autoqlog.h>
#include <picoquic_utils.h>
#include <spdlog/spdlog.h>
#include <picoquic_config.h>
#include <picosocks.h>

// Transport.
#include <moq/detail/quic_transport_metrics.h>
#include <moq/detail/safe_queue.h>
#include <moq/detail/stream_buffer.h>
#include <moq/detail/priority_queue.h>
#include <moq/detail/span.h>
#include <moq/detail/time_queue.h>
#include <spdlog/logger.h>

// System.
#include <arpa/inet.h>
#include <sys/socket.h>
#include <cassert>
#include <cstring>
#include <ctime>
#include <iostream>
#include <thread>
#include <sstream>
#include <cstdint>
#include <optional>
#include <utility>
#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <chrono>
#include <sstream>
#include <stdexcept>

#if defined(__linux__)
#include <net/ethernet.h>
#include <netpacket/packet.h>
#endif

#define LOGGER_TRACE(logger, ...) if (logger) SPDLOG_LOGGER_TRACE(logger, __VA_ARGS__)
#define LOGGER_DEBUG(logger, ...) if (logger) SPDLOG_LOGGER_DEBUG(logger, __VA_ARGS__)
#define LOGGER_INFO(logger, ...) if (logger) SPDLOG_LOGGER_INFO(logger, __VA_ARGS__)
#define LOGGER_WARN(logger, ...) if (logger) SPDLOG_LOGGER_WARN(logger, __VA_ARGS__)
#define LOGGER_ERROR(logger, ...) if (logger) SPDLOG_LOGGER_ERROR(logger, __VA_ARGS__)
#define LOGGER_CRITICAL(logger, ...) if (logger) SPDLOG_LOGGER_CRITICAL(logger, __VA_ARGS__)

using namespace qtransport;

namespace {
    /**
     * @brief Returns the next stream id depending on if the stream is
     *        initiated by the client or the server, and if it bi- or uni- directional.
     *
     * @param id                    The initial value to adjust
     * @param is_server             Flag if the initiating request is from a server or a client
     * @param is_unidirectional     Flag if the streams are bi- or uni- directional.
     *
     * @return The correctly adjusted stream id value.
     */
    constexpr uint64_t GetNextStreamId(uint64_t last_stream_id, bool is_server, bool is_unidirectional)
    {
        return ((last_stream_id + 4) & (~0x0u << 2)) | (is_server ? 0b01 : 0b00) | (is_unidirectional ? 0b10 : 0b00);
    }

    /**
     * @brief Defines the default/datagram stream depending on if the stream is
     *        initiated by the client or the server, and if it bi- or uni- directional.
     *
     * @param is_server Flag if the initiating request is from a server or a client
     * @param is_unidirectional Flag if the streams are bi- or uni- directional.
     *
     * @return The datagram stream id.
     */
    constexpr DataContextId MakeDatagramStreamId(bool /*is_server*/, bool /*is_unidirectional*/)
    {
        return 0;
    }
} // namespace

/* ============================================================================
 * PicoQuic Callbacks
 * ============================================================================
 */

int PqEventCb(picoquic_cnx_t* pq_cnx,
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

                if (picoquic_get_cwin(pq_cnx) < kPqCcLowCwin) {        // Congested if less than 8K or near jumbo MTU size
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
                LOGGER_INFO(transport->logger, "conn_id: {0} stream_id: {1} context is null", conn_id, stream_id);
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
                    if ( ( (stream_id & 0x1) == 1 && !transport->is_server_mode)
                        || ( (stream_id & 0x0) == 0 && transport->is_server_mode)) {

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
                transport->OnRecvStreamBytes(conn_ctx, data_ctx, stream_id, bytes, length);

                if (is_fin) {
                    LOGGER_INFO(transport->logger, "Received FIN for stream {0}",stream_id);
                    picoquic_reset_stream_ctx(pq_cnx, stream_id);

                    if (auto conn_ctx = transport->GetConnContext(conn_id)) {
                        const auto rx_buf_it = conn_ctx->rx_stream_buffer.find(stream_id);
                        if (rx_buf_it != conn_ctx->rx_stream_buffer.end()) {
                            rx_buf_it->second.closed = true;
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
            LOGGER_DEBUG(transport->logger, "Received RESET stream conn_id: {0} stream_id: {1}", conn_id, stream_id);

            picoquic_reset_stream_ctx(pq_cnx, stream_id);

            if (auto conn_ctx = transport->GetConnContext(conn_id)) {
                const auto rx_buf_it = conn_ctx->rx_stream_buffer.find(stream_id);
                if (rx_buf_it != conn_ctx->rx_stream_buffer.end()) {
                    rx_buf_it->second.closed = true;
                }
            }

            if (data_ctx == NULL) {
                break;
            }

            data_ctx->current_stream_id = std::nullopt;

            LOGGER_DEBUG(transport->logger, "Received RESET stream; conn_id: {0} data_ctx_id: {1} stream_id: {2}", data_ctx->conn_id, data_ctx->data_ctx_id, stream_id);
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

            LOGGER_INFO(
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
            LOGGER_INFO(transport->logger, "Application closed conn_id: {0}", conn_id);
            [[fallthrough]];
        case picoquic_callback_close: {
            std::ostringstream log_msg;
            log_msg << "Closing connection conn_id: " << conn_id << " stream_id: " << stream_id;

            switch (picoquic_get_local_error(pq_cnx)) {
                case PICOQUIC_ERROR_IDLE_TIMEOUT:
                    log_msg << " Idle timeout";
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

            LOGGER_INFO(transport->logger, log_msg.str());

            transport->Close(conn_id);

            if (not transport->is_server_mode) {
                // TODO: Fix picoquic. Apparently picoquic is not processing return values for this callback
                return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
            }

            return 0;
        }

        case picoquic_callback_ready: { // Connection callback, not per stream
            if (transport->is_server_mode) {
                transport->CreateConnContext(pq_cnx);
                transport->OnNewConnection(conn_id);
            }
            else {
                // Client
                transport->SetStatus(TransportStatus::kReady);
                transport->OnConnectionStatus(conn_id, TransportStatus::kReady);
            }

            (void)picoquic_mark_datagram_ready(pq_cnx, 1);

            break;
        }

        default:
            LOGGER_DEBUG(transport->logger, "Got event {0}", fin_or_event);
            break;
    }

    return 0;
}

int PqLoopCb(picoquic_quic_t* quic, picoquic_packet_loop_cb_enum cb_mode, void* callback_ctx, void* callback_arg)
{
    PicoQuicTransport* transport = static_cast<PicoQuicTransport*>(callback_ctx);
    int ret = 0;

    if (transport == NULL) {
        std::cerr << "picoquic transport was called with NULL transport" << '\n';
        return PICOQUIC_ERROR_UNEXPECTED_ERROR;

    } else if (transport->Status() == TransportStatus::kDisconnected) {
        return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;

    } else {
        transport->PqRunner();

        switch (cb_mode) {
            case picoquic_packet_loop_ready: {
                LOGGER_INFO(transport->logger, "packet_loop_ready, waiting for packets");

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
                LOGGER_DEBUG(transport->logger, "packet_loop_port_update");
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

                if (targ->current_time - transport->pq_loop_prev_time > 100'000) {

                    transport->CheckConnsForCongestion();

                    transport->pq_loop_prev_time = targ->current_time;
                }

                // Stop loop if shutting down
                if (transport->Status() == TransportStatus::kShutdown) {
                    LOGGER_INFO(transport->logger, "picoquic is shutting down");

                    picoquic_cnx_t* close_cnx = picoquic_get_first_cnx(quic);

                    if (close_cnx == NULL) {
                        return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
                    }

                    while (close_cnx != NULL) {
                        LOGGER_INFO(transport->logger, "Closing connection id {0}", reinterpret_cast<uint64_t>(close_cnx));
                        picoquic_close(close_cnx, 0);
                        close_cnx = picoquic_get_next_cnx(close_cnx);
                    }

                    return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
                }

                break;
            }

            default:
                //ret = PICOQUIC_ERROR_UNEXPECTED_ERROR;
                LOGGER_WARN(transport->logger, "pq_loop_cb() does not implement ", std::to_string(cb_mode));
                break;
        }
    }

    return ret;
}

/* ============================================================================
 * Public API methods
 * ============================================================================
 */

TransportStatus PicoQuicTransport::Status() const
{
    return transportStatus_;
}

TransportConnId
PicoQuicTransport::Start(std::shared_ptr<SafeQueue<MetricsConnSample>> metrics_conn_samples,
                         std::shared_ptr<SafeQueue<MetricsDataSample>> metrics_data_samples)
{
    this->metrics_conn_samples = std::move(metrics_conn_samples);
    this->metrics_data_samples = std::move(metrics_data_samples);

    uint64_t current_time = picoquic_current_time();

    if (debug) {
        debug_set_callback(&PicoQuicTransport::PicoQuicLogging, this);
    }

    if (tconfig_.use_reset_wait_strategy) {
        LOGGER_INFO(logger, "Using Reset and Wait congestion control strategy");
    }

    if (tconfig_.use_bbr) {
        (void)picoquic_config_set_option(&config_, picoquic_option_CC_ALGO, "bbr");
    } else {
        LOGGER_INFO(logger, "Using NewReno congestion control");
        (void)picoquic_config_set_option(&config_, picoquic_option_CC_ALGO, "reno");
    }

    (void)picoquic_config_set_option(&config_, picoquic_option_ALPN, quicr_alpn);
    (void)picoquic_config_set_option(&config_, picoquic_option_CWIN_MIN,
                                     std::to_string(tconfig_.quic_cwin_minimum).c_str());
    (void)picoquic_config_set_option(&config_, picoquic_option_MAX_CONNECTIONS, "100");

    quic_ctx_ = picoquic_create_and_configure(&config_, PqEventCb, this, current_time, NULL);

    if (quic_ctx_ == NULL) {
        LOGGER_CRITICAL(logger, "Unable to create picoquic context, check certificate and key filenames");
        throw PicoQuicException("Unable to create picoquic context");
    }

    /*
     * TODO doc: Apparently need to set some value to send datagrams. If not set,
     *    max datagram size is zero, preventing sending of datagrams. Setting this
     *    also triggers PMTUD to run. This value will be the initial value.
     */
    picoquic_init_transport_parameters(&local_tp_options_, 1);
    local_tp_options_.max_datagram_frame_size = 1280;
    //  local_tp_options.max_packet_size = 1450;
    local_tp_options_.max_idle_timeout = tconfig_.idle_timeout_ms;
    local_tp_options_.max_ack_delay = 100000;
    local_tp_options_.min_ack_delay = 1000;

    picoquic_set_default_handshake_timeout(quic_ctx_, (tconfig_.idle_timeout_ms * 1000) / 2);
    picoquic_set_default_tp(quic_ctx_, &local_tp_options_);
    picoquic_set_default_idle_timeout(quic_ctx_, tconfig_.idle_timeout_ms);
    picoquic_set_default_priority(quic_ctx_, 2);
    picoquic_set_default_datagram_priority(quic_ctx_, 1);

    LOGGER_INFO(logger, "Setting idle timeout to {0}ms", tconfig_.idle_timeout_ms);
    //picoquic_set_default_wifi_shadow_rtt(quic_ctx, tconfig.quic_wifi_shadow_rtt_us);
    //logger->info << "Setting wifi shadow RTT to " << tconfig.quic_wifi_shadow_rtt_us << "us" << std::flush;

    picoquic_runner_queue_.SetLimit(2000);

    cbNotifyQueue_.SetLimit(2000);
    cbNotifyThread_ = std::thread(&PicoQuicTransport::CbNotifier, this);

    TransportConnId cid = 0;
    std::ostringstream log_msg;

    if (is_server_mode) {

        LOGGER_INFO(logger, "Starting server, listening on {0}:{1}", serverInfo_.host_or_ip, serverInfo_.port);

        picoQuicThread_ = std::thread(&PicoQuicTransport::Server, this);

    } else {
        LOGGER_INFO(logger, "Connecting to server {0}:{1}", serverInfo_.host_or_ip, serverInfo_.port);

        if ((cid = CreateClient())) {
            picoQuicThread_ = std::thread(&PicoQuicTransport::Client, this, cid);
        }
    }

    if (!tconfig_.quic_qlog_path.empty()) {
        LOGGER_INFO(logger, "Enabling qlog using '{0}' path", tconfig_.quic_qlog_path);
        picoquic_set_qlog(quic_ctx_, tconfig_.quic_qlog_path.c_str());
    }

    return cid;
}

bool PicoQuicTransport::GetPeerAddrInfo(const TransportConnId& conn_id,
                                   sockaddr_storage* addr)
{
    std::lock_guard<std::mutex> _(state_mutex_);

    // Locate the specified transport connection context
    auto it = conn_context_.find(conn_id);

    // If not found, return false
    if (it == conn_context_.end()) return false;

    // Copy the address
    std::memcpy(addr, &it->second.peer_addr, sizeof(sockaddr_storage));

    return true;
}

TransportError
PicoQuicTransport::Enqueue(const TransportConnId& conn_id,
                           const DataContextId& data_ctx_id,
                           std::vector<uint8_t>&& bytes,
                           std::vector<MethodTraceItem> &&trace,
                           const uint8_t priority,
                           const uint32_t ttl_ms,
                           [[maybe_unused]] const uint32_t delay_ms,
                           const EnqueueFlags flags)
{
    if (bytes.empty()) {
        LOGGER_ERROR(logger, "enqueue dropped due bytes empty, conn_id: {0} data_ctx_id: {1}", conn_id, data_ctx_id);
        return TransportError::kNone;
    }

    trace.push_back({"transport_quic:enqueue", trace.front().start_time});

    std::lock_guard<std::mutex> _(state_mutex_);

    trace.push_back({"transport_quic:enqueue:afterLock", trace.front().start_time});

    const auto conn_ctx_it = conn_context_.find(conn_id);
    if (conn_ctx_it == conn_context_.end()) {
        return TransportError::kInvalidConnContextId;
    }

    const auto data_ctx_it = conn_ctx_it->second.active_data_contexts.find(data_ctx_id);
    if (data_ctx_it == conn_ctx_it->second.active_data_contexts.end()) {
        return TransportError::kInvalidDataContextId;
    }

    data_ctx_it->second.metrics.enqueued_objs++;

    ConnData cd { conn_id,
                  data_ctx_id,
                  priority,
                  std::move(bytes),
                  std::move(trace)};

    if (flags.use_reliable) {
        if (flags.new_stream) {
            if (flags.use_reset) {
                data_ctx_it->second.stream_action = DataContext::StreamAction::kReplaceStreamUseReset;
            } else {
                data_ctx_it->second.stream_action = DataContext::StreamAction::kReplaceStreamUseFin;
            }
        }

        if (flags.clear_tx_queue) {
            data_ctx_it->second.metrics.tx_queue_discards += data_ctx_it->second.tx_data->Size();
            data_ctx_it->second.tx_data->Clear();
        }

        data_ctx_it->second.tx_data->Push(std::move(cd), ttl_ms, priority, 0);

        if (! data_ctx_it->second.mark_stream_active) {
            data_ctx_it->second.mark_stream_active = true;

            picoquic_runner_queue_.Push([this, conn_id, data_ctx_id]() {
                MarkStreamActive(conn_id, data_ctx_id);
            });
        }
    }

    else { // datagram
        conn_ctx_it->second.dgram_tx_data->Push(std::move(cd), ttl_ms, priority, 0);

        if (!conn_ctx_it->second.mark_dgram_ready ) {
            conn_ctx_it->second.mark_dgram_ready = true;

            picoquic_runner_queue_.Push([this, conn_id]() {
                MarkDgramReady(conn_id);
            });
        }
    }
    return TransportError::kNone;
}

std::shared_ptr<StreamBuffer<uint8_t>>
PicoQuicTransport::GetStreamBuffer(TransportConnId conn_id, uint64_t stream_id)
{
    std::lock_guard<std::mutex> _(state_mutex_);

    const auto conn_ctx_it = conn_context_.find(conn_id);
    if (conn_ctx_it == conn_context_.end()) {
        return nullptr;
    }

    const auto sbuf_it = conn_ctx_it->second.rx_stream_buffer.find(stream_id);
    if (sbuf_it != conn_ctx_it->second.rx_stream_buffer.end()) {
        return sbuf_it->second.buf;
    }
    return nullptr;
}

std::optional<std::vector<uint8_t>>
PicoQuicTransport::Dequeue(TransportConnId conn_id,
                           [[maybe_unused]] std::optional<DataContextId> data_ctx_id)
{
    std::lock_guard<std::mutex> _(state_mutex_);

    const auto conn_ctx_it = conn_context_.find(conn_id);
    if (conn_ctx_it == conn_context_.end()) {
        return std::nullopt;
    }

    return conn_ctx_it->second.dgram_rx_data.Pop();
}

DataContextId
PicoQuicTransport::CreateDataContext(const TransportConnId conn_id,
                                     bool use_reliable_transport,
                                     uint8_t priority, bool bidir)
{
    std::lock_guard<std::mutex> _(state_mutex_);

    if (priority > 127) {
        /*
         * Picoquic most significant bit of priority indicates to use round-robin. We don't want
         *      to use round-robin of same priorities right now.
         */
        throw std::runtime_error("Create stream priority cannot be greater than 127, range is 0 - 127");
    }

    const auto conn_it = conn_context_.find(conn_id);
    if (conn_it == conn_context_.end()) {
        LOGGER_ERROR(logger, "Invalid conn_id: {0}, cannot create data context", conn_id);
        return 0;
    }

    const auto [data_ctx_it, is_new] = conn_it->second.active_data_contexts.emplace(conn_it->second.next_data_ctx_id,
                                                                                    DataContext{});

    if (is_new) {
        // Init context
        data_ctx_it->second.conn_id = conn_id;
        data_ctx_it->second.is_bidir = bidir;
        data_ctx_it->second.data_ctx_id = conn_it->second.next_data_ctx_id++; // Set and bump next data_ctx_id

        data_ctx_it->second.priority = priority;

        data_ctx_it->second.tx_data = std::make_unique<PriorityQueue<ConnData>>(tconfig_.time_queue_max_duration,
                                                                                tconfig_.time_queue_bucket_interval,
                                                                                tick_service_,
                                                                                tconfig_.time_queue_init_queue_size);

        // Create stream
        if (use_reliable_transport) {
            CreateStream(conn_it->second, &data_ctx_it->second);
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
    for (const auto& [stream_id, rx_buf]: conn_it->second.rx_stream_buffer) {
        picoquic_mark_active_stream(conn_it->second.pq_cnx, stream_id, 0, NULL);
        picoquic_unlink_app_stream_ctx(conn_it->second.pq_cnx, stream_id);

        if (!rx_buf.closed) {
            picoquic_reset_stream(conn_it->second.pq_cnx, stream_id, 0);
        }
    }

    // Only one datagram context is per connection, if it's deleted, then the connection is to be terminated
    OnConnectionStatus(conn_id, TransportStatus::kDisconnected);

    picoquic_close(conn_it->second.pq_cnx, app_reason_code);

    if (not is_server_mode) {
        SetStatus(TransportStatus::kShutdown);
    }

    conn_context_.erase(conn_it);
}

void PicoQuicTransport::SetRemoteDataCtxId([[maybe_unused]] const TransportConnId conn_id,
                                           [[maybe_unused]] const DataContextId data_ctx_id,
                                           [[maybe_unused]] const DataContextId remote_data_ctx_id) {
    return;
}

void PicoQuicTransport::SetDataCtxPriority(const TransportConnId conn_id, DataContextId data_ctx_id, uint8_t priority)
{
    std::lock_guard<std::mutex> _(state_mutex_);

    const auto conn_it = conn_context_.find(conn_id);

    if (conn_it == conn_context_.end())
        return;

    const auto data_ctx_it = conn_it->second.active_data_contexts.find(data_ctx_id);
    if (data_ctx_it == conn_it->second.active_data_contexts.end())
        return;

    LOGGER_DEBUG(logger, "Set data context priority to {0}  conn_id: {1} data_ctx_id: {2}" << static_cast<int>(priority), conn_id, data_ctx_id);

    data_ctx_it->second.priority = priority;
}

void PicoQuicTransport::SetStreamIdDataCtxId(const TransportConnId conn_id,
                                             DataContextId data_ctx_id,
                                             uint64_t stream_id) {
    std::lock_guard<std::mutex> _(state_mutex_);

    const auto conn_it = conn_context_.find(conn_id);

    if (conn_it == conn_context_.end())
        return;

    const auto data_ctx_it = conn_it->second.active_data_contexts.find(data_ctx_id);
    if (data_ctx_it == conn_it->second.active_data_contexts.end())
        return;

    LOGGER_DEBUG(logger, "Set data context to stream conn_id: {0} data_ctx_id: {1} stream_id: {2}", conn_id, data_ctx_id, stream_id);

    data_ctx_it->second.current_stream_id = stream_id;

    picoquic_runner_queue_.Push([=]() {
        if (conn_it->second.pq_cnx != nullptr)
            picoquic_set_app_stream_ctx(conn_it->second.pq_cnx, stream_id, &data_ctx_it->second);
    });
}

  /* ============================================================================
 * Public internal methods used by picoquic
 * ============================================================================
 */

PicoQuicTransport::ConnectionContext* PicoQuicTransport::GetConnContext(const TransportConnId& conn_id)
{
    // Locate the specified transport connection context
    auto it = conn_context_.find(conn_id);

    // If not found, return empty context
    if (it == conn_context_.end()) return nullptr;

    return &it->second;
}


PicoQuicTransport::ConnectionContext& PicoQuicTransport::CreateConnContext(picoquic_cnx_t * pq_cnx)
{

    auto [conn_it, is_new] = conn_context_.emplace(reinterpret_cast<TransportConnId>(pq_cnx), pq_cnx);

    sockaddr* addr;

    auto &conn_ctx = conn_it->second;
    conn_ctx.conn_id = reinterpret_cast<TransportConnId>(pq_cnx);
    conn_ctx.pq_cnx = pq_cnx;

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
        LOGGER_INFO(logger, "Created new connection context for conn_id: {0}", conn_ctx.conn_id);

        conn_ctx.dgram_rx_data.SetLimit(tconfig_.time_queue_rx_size);
        conn_ctx.dgram_tx_data = std::make_unique<PriorityQueue<ConnData>>(tconfig_.time_queue_max_duration,
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
                                     std::shared_ptr<spdlog::logger> logger)
  : logger(std::move(logger))
  , is_server_mode(is_server_mode)
  , stop_(false)
  , transportStatus_(TransportStatus::kConnecting)
  , serverInfo_(server)
  , delegate_(delegate)
  , tconfig_(tcfg)
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
    tick_service_ = std::make_shared<ThreadedTickService>();
}

PicoQuicTransport::~PicoQuicTransport()
{
    SetStatus(TransportStatus::kShutdown);
    Shutdown();
}

void
PicoQuicTransport::SetStatus(TransportStatus status)
{
    transportStatus_ = status;
}

PicoQuicTransport::DataContext* PicoQuicTransport::CreateDataContextBiDirRecv(TransportConnId conn_id, uint64_t stream_id)
{
    std::lock_guard<std::mutex> _(state_mutex_);

    const auto conn_it = conn_context_.find(conn_id);
    if (conn_it == conn_context_.end()) {
        LOGGER_ERROR(logger, "Invalid conn_id: {0}, cannot create data context", conn_id);
        return nullptr;
    }

    const auto [data_ctx_it, is_new] = conn_it->second.active_data_contexts.emplace(conn_it->second.next_data_ctx_id,
                                                                                    DataContext{});

    if (is_new) {
        // Init context
        data_ctx_it->second.conn_id = conn_id;
        data_ctx_it->second.is_bidir = true;
        data_ctx_it->second.data_ctx_id = conn_it->second.next_data_ctx_id++; // Set and bump next data_ctx_id

        data_ctx_it->second.priority = 10; // TODO: Need to get priority from remote

        data_ctx_it->second.tx_data = std::make_unique<PriorityQueue<ConnData>>(tconfig_.time_queue_max_duration,
                                                                                tconfig_.time_queue_bucket_interval,
                                                                                tick_service_,
                                                                                tconfig_.time_queue_init_queue_size);

        data_ctx_it->second.current_stream_id = stream_id;

#if __cplusplus >= 202002L
        _cbNotifyQueue.push([=, data_ctx_id = data_ctx_it->second.data_ctx_id, this]() {
#else
        cbNotifyQueue_.Push([=, data_ctx_id = data_ctx_it->second.data_ctx_id]() {
#endif
            delegate_.OnNewDataContext(conn_id, data_ctx_id); });

        LOGGER_INFO(logger, "Created new bidir data context conn_id: {0} data_ctx_id: {1} stream_id: {2}", conn_id, data_ctx_it->second.data_ctx_id, stream_id);

        return &data_ctx_it->second;
    }

    return nullptr;
}

void PicoQuicTransport::PqRunner() {

    if (picoquic_runner_queue_.Empty()) {
        return;
    }

    // note: check before running move of optional, which is more CPU taxing when empty
    while (auto cb = picoquic_runner_queue_.Pop()) {
        (*cb)();
    }
}

void PicoQuicTransport::DeleteDataContextInternal(TransportConnId conn_id, DataContextId data_ctx_id)
{
    const auto conn_it = conn_context_.find(conn_id);

    if (conn_it == conn_context_.end())
        return;

    LOGGER_INFO(logger, "Delete data context {0} in conn_id: {1}", data_ctx_id, conn_id);

    const auto data_ctx_it = conn_it->second.active_data_contexts.find(data_ctx_id);
    if (data_ctx_it == conn_it->second.active_data_contexts.end())
        return;

    CloseStream(conn_it->second, &data_ctx_it->second, false);

    conn_it->second.active_data_contexts.erase(data_ctx_it);
}

void PicoQuicTransport::DeleteDataContext(const TransportConnId& conn_id, DataContextId data_ctx_id)
{
    if (data_ctx_id == 0) {
        return; // use close() instead of deleting default/datagram context
    }

    /*
     * Race conditions exist with picoquic thread callbacks that will cause a problem if the context (pointer context)
     *    is deleted outside of the picoquic thread. Below schedules the delete to be done within the picoquic thread.
     */
    picoquic_runner_queue_.Push([this, conn_id, data_ctx_id]() {
        DeleteDataContextInternal(conn_id, data_ctx_id);
    });

}

void
PicoQuicTransport::SendNextDatagram(ConnectionContext* conn_ctx, uint8_t* bytes_ctx, size_t max_len)
{
    if (bytes_ctx == nullptr) {
        return;
    }

    auto out_data = conn_ctx->dgram_tx_data->Front();
    if (out_data.has_value) {
        const auto data_ctx_it = conn_ctx->active_data_contexts.find(out_data.value.data_ctx_id);
        if (data_ctx_it == conn_ctx->active_data_contexts.end()) {
            LOGGER_DEBUG(logger, "send_next_dgram has no data context conn_id: {0} data len: {1} dropping", conn_ctx->conn_id, out_data.value.data.size());
            conn_ctx->metrics.tx_dgram_drops++;
            return;
        }

        CheckCallbackDelta(&data_ctx_it->second);

        if (out_data.value.data.size() == 0) {
            LOGGER_ERROR(logger, "conn_id: {0} data_ctx_id: {1} priority: {2} has ZERO data size", data_ctx_it->second.conn_id, data_ctx_it->second.data_ctx_id, static_cast<int>(data_ctx_it->second.priority));
            data_ctx_it->second.tx_data->Pop();
            return;
        }


        data_ctx_it->second.metrics.tx_queue_expired += out_data.expired_count;

        if (out_data.value.data.size() <= max_len) {
            conn_ctx->dgram_tx_data->Pop();

            out_data.value.trace.push_back({"transport_quic:send_dgram", out_data.value.trace.front().start_time});

            data_ctx_it->second.metrics.tx_object_duration_us.AddValue(out_data.value.trace.back().delta);

            if (out_data.value.trace.back().delta > 60000) {
                std::ostringstream log_msg;
                log_msg << "MethodTrace conn_id: " << conn_ctx->conn_id
                        << " data_ctx_id: " << data_ctx_it->second.data_ctx_id
                        << " priority: " << static_cast<int>(out_data.value.priority);
                for (const auto &ti: out_data.value.trace) {
                    log_msg << " " << ti.method << ": " << ti.delta << " ";
                }

                log_msg << " total_duration: " << out_data.value.trace.back().delta;
                LOGGER_INFO(logger, log_msg.str());
            }

            data_ctx_it->second.metrics.tx_dgrams_bytes += out_data.value.data.size();
            data_ctx_it->second.metrics.tx_dgrams++;

            uint8_t* buf = nullptr;

            buf = picoquic_provide_datagram_buffer_ex(bytes_ctx,
                                                      out_data.value.data.size(),
                                                      conn_ctx->dgram_tx_data->Empty() ? picoquic_datagram_not_active : picoquic_datagram_active_any_path);

            if (buf != nullptr) {
                std::memcpy(buf, out_data.value.data.data(), out_data.value.data.size());
            }
        }
        else {
            picoquic_runner_queue_.Push([this, conn_id = conn_ctx->conn_id]() {
                MarkDgramReady(conn_id);
            });

            /* TODO(tievens): picoquic_prepare_stream_and_datagrams() appears to ignore the
             *     below unless data was sent/provided
             */
            picoquic_provide_datagram_buffer_ex(bytes_ctx, 0, picoquic_datagram_active_any_path);
        }
    }
    else {
        picoquic_provide_datagram_buffer_ex(bytes_ctx, 0, picoquic_datagram_not_active);
    }
}

void
PicoQuicTransport::SendStreamBytes(DataContext* data_ctx, uint8_t* bytes_ctx, size_t max_len)
{
    if (bytes_ctx == NULL) {
        return;
    }

    uint32_t data_len = 0;                                  /// Length of data to follow the 4 byte length
    size_t offset = 0;
    int is_still_active = 0;

    CheckCallbackDelta(data_ctx);

    switch (data_ctx->stream_action) {
        case DataContext::StreamAction::kNoAction:
            [[fallthrough]];
        default:
            if (!data_ctx->current_stream_id.has_value()) {
                LOGGER_INFO(logger, "Creating unset stream in conn_id: {0}", data_ctx->conn_id);
                const auto conn_ctx = GetConnContext(data_ctx->conn_id);
                CreateStream(*conn_ctx, data_ctx);

                return;
            }
            break;

        case DataContext::StreamAction::kReplaceStreamUseReset: {
            data_ctx->uses_reset_wait = true;

            std::lock_guard<std::mutex> _(state_mutex_);
            const auto conn_ctx = GetConnContext(data_ctx->conn_id);

            // Keep stream in discard mode if still congested
            if (conn_ctx->is_congested && data_ctx->tx_reset_wait_discard) {
                break;
            }

            if (data_ctx->stream_tx_object != nullptr) {
                data_ctx->metrics.tx_buffer_drops++;
            }

            CloseStream(*conn_ctx, data_ctx, true);

            LOGGER_DEBUG(logger,
                                "Replacing stream using RESET; conn_id: {0} data_ctx_id: {1} existing_stream: {2} "
                                "write buf drops: {3} tx_queue_discards: {4}",
                                data_ctx->conn_id,
                                data_ctx->data_ctx_id,
                                *data_ctx->current_stream_id,
                                data_ctx->metrics.tx_buffer_drops,
                                data_ctx->metrics.tx_queue_discards);

            CreateStream(*conn_ctx, data_ctx);
            data_ctx->stream_action = DataContext::StreamAction::kNoAction;

            if (!conn_ctx->is_congested) { // Only clear reset wait if not congested
                data_ctx->tx_reset_wait_discard = false;                // Allow new object to be sent
            }

            data_ctx->mark_stream_active = false;
            return; // New stream requires PQ to callback again using that stream
        }

        case DataContext::StreamAction::kReplaceStreamUseFin: {
            data_ctx->uses_reset_wait = true;

            if (data_ctx->stream_tx_object != nullptr) {
                data_ctx->metrics.tx_buffer_drops++;
            }

            LOGGER_INFO(logger, "Replacing stream using FIN; conn_id: {0} existing_stream: {1}", data_ctx->conn_id, *data_ctx->current_stream_id);

            std::lock_guard<std::mutex> _(state_mutex_);

            const auto conn_ctx = GetConnContext(data_ctx->conn_id);
            CloseStream(*conn_ctx, data_ctx, false);
            CreateStream(*conn_ctx, data_ctx);

            data_ctx->stream_action = DataContext::StreamAction::kNoAction;

            data_ctx->mark_stream_active = false;
            return; // New stream requires PQ to callback again using that stream
        }
    }

    if (data_ctx->tx_reset_wait_discard) {      // Drop TX objects till next reset/new stream
        auto obj = data_ctx->tx_data->PopFront();
        if (obj.has_value) {
            data_ctx->metrics.tx_queue_discards++;

            picoquic_runner_queue_.Push([this, conn_id = data_ctx->conn_id, data_ctx_id = data_ctx->data_ctx_id]() {
                MarkStreamActive(conn_id, data_ctx_id);
            });
        }

        data_ctx->mark_stream_active = false;
        return;
    }

    if (data_ctx->stream_tx_object == nullptr) {
        auto obj = data_ctx->tx_data->PopFront();
        data_ctx->metrics.tx_queue_expired += obj.expired_count;

        if (obj.has_value) {
            if (obj.value.data.size() == 0) {
                LOGGER_ERROR(logger, "conn_id: {0} data_ctx_id: {1} priority: {2} stream has ZERO data size", data_ctx->conn_id, data_ctx->data_ctx_id, static_cast<int>(data_ctx->priority));
                return;
            }

            data_ctx->metrics.tx_stream_objects++;

            obj.value.trace.push_back({"transport_quic:send_stream", obj.value.trace.front().start_time});
            data_ctx->metrics.tx_object_duration_us.AddValue(obj.value.trace.back().delta);

            data_ctx->stream_tx_object = new uint8_t[obj.value.data.size()];
            data_ctx->stream_tx_object_size = obj.value.data.size();
            std::memcpy(data_ctx->stream_tx_object, obj.value.data.data(), obj.value.data.size());

            if (obj.value.data.size() > max_len) {
                data_ctx->stream_tx_object_offset = max_len;
                data_len = max_len;
                is_still_active = 1;

            } else {
                data_len = obj.value.data.size();
                data_ctx->stream_tx_object_offset = 0;
            }

        } else {
            // Queue is empty
            picoquic_provide_stream_data_buffer(bytes_ctx,
                                                0,
                                                0,
                                                is_still_active);

            return;
        }
    }
    else { // Have existing object with remaining bytes to send.
        data_len = data_ctx->stream_tx_object_size - data_ctx->stream_tx_object_offset;
        offset = data_ctx->stream_tx_object_offset;

        if (data_len > max_len) {
            data_ctx->stream_tx_object_offset += max_len;
            data_len = max_len;
            is_still_active = 1;

        } else {
            data_ctx->stream_tx_object_offset = 0;
        }
    }

    data_ctx->metrics.tx_stream_bytes += data_len;

    if (!is_still_active && !data_ctx->tx_data->Empty())
        is_still_active = 1;

    uint8_t *buf = nullptr;

    buf = picoquic_provide_stream_data_buffer(bytes_ctx,
                                              data_len,
                                              0,
                                              is_still_active);

    if (buf == NULL) {
        // Error allocating memory to write
        LOGGER_ERROR(logger, "conn_id: {0} data_ctx_id: {1} priority: {2} unable to allocate pq buffer size: {3}", data_ctx->conn_id,  data_ctx->data_ctx_id, static_cast<int>(data_ctx->priority), data_len);
        return;
    }

    // Write data
    std::memcpy(buf, data_ctx->stream_tx_object + offset, data_len);

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
        LOGGER_INFO(logger, "Connection established to server {0}", conn_ctx->peer_addr_text);
    }

#if __cplusplus >= 202002L
    _cbNotifyQueue.push([=, this]() { _delegate.on_connection_status(conn_id, status); });
#else
    cbNotifyQueue_.Push([=]() { delegate_.OnConnectionStatus(conn_id, status); });
#endif
}

void
PicoQuicTransport::OnNewConnection(const TransportConnId conn_id)
{
    auto conn_ctx = GetConnContext(conn_id);
    if (!conn_ctx) return;

    LOGGER_INFO(logger, "New Connection {0} port: {1} conn_id: {2}", conn_ctx->peer_addr_text, conn_ctx->peer_port, conn_id);

//    picoquic_subscribe_pacing_rate_updates(conn_ctx->pq_cnx, tconfig.pacing_decrease_threshold_Bps,
//                                           tconfig.pacing_increase_threshold_Bps);

    TransportRemote remote{ .host_or_ip = conn_ctx->peer_addr_text,
                            .port = conn_ctx->peer_port,
                            .proto = TransportProtocol::kQuic };

    picoquic_enable_keep_alive(conn_ctx->pq_cnx, tconfig_.idle_timeout_ms * 500);
    picoquic_set_feedback_loss_notification(conn_ctx->pq_cnx, 1);

    if (tconfig_.quic_priority_limit > 0) {
        LOGGER_INFO(logger, "Setting priority bypass limit to {0}", static_cast<int>(tconfig_.quic_priority_limit));
        picoquic_set_priority_limit_for_bypass(conn_ctx->pq_cnx, tconfig_.quic_priority_limit);
    }

#if __cplusplus >= 202002L
    _cbNotifyQueue.push([=, this]() { _delegate.on_new_connection(conn_id, remote); });
#else
    cbNotifyQueue_.Push([=]() { delegate_.OnNewConnection(conn_id, remote); });
#endif
}

void
PicoQuicTransport::OnRecvDatagram(ConnectionContext* conn_ctx, uint8_t* bytes, size_t length)
{
    if (length == 0) {
        return;
    }

    if (conn_ctx == nullptr) {
        LOGGER_WARN(logger, "DGRAM received with NULL connection context; dropping length: {0}", length);
        return;
    }

    std::vector<uint8_t> data(bytes, bytes + length);

    conn_ctx->dgram_rx_data.Push(data);
    conn_ctx->metrics.rx_dgrams++;
    conn_ctx->metrics.rx_dgrams_bytes += length;

    if (cbNotifyQueue_.Size() > 100) {
        LOGGER_INFO(logger, "on_recv_datagram cbNotifyQueue size {0}", cbNotifyQueue_.Size());
    }

#if __cplusplus >= 202002L
    if (conn_ctx->dgram_rx_data.size() < 10 && !_cbNotifyQueue.push([=, this]() {
#else
    if (conn_ctx->dgram_rx_data.Size() < 10 && !cbNotifyQueue_.Push([=]() {
#endif
            delegate_.OnRecvDgram(conn_ctx->conn_id,std::nullopt);
        })) {

        LOGGER_ERROR(logger, "conn_id: {0} DGRAM notify queue is full", conn_ctx->conn_id);
    }
}

void PicoQuicTransport::OnRecvStreamBytes(ConnectionContext* conn_ctx,
                                             DataContext* data_ctx,
                                             uint64_t stream_id,
                                             uint8_t* bytes,
                                             size_t length)
{
    if (length == 0) {
        LOGGER_DEBUG(logger, "on_recv_stream_bytes length is ZERO");
        return;
    }

    auto rx_buf_it = conn_ctx->rx_stream_buffer.find(stream_id);
    if (rx_buf_it == conn_ctx->rx_stream_buffer.end()) {
        std::lock_guard<std::mutex> l(state_mutex_);

        LOGGER_DEBUG(logger, "Adding received conn_id: {0} stream_id: {1} into RX buffer", conn_ctx->conn_id, stream_id);

        auto [it, _] = conn_ctx->rx_stream_buffer.try_emplace(stream_id);
        rx_buf_it = it;
    }

    Span<uint8_t> bytes_a(bytes, length);
    auto &rx_buf = rx_buf_it->second;

    rx_buf.buf->Push(bytes_a);

    if (data_ctx != nullptr) {
        data_ctx->metrics.rx_stream_cb++;
        data_ctx->metrics.rx_stream_bytes += length;

#if __cplusplus >= 202002L
        if (!_cbNotifyQueue.push([=, this]() {
#else
        if (!cbNotifyQueue_.Push([=]() {
#endif
                delegate_.OnRecvStream(conn_ctx->conn_id, stream_id, data_ctx->data_ctx_id, data_ctx->is_bidir);
            })) {

            LOGGER_ERROR(logger, "conn_id: {0} stream_id: {1} notify queue is full", conn_ctx->conn_id, stream_id);
        }

    } else {
#if __cplusplus >= 202002L
        if (!_cbNotifyQueue.push([=, this]() {
#else
        if (!cbNotifyQueue_.Push([=]() {
#endif
                delegate_.OnRecvStream(conn_ctx->conn_id, stream_id, std::nullopt);
            })) {

            LOGGER_ERROR(logger, "conn_id: {0} stream_id: {1} notify queue is full", conn_ctx->conn_id, stream_id);
        }
    }
}

void PicoQuicTransport::EmitMetrics()
{
    for (auto& [conn_id, conn_ctx] : conn_context_) {
        const auto sample_time = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::steady_clock::now());

        if (metrics_conn_samples) {
            metrics_conn_samples->Push({ sample_time, conn_id, conn_ctx.metrics });
        }

        for (auto& [data_ctx_id, data_ctx] : conn_ctx.active_data_contexts) {

            if (metrics_data_samples) {
                metrics_data_samples->Push({ sample_time, conn_id, data_ctx_id, data_ctx.metrics });
            }
            data_ctx.metrics.ResetPeriod();
        }

        conn_ctx.metrics.ResetPeriod();
    }
}

void PicoQuicTransport::RemoveClosedStreams() {
    std::lock_guard<std::mutex> _(state_mutex_);

    std::vector<uint64_t> closed_streams;
    for (auto& [conn_id, conn_ctx] : conn_context_) {
        std::vector<uint64_t> closed_streams;

        for (auto& [stream_id, rx_buf]: conn_ctx.rx_stream_buffer) {
            if (rx_buf.closed && (rx_buf.buf->Empty() || rx_buf.checked_once)) {
                closed_streams.push_back(stream_id);
            }
            rx_buf.checked_once = true;
        }

        for (const auto stream_id: closed_streams) {
            conn_ctx.rx_stream_buffer.erase(stream_id);
        }
    }
}

void PicoQuicTransport::CheckConnsForCongestion()
{
    std::lock_guard<std::mutex> _(state_mutex_);

    /*
     * A sign of congestion is when transmit queues are not being serviced (e.g., have a backlog).
     * With no congestion, queues will be close to zero in size.
     *
     * Check each queue size to determine if there is possible congestion
     */

    for (auto& [conn_id, conn_ctx] : conn_context_) {
        int congested_count { 0 };
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
        if (cwin_congested_count > 5 || (path_quality.cwin < kPqCcLowCwin
                                         && path_quality.bytes_in_transit)) {

            /* TODO(tievens): comem back to this later. Right now it's too loud
            logger->info << "CC: CWIN congested (fyi only)"
                         << " conn_id: " << conn_id
                         << " cwin_congested_count: " << cwin_congested_count
                         << " rate Kbps: " << path_quality.pacing_rate * 8 / 1000
                         << " cwin_bytes: " << path_quality.cwin
                         << " rtt_us: " << path_quality.rtt
                         << " rtt_max: " << path_quality.rtt_max
                         << " rtt_sample: " << path_quality.rtt_sample
                         << " lost_pkts: " << path_quality.lost
                         << " bytes_in_transit: " << path_quality.bytes_in_transit
                         << " recv_rate_Kbps: " << path_quality.receive_rate_estimate * 8 / 1000
                         << std::flush;
            */
            //congested_count++; /* TODO(tievens): DO NOT react to this right now, causing issue with low latency wired networks */
        }
        conn_ctx.metrics.prev_cwin_congested = conn_ctx.metrics.cwin_congested;

        // All other data flows (streams)
        uint64_t reset_wait_data_ctx_id {0};       // Positive value indicates the data_ctx_id that can be set to reset_wait

        for (auto& [data_ctx_id, data_ctx] : conn_ctx.active_data_contexts) {

            // Skip context that is in reset and wait
            if (data_ctx.tx_reset_wait_discard) {
                continue;
            }

            // Don't include control stream in delayed callbacks check. Control stream should be priority 0 or 1
            if (data_ctx.priority >= 2
                    && data_ctx.metrics.tx_delayed_callback - data_ctx.metrics.prev_tx_delayed_callback > 1) {
                LOGGER_WARN(logger,
                                   "CC: Stream congested, callback count greater than 1 conn_id: {0} data_ctx_id: {1} "
                                   "tx_data_queue: {2} congested_callbacks: {3}",
                                   data_ctx.conn_id,
                                   data_ctx.data_ctx_id,
                                   data_ctx.tx_data->Size(),
                                   data_ctx.metrics.tx_delayed_callback - data_ctx.metrics.prev_tx_delayed_callback);
                congested_count++;
            }
            data_ctx.metrics.prev_tx_delayed_callback = data_ctx.metrics.tx_delayed_callback;

            data_ctx.metrics.tx_queue_size.AddValue(data_ctx.tx_data->Size());

            // TODO(tievens): size of TX is based on rate; adjust based on burst rates
            if (data_ctx.tx_data->Size() >= 50) {
                LOGGER_WARN(
                  logger,
                  "CC: Stream congested, queue backlog conn_id: {0} data_ctx_id: {1} tx_data_queue: {2}",
                  data_ctx.conn_id,
                  data_ctx.data_ctx_id,
                  data_ctx.tx_data->Size());
                congested_count++;
            }

            if (data_ctx.priority >= kPqRestWaitMinPriority
                && data_ctx.uses_reset_wait
                && reset_wait_data_ctx_id == 0
                && !data_ctx.tx_reset_wait_discard) {

                reset_wait_data_ctx_id = data_ctx_id;
            }
        }

        if (cwin_congested_count && conn_ctx.pq_cnx->nb_retransmission_total - conn_ctx.metrics.tx_retransmits > 2) {
            LOGGER_INFO(logger,
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
            LOGGER_WARN(
              logger,
              "CC: conn_id: {0} has streams congested. congested_count: {1} retrans: {2} cwin_congested: {3}",
              conn_id,
              congested_count,
              conn_ctx.metrics.tx_retransmits,
              conn_ctx.metrics.cwin_congested);

            if (tconfig_.use_reset_wait_strategy && reset_wait_data_ctx_id > 0) {
                auto& data_ctx = conn_ctx.active_data_contexts[reset_wait_data_ctx_id];
                LOGGER_INFO(logger,
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
                //close_stream(conn_ctx, &data_ctx, true);
            }


        } else if (conn_ctx.is_congested) {

            if (conn_ctx.not_congested_gauge > 4) {
                // No longer congested
                conn_ctx.is_congested = false;
                conn_ctx.not_congested_gauge = 0;
                LOGGER_INFO(logger, "CC: conn_id: {0} congested_count: {1} is no longer congested.", conn_id, congested_count);
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
void PicoQuicTransport::Server()
{
    int ret = picoquic_packet_loop(quic_ctx_, serverInfo_.port, PF_UNSPEC, 0, 2000000, 0, PqLoopCb, this);

    if (quic_ctx_ != NULL) {
        picoquic_free(quic_ctx_);
        quic_ctx_ = NULL;
    }

    LOGGER_INFO(logger, "picoquic packet loop ended with {0}", ret);

    SetStatus(TransportStatus::kShutdown);
}

TransportConnId PicoQuicTransport::CreateClient()
{
    struct sockaddr_storage server_address;
    char const* sni = "cisco.webex.com";
    int ret;

    int is_name = 0;

    ret = picoquic_get_server_address(serverInfo_.host_or_ip.c_str(), serverInfo_.port, &server_address, &is_name);
    if (ret != 0 || server_address.ss_family == 0) {
        LOGGER_ERROR(logger, "Failed to get server: {0} port: {1}", serverInfo_.host_or_ip, serverInfo_.port);
        SetStatus(TransportStatus::kDisconnected);
        OnConnectionStatus(0, TransportStatus::kShutdown);
        return 0;
    } else if (is_name) {
        sni = serverInfo_.host_or_ip.c_str();
    }

    if (tconfig_.use_bbr) {
        picoquic_set_default_congestion_algorithm(quic_ctx_, picoquic_bbr_algorithm);
    } else {
        picoquic_set_default_congestion_algorithm(quic_ctx_, picoquic_newreno_algorithm);
    }

    uint64_t current_time = picoquic_current_time();

    picoquic_cnx_t* cnx = picoquic_create_cnx(quic_ctx_,
                                              picoquic_null_connection_id,
                                              picoquic_null_connection_id,
                                              reinterpret_cast<struct sockaddr*>(&server_address),
                                              current_time,
                                              0,
                                              sni,
                                              config_.alpn,
                                              1);

    if (cnx == NULL) {
        LOGGER_ERROR(logger, "Could not create picoquic connection client context");
        return 0;
    }

    // Using default TP
    picoquic_set_transport_parameters(cnx, &local_tp_options_);
    picoquic_set_feedback_loss_notification(cnx, 1);

    if (tconfig_.quic_priority_limit > 0) {
        LOGGER_INFO(logger, "Setting priority bypass limit to {0}", static_cast<int>(tconfig_.quic_priority_limit));
        picoquic_set_priority_limit_for_bypass(cnx, tconfig_.quic_priority_limit);
    } else {
        LOGGER_INFO(logger, "No priority bypass");
    }

//    picoquic_subscribe_pacing_rate_updates(cnx, tconfig.pacing_decrease_threshold_Bps,
//                                           tconfig.pacing_increase_threshold_Bps);

    auto &_ = CreateConnContext(cnx);

    return reinterpret_cast<uint64_t>(cnx);
}

void PicoQuicTransport::Client(const TransportConnId conn_id)
{
    int ret;

    auto conn_ctx = GetConnContext(conn_id);

    if (conn_ctx == nullptr) {
        LOGGER_ERROR(logger, "Client connection does not exist, check connection settings.");
        SetStatus(TransportStatus::kDisconnected);
        return;
    }

    LOGGER_INFO(logger, "Thread client packet loop for client conn_id: {0}", conn_id);

    if (conn_ctx->pq_cnx == NULL) {
        LOGGER_ERROR(logger, "Could not create picoquic connection client context");
    } else {
        picoquic_set_callback(conn_ctx->pq_cnx, PqEventCb, this);

        picoquic_enable_keep_alive(conn_ctx->pq_cnx, tconfig_.idle_timeout_ms * 500);
        ret = picoquic_start_client_cnx(conn_ctx->pq_cnx);
        if (ret < 0) {
            LOGGER_ERROR(logger, "Could not activate connection");
            return;
        }

        ret = picoquic_packet_loop(quic_ctx_, 0, PF_UNSPEC, 0, 2000000, 0, PqLoopCb, this);

        LOGGER_INFO(logger, "picoquic ended with {0}", ret);
    }

    if (quic_ctx_ != NULL) {
        picoquic_free(quic_ctx_);
        quic_ctx_ = NULL;
    }

    SetStatus(TransportStatus::kDisconnected);
}

void PicoQuicTransport::Shutdown()
{
    if (stop_) // Already stopped
        return;

    stop_ = true;

    if (picoQuicThread_.joinable()) {
        LOGGER_INFO(logger, "Closing transport pico thread");
        picoQuicThread_.join();
    }

    picoquic_runner_queue_.StopWaiting();
    cbNotifyQueue_.StopWaiting();

    if (cbNotifyThread_.joinable()) {
        LOGGER_INFO(logger, "Closing transport callback notifier thread");
        cbNotifyThread_.join();
    }

    tick_service_.reset();
    LOGGER_INFO(logger, "done closing transport threads");

    picoquic_config_clear(&config_);

    // If logging picoquic events, stop those
    debug_set_callback(NULL, NULL);
}

void PicoQuicTransport::CheckCallbackDelta(DataContext* data_ctx, bool tx) {
    if (!tx) return;

    const auto current_tick = tick_service_->GetTicks(std::chrono::milliseconds(1));

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

        /*
        logger->info << "conn_id: " << data_ctx->conn_id
                      << " data_ctx_id: " << data_ctx->data_ctx_id
                      << " stream_id: " << data_ctx->current_stream_id
                      << " pri: " << static_cast<int>(data_ctx->priority)
                      << " CB TX delta " << delta_ms << " ms"
                      << " cb_tx_count: " << data_ctx->metrics.tx_delayed_callback
                      << " tx_queue_size: " << data_ctx->tx_data->size()
                      << " expired_count: " << data_ctx->metrics.tx_queue_expired
                      << " stream_cb_count: " << data_ctx->metrics.tx_stream_cb
                      << " tx_reset_wait: " << data_ctx->metrics.tx_reset_wait
                      << " tx_queue_discards: " << data_ctx->metrics.tx_queue_discards
                      << " rate Kbps: " << path_quality.pacing_rate * 8 / 1000
                      << " cwin_bytes: " << path_quality.cwin
                      << " rtt_us: " << path_quality.rtt
                      << " rtt_max: " << path_quality.rtt_max
                      << " rtt_sample: " << path_quality.rtt_sample
                      << " lost_pkts: " << path_quality.lost
                      << " bytes_in_transit: " << path_quality.bytes_in_transit
                      << " recv_rate_Kbps: " << path_quality.receive_rate_estimate * 8 / 1000
                      << std::flush;
        */
    }
}

void PicoQuicTransport::CbNotifier()
{
    LOGGER_INFO(logger, "Starting transport callback notifier thread");

    while (not stop_) {
        auto cb = cbNotifyQueue_.BlockPop();
        if (cb) {
            (*cb)();
        } else {
            LOGGER_INFO(logger, "Notify callback is NULL");
        }
    }

    LOGGER_INFO(logger, "Done with transport callback notifier thread");
}

void PicoQuicTransport::CreateStream(ConnectionContext& conn_ctx, DataContext *data_ctx)
{
    conn_ctx.last_stream_id = picoquic_get_next_local_stream_id(conn_ctx.pq_cnx, !data_ctx->is_bidir);

    LOGGER_DEBUG(logger, "conn_id: {0} data_ctx_id: {1} create new stream with stream_id: {2}" << conn_ctx.conn_id, data_ctx->data_ctx_id, conn_ctx.last_stream_id);

    if (data_ctx->current_stream_id) {
        CloseStream(conn_ctx, data_ctx, false);
    }

    data_ctx->current_stream_id = conn_ctx.last_stream_id;

    /*
     * Low order bit set indicates FIFO handling of same priorities, unset is round-robin
     */
    picoquic_set_stream_priority(conn_ctx.pq_cnx, *data_ctx->current_stream_id, (data_ctx->priority << 1));
    picoquic_mark_active_stream(conn_ctx.pq_cnx, *data_ctx->current_stream_id, 1, data_ctx);

    data_ctx->mark_stream_active = true;

    picoquic_runner_queue_.Push([this, conn_id = conn_ctx.conn_id, data_ctx_id = data_ctx->data_ctx_id]() {
        MarkStreamActive(conn_id, data_ctx_id);
    });


}

void PicoQuicTransport::CloseStream(ConnectionContext& conn_ctx, DataContext* data_ctx, const bool send_reset)
{
    if (!data_ctx->current_stream_id.has_value()) {
        return; // stream already closed
    }

    LOGGER_DEBUG(logger, "conn_id: {0} data_ctx_id: {1} closing stream stream_id: {2}", conn_ctx.conn_id, data_ctx->data_ctx_id, *data_ctx->current_stream_id);

    if (send_reset) {
        LOGGER_DEBUG(logger, "Reset stream_id: {0} conn_id: {1}", *data_ctx->current_stream_id, conn_ctx.conn_id);

        picoquic_reset_stream_ctx(conn_ctx.pq_cnx, *data_ctx->current_stream_id);
        picoquic_reset_stream(conn_ctx.pq_cnx, *data_ctx->current_stream_id, 0);

    } else {
        LOGGER_INFO(logger, "Sending FIN for stream_id: {0} conn_id: {1}",*data_ctx->current_stream_id, conn_ctx.conn_id);

        picoquic_reset_stream_ctx(conn_ctx.pq_cnx, *data_ctx->current_stream_id);
        uint8_t empty {0};
        picoquic_add_to_stream(conn_ctx.pq_cnx, *data_ctx->current_stream_id, &empty, 0, 1);
    }

    data_ctx->ResetTxObject();

    if (data_ctx->current_stream_id) {
        const auto rx_buf_it = conn_ctx.rx_stream_buffer.find(*data_ctx->current_stream_id);
        if (rx_buf_it != conn_ctx.rx_stream_buffer.end()) {
            std::lock_guard<std::mutex> _(state_mutex_);
            conn_ctx.rx_stream_buffer.erase(rx_buf_it);
        }
    }

    data_ctx->current_stream_id = std::nullopt;
}

void PicoQuicTransport::MarkStreamActive(const TransportConnId conn_id, const DataContextId data_ctx_id) {
    std::lock_guard<std::mutex> _(state_mutex_);

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

    picoquic_mark_active_stream(conn_it->second.pq_cnx,
                                *data_ctx_it->second.current_stream_id,
                                1,
                                &data_ctx_it->second);
}

void PicoQuicTransport::MarkDgramReady(const TransportConnId conn_id) {
    std::lock_guard<std::mutex> _(state_mutex_);

    const auto conn_it = conn_context_.find(conn_id);
    if (conn_it == conn_context_.end()) {
        return;
    }

    picoquic_mark_datagram_ready(conn_it->second.pq_cnx, 1);

    conn_it->second.mark_dgram_ready = false;
}
