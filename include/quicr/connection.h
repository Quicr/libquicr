// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/messages/ctrl_message_types.h"
#include "quicr/metrics.h"
#include "quicr/track_name.h"
#include "quicr/utilities/bytes.h"
#include "quicr/utilities/thread_safety.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace quicr {

    class PublishTrackHandler;
    class Stream;
    class SubscribeTrackHandler;
    class TrackHandler;

    /// @brief Peer or transport event that closed a stream direction.
    enum class StreamClosedFlag : uint8_t
    {
        kFin,         ///< The send direction ended normally with FIN.
        kReset,       ///< The send direction ended abruptly with RESET_STREAM.
        kStopSending, ///< The peer requested that the local send direction stop.
    };

    /// @brief Transport-independent state and callback dispatch for one connection.
    class Connection
    {
      public:
        /// @brief Network API negotiated for this connection.
        enum class API : std::uint8_t
        {
            kNativeQuic,   ///< Native MoQ over QUIC.
            kWebTransport, ///< MoQ over a WebTransport session.
        };

        /// @brief Connection lifecycle state.
        enum class Status : std::uint8_t
        {
            kReady = 0,          ///< Ready for application traffic.
            kConnecting,         ///< Connection establishment is in progress.
            kRemoteRequestClose, ///< The peer requested closure.
            kDisconnected,       ///< No longer connected.
            kIdleTimeout,        ///< Closed because the idle timeout elapsed.
            kShutdown,           ///< Transport shutdown is complete.
            kShuttingDown,       ///< Transport shutdown is in progress.
        };

        /**
         * @brief Async Callback API on the transport
         */
        class Delegate
        {
          public:
            /// @brief Destroy the delegate.
            virtual ~Delegate() = default;

            /**
             * @brief Event notification for connection status changes
             *
             * @details Called when the connection changes state/status
             *
             * @param status New connection status.
             */
            virtual void OnConnectionStatus(Status status) = 0;

            /**
             * @brief callback notification that datagram data has been received and should be processed
             *
             * @details Datagrams share one queue per connection, so there is nothing to identify here
             *      beyond the connection itself.
             */
            virtual void OnRecvDgram() = 0;

            /**
             * @brief callback notification that data has been received and should be processed
             *
             * @param stream Stream the data arrived on, carrying its receive queue and the handler consuming it.
             *
             * @return `true` if the stream was left with more that can be read now, having stopped
             *      short of draining it so as not to starve the others, and so wants another turn
             */
            virtual bool OnRecvStream(const std::shared_ptr<Stream>& stream) = 0;

            /**
             * @brief Callback notification that a stream has been closed by either FIN or RST.
             *
             * @param stream Stream that closed, carrying the handler it belonged to
             * @param flag Flag value for how the stream was closed. Values are FIN or RST
             */
            virtual void OnStreamClosed(const std::shared_ptr<Stream>& stream, StreamClosedFlag flag) = 0;

            /**
             * @brief callback notification on connection metrics sampled
             *
             * @details This callback will be called when the connection metrics are sampled per connection
             *
             * @param sample_time Sample time in microseconds
             * @param quic_connection_metrics Connection specific metrics for sample period
             */
            virtual void OnConnectionMetricsSampled(const MetricsTimeStamp sample_time,
                                                    const QuicConnectionMetrics& quic_connection_metrics) = 0;

            /**
             * @brief callback notification on stream metrics sampled
             *
             * @details Called once per stream per sample period, before
             *      `OnConnectionMetricsSampled` for the same period. A track carried by several
             *      streams therefore accumulates several of these before its period is complete.
             *      A stream that closed during the period reports once more, so one that came and
             *      went between two samples still counts towards its track.
             *
             * @note Every value covers the period alone, so a running total simply adds each one.
             *
             * @param sample_time Sample time in microseconds
             * @param stream_id Stream the metrics belong to
             * @param quic_stream_metrics Stream metrics for sample period
             * @param is_final Last report for this stream; it no longer exists
             */
            virtual void OnStreamMetricsStampled(const MetricsTimeStamp sample_time,
                                                 std::uint64_t stream_id,
                                                 const QuicStreamMetrics& quic_stream_metrics,
                                                 bool is_final) = 0;
        };

      public:
        /// @brief Construct connection state in `Status::kConnecting`.
        /// @param id Transport-defined connection identifier.
        /// @param api Network API used by the connection.
        Connection(std::uint64_t id, API api = API::kNativeQuic);

        /// @brief Destroy the connection.
        virtual ~Connection() = default;

        /// @brief Return the transport-defined connection identifier.
        /// @return Identifier unique within the owning transport.
        std::uint64_t GetID() const noexcept;

        /// @brief Return the network API used by this connection.
        /// @return Negotiated native QUIC or WebTransport API.
        API GetAPI() const noexcept;

        /**
         * @brief Change connection status and notify the delegate.
         * @param new_status New lifecycle state.
         *
         * @note Reapplying the current status is a no-op.
         */
        void SetStatus(Status new_status);

        /// @brief Return the current connection status.
        /// @return Atomically loaded lifecycle state.
        Status GetStatus() const noexcept { return status_.load(std::memory_order_acquire); }

        /**
         * @brief Replace the non-owning callback delegate.
         * @param session Delegate to observe events, or null to detach it.
         *
         * @details The connection stores a weak reference. Attaching after connection establishment immediately
         *      replays the current non-connecting status.
         */
        void SetDelegate(const std::shared_ptr<Delegate>& session);

        /**
         * @brief Close the sample period, taking its metrics off the connection
         *
         * @warning Must be called with whatever excludes the threads that write those counters,
         *      since it resets them; see `PicoQuicTransport::EmitMetrics` for the transport's.
         * @return Metrics removed from the connection and its streams for the completed period.
         */
        virtual QuicMetricsSample TakeMetricsSample() = 0;

        /**
         * @brief Report a taken sample to the delegate
         *
         * @details Separate from taking it so the reporting can be handed to the delegate's thread
         *      without leaving the counters exposed to it.
         *
         * @param sample_time Wall-clock time associated with the sample.
         * @param sample Metrics previously returned by `TakeMetricsSample`.
         */
        virtual void ReportMetricsSample(const MetricsTimeStamp& sample_time, const QuicMetricsSample& sample) = 0;

        /**
         * @brief Event notification for connection status changes
         *
         * @details Called when the connection changes state/status
         *
         * @param status Transport status value.
         */
        virtual void OnStatusChanged(Status status);

        /**
         * @brief callback notification that datagram data has been received and should be processed
         */
        virtual void OnRecvDgram();

        /**
         * @brief callback notification that data has been received and should be processed
         *
         * @param stream Stream the data arrived on, carrying its receive queue and the handler consuming it.
         *
         * @returns True if the stream was left with more that can be read now, and so wants
         *      another turn; false if there is no delegate to read it
         */
        virtual bool OnRecvStream(const std::shared_ptr<Stream>& stream);

        /**
         * @brief Callback notification that a stream has been closed by either FIN or RST.
         *
         * @param stream Stream that closed, carrying the handler it belonged to
         * @param flag Flag value for how the stream was closed. Values are FIN or RST
         */
        virtual void OnStreamClosed(const std::shared_ptr<Stream>& stream, StreamClosedFlag flag);

        // TODO: Move these to be private.
      public:
        ConnectionMetrics metrics{}; ///< Application-facing cumulative and current-period connection metrics.

      protected:
        /// @brief Lock and promote the current weak delegate.
        /// @return Shared delegate, or null if detached or expired.
        std::shared_ptr<Delegate> GetDelegate() const;

        std::uint64_t id{ 0 }; ///< Transport-defined connection identifier.

        /// The API the connection uses. Default is Native Quic.
        API api_{ API::kNativeQuic };

        std::atomic<Status> status_; ///< Current lifecycle state.

        mutable std::mutex delegate_mutex_; ///< Protects access to the connection delegate.

        /// @brief Non-owning event delegate guarded by the delegate mutex.
        std::weak_ptr<Delegate> delegate_ QUICR_GUARDED_BY(delegate_mutex_);
    };
}
