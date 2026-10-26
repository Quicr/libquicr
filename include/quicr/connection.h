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

    enum class StreamClosedFlag : uint8_t
    {
        kFin,
        kReset,
        kStopSending,
    };

    class Connection : public std::enable_shared_from_this<Connection>
    {
      public:
        enum class Protocol : std::uint8_t
        {
            kNativeQuic,
            kWebTransport,
        };

        enum class Status : std::uint8_t
        {
            kReady = 0,
            kConnecting,
            kRemoteRequestClose,
            kDisconnected,
            kIdleTimeout,
            kShutdown,
            kShuttingDown,
        };

        enum class SetupState
        {
            kNone,
            kSent,
            kReceived,
            kComplete,
        };

        /**
         * @brief Async Callback API on the transport
         */
        class Delegate
        {
          public:
            virtual ~Delegate() = default;

            /**
             * @brief Event notification for connection status changes
             *
             * @details Called when the connection changes state/status
             *
             * @param[in] connection        Connection whose status changed
             * @param[in] status            Transport status value
             */
            virtual void OnConnectionStatus(const std::shared_ptr<Connection>& connection, Status status) = 0;

            /**
             * @brief callback notification that datagram data has been received and should be processed
             *
             * @details Datagrams share one queue per connection, so there is nothing to identify here
             *      beyond the connection itself.
             */
            virtual void OnRecvDgram(const std::shared_ptr<Connection>& connection) = 0;

            /**
             * @brief callback notification that data has been received and should be processed
             *
             * @param[in] stream        Stream the data arrived on, carrying its receive queue and
             *                          the handler consuming it
             *
             * @returns True if the stream was left with more that can be read now, having stopped
             *      short of draining it so as not to starve the others, and so wants another turn
             */
            virtual bool OnRecvStream(const std::shared_ptr<Stream>& stream) = 0;

            /**
             * @brief Callback notification that a stream has been closed by either FIN or RST.
             *
             * @param stream            Stream that closed, carrying the handler it belonged to
             * @param flag              Flag value for how the stream was closed. Values are FIN or RST
             */
            virtual void OnStreamClosed(const std::shared_ptr<Stream>& stream, StreamClosedFlag flag) = 0;

            /**
             * @brief callback notification on connection metrics sampled
             *
             * @details This callback will be called when the connection metrics are sampled per connection
             *
             * @param connection                     Connection whose metrics were sampled
             * @param sample_time                    Sample time in microseconds
             * @param quic_connection_metrics        Connection specific metrics for sample period
             */
            virtual void OnConnectionMetricsSampled(const std::shared_ptr<Connection>& connection,
                                                    const MetricsTimeStamp sample_time,
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
             * @param connection            Connection the stream belongs to
             * @param sample_time           Sample time in microseconds
             * @param stream_id             Stream the metrics belong to
             * @param quic_stream_metrics   Stream metrics for sample period
             * @param is_final              Last report for this stream; it no longer exists
             */
            virtual void OnStreamMetricsStampled(const std::shared_ptr<Connection>& connection,
                                                 const MetricsTimeStamp sample_time,
                                                 std::uint64_t stream_id,
                                                 const QuicStreamMetrics& quic_stream_metrics,
                                                 bool is_final) = 0;
        };

      public:
        Connection(std::uint64_t id, Protocol api = Protocol::kNativeQuic);

        virtual ~Connection() = default;

        std::uint64_t GetID() const noexcept;

        Protocol GetProtocol() const noexcept;

        void SetStatus(Status new_status);

        Status GetStatus() const noexcept { return status_.load(std::memory_order_acquire); }

        void SetDelegate(const std::shared_ptr<Delegate>& session);

        /**
         * @brief Close the sample period, taking its metrics off the connection
         *
         * @warning Must be called with whatever excludes the threads that write those counters,
         *      since it resets them; see `PicoQuicTransport::EmitMetrics` for the transport's.
         */
        virtual QuicMetricsSample TakeMetricsSample() = 0;

        /**
         * @brief Report a taken sample to the delegate
         *
         * @details Separate from taking it so the reporting can be handed to the delegate's thread
         *      without leaving the counters exposed to it.
         */
        virtual void ReportMetricsSample(const MetricsTimeStamp& sample_time, const QuicMetricsSample& sample) = 0;

        /**
         * @brief Event notification for connection status changes
         *
         * @details Called when the connection changes state/status
         *
         * @param[in] status 	    Transport Status value
         */
        virtual void OnStatusChanged(Status status);

        /**
         * @brief callback notification that datagram data has been received and should be processed
         */
        virtual void OnRecvDgram();

        /**
         * @brief callback notification that data has been received and should be processed
         *
         * @param[in] stream        Stream the data arrived on, carrying its receive queue and the
         *                          handler consuming it
         *
         * @returns True if the stream was left with more that can be read now, and so wants
         *      another turn; false if there is no delegate to read it
         */
        virtual bool OnRecvStream(const std::shared_ptr<Stream>& stream);

        /**
         * @brief Callback notification that a stream has been closed by either FIN or RST.
         *
         * @param stream            Stream that closed, carrying the handler it belonged to
         * @param flag              Flag value for how the stream was closed. Values are FIN or RST
         */
        virtual void OnStreamClosed(const std::shared_ptr<Stream>& stream, StreamClosedFlag flag);

        /**
         * @brief Record one side of the SETUP exchange.
         *
         * @returns False when the same side was already recorded.
         */
        bool UpdateSetupState(SetupState state);

        SetupState GetSetupState() const;

        void SetRxControlStreamId(std::uint64_t stream_id);

        std::optional<std::uint64_t> GetRxControlStreamId() const;

        void SetTxControlStream(std::shared_ptr<Stream> stream);

        std::shared_ptr<Stream> GetTxControlStream() const;

      public:
        ///< Connection metrics
        ConnectionMetrics metrics{};

      protected:
        std::shared_ptr<Delegate> GetDelegate() const;

        std::uint64_t id{ 0 };

        /// The protocol the connection uses. Default is Native Quic.
        Protocol protocol_{ Protocol::kNativeQuic };

        std::atomic<Status> status_;

        mutable std::mutex delegate_mutex_;
        std::weak_ptr<Delegate> delegate_ QUICR_GUARDED_BY(delegate_mutex_);

        mutable std::mutex session_state_mutex_;

        SetupState setup_state_ QUICR_GUARDED_BY(session_state_mutex_){ SetupState::kNone };

        std::optional<std::uint64_t> rx_ctrl_stream_id_ QUICR_GUARDED_BY(session_state_mutex_);

        std::shared_ptr<Stream> tx_ctrl_stream_ QUICR_GUARDED_BY(session_state_mutex_);
    };
}
