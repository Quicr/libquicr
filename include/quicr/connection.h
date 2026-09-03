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

    struct StreamRxContext;

    enum class StreamClosedFlag : uint8_t
    {
        kFin,
        kReset,
    };

    class Connection
    {
      public:
        enum class API : std::uint8_t
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
             * @param[in] conn_id           Transport context Id
             * @param[in] status 	    Transport Status value
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
             * @param[in] stream_id     Transport stream ID
             * @param[in] rx_ctx        Stream Rx context holding the received data queue
             * @param[in] stream        Stream the data arrived on, for replying on a request stream.
             *                          Null if the transport does not have a handle for it.
             * @param[in] is_bidir      True if the message is from a bidirectional stream
             */
            virtual void OnRecvStream(std::uint64_t stream_id,
                                      const std::shared_ptr<StreamRxContext>& rx_ctx,
                                      const std::shared_ptr<Stream>& stream,
                                      bool is_bidir = false) = 0;

            /**
             * @brief Callback notification that a stream has been closed by either FIN or RST.
             *
             * @param stream_id         Transport stream id.
             * @param rx_ctx            Stream Rx context with the handler info.
             * @param flag              Flag value for how the stream was closed. Values are FIN or RST
             */
            virtual void OnStreamClosed(std::uint64_t stream_id,
                                        std::shared_ptr<StreamRxContext> rx_ctx,
                                        StreamClosedFlag flag) = 0;

            /**
             * @brief callback notification on connection metrics sampled
             *
             * @details This callback will be called when the connection metrics are sampled per connection
             *
             * @param sample_time                    Sample time in microseconds
             * @param quic_connection_metrics        Connection specific metrics for sample period
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
             * @param sample_time           Sample time in microseconds
             * @param stream_id             Stream the metrics belong to
             * @param quic_stream_metrics   Stream metrics for sample period
             * @param is_final              Last report for this stream; it no longer exists
             */
            virtual void OnStreamMetricsStampled(const MetricsTimeStamp sample_time,
                                                 std::uint64_t stream_id,
                                                 const QuicStreamMetrics& quic_stream_metrics,
                                                 bool is_final) = 0;
        };

      public:
        Connection(std::uint64_t id, API api = API::kNativeQuic);

        virtual ~Connection() = default;

        std::uint64_t GetID() const noexcept;

        API GetAPI() const noexcept;

        void SetStatus(Status new_status);

        Status GetStatus() const noexcept { return status_; }

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
         * @param[in] stream_id     Transport stream ID
         * @param[in] rx_ctx        Stream Rx context holding the received data queue
         * @param[in] stream        Stream the data arrived on, for replying on a request stream.
         *                          Null if the transport does not have a handle for it.
         * @param[in] is_bidir      True if the message is from a bidirectional stream
         */
        virtual void OnRecvStream(std::uint64_t stream_id,
                                  const std::shared_ptr<StreamRxContext>& rx_ctx,
                                  const std::shared_ptr<Stream>& stream,
                                  bool is_bidir = false);

        /**
         * @brief Callback notification that a stream has been closed by either FIN or RST.
         *
         * @param stream_id         Transport stream id.
         * @param rx_ctx            Stream Rx context with the handler info.
         * @param flag              Flag value for how the stream was closed. Values are FIN or RST
         */
        virtual void OnStreamClosed(std::uint64_t stream_id,
                                    std::shared_ptr<StreamRxContext> rx_ctx,
                                    StreamClosedFlag flag);

        // TODO: Move these to be private.
      public:
        ///< Connection metrics
        ConnectionMetrics metrics{};

      protected:
        std::shared_ptr<Delegate> GetDelegate() const;

        std::uint64_t id{ 0 };

        /// The API the connection uses. Default is Native Quic.
        API api_{ API::kNativeQuic };

        Status status_;

        mutable std::mutex delegate_mutex_;
        std::weak_ptr<Delegate> delegate_ QUICR_GUARDED_BY(delegate_mutex_);
    };
}
