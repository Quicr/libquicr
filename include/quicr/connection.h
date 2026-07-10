// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/messages/ctrl_message_types.h"
#include "quicr/metrics.h"
#include "quicr/track_name.h"
#include "quicr/utilities/bytes.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace quicr {

    class PublishTrackHandler;
    class SubscribeTrackHandler;
    class TrackHandler;

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
             * @brief Report a new data context created
             *
             * @details Report that a new data context was created for a new bi-directional
             *  stream that was received. This method is not called for app created
             *  data contexts.
             *
             * @param[in] conn_id	Transport context identifier mapped to the connection
             * @param[in] data_ctx_id	Data context id for a new data context received by the transport
             */
            virtual void OnNewDataContext(const std::uint64_t& data_ctx_id) = 0;

            /**
             * @brief callback notification that data has been received and should be processed
             *
             * @param[in] conn_id 	Transport context identifier mapped to the connection
             * @param[in] data_ctx_id	If known, Data context id that the data was received on
             */
            virtual void OnRecvDgram(std::optional<std::uint64_t> data_ctx_id) = 0;

            /**
             * @brief callback notification that data has been received and should be processed
             *
             * @param[in] stream_id     Transport stream ID
             * @param[in] data_ctx_id	If known, Data context id that the data was received on
             * @param[in] is_bidir      True if the message is from a bidirectional stream
             */
            virtual void OnRecvStream(std::uint64_t stream_id,
                                      std::optional<std::uint64_t> data_ctx_id,
                                      bool is_bidir = false) = 0;

            /**
             * @brief Callback notification that a stream has been closed by either FIN or RST.
             *
             * @param stream_id         Transport stream id.
             * @param rx_ctx            Stream Rx context with the handler info.
             * @param data_ctx_id       Optional data context ID the stream belonged to
             * @param flag              Flag value for how the stream was closed. Values are FIN or RST
             */
            virtual void OnStreamClosed(std::uint64_t stream_id,
                                        std::shared_ptr<StreamRxContext> rx_ctx,
                                        std::optional<uint64_t> data_ctx_id,
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
             * @brief callback notification on data context metrics sampled
             *
             * @details This callback will be called when the data context metrics are sampled
             *
             * @param sample_time                   Sample time in microseconds
             * @param data_ctx_id                   Data context ID for metrics
             * @param quic_data_context_metrics     Data context metrics for sample period
             */
            virtual void OnDataMetricsStampled(const MetricsTimeStamp sample_time,
                                               const std::uint64_t data_ctx_id,
                                               const QuicDataContextMetrics& quic_data_context_metrics) = 0;
        };

      public:
        Connection(std::uint64_t id, API api = API::kNativeQuic);

        Connection(const Connection& other);

        virtual ~Connection() = default;

        /**
         * @brief Get the next request ID to use. IDs increase by 2.
         * @returns The next request ID.
         */
        std::uint64_t GetNextRequestID();

        void SetStartingRequestID(std::uint64_t starting_id);

        std::uint64_t GetID() const noexcept;

        API GetAPI() const noexcept;

        void SetStatus(Status new_status);

        void SetDelegate(const std::shared_ptr<Delegate>& session);

        virtual void SampleMetrics(const MetricsTimeStamp sample_time) = 0;

        /**
         * @brief Event notification for connection status changes
         *
         * @details Called when the connection changes state/status
         *
         * @param[in] status 	    Transport Status value
         */
        virtual void OnStatusChanged(Status status);

        /**
         * @brief Report a new data context created
         *
         * @details Report that a new data context was created for a new bi-directional
         *  stream that was received. This method is not called for app created
         *  data contexts.
         *
         * @param[in] data_ctx_id	Data context id for a new data context received by the transport
         */
        virtual void OnNewDataContext(const std::uint64_t& data_ctx_id);

        /**
         * @brief callback notification that data has been received and should be processed
         *
         * @param[in] data_ctx_id	If known, Data context id that the data was received on
         */
        virtual void OnRecvDgram(std::optional<std::uint64_t> data_ctx_id);

        /**
         * @brief callback notification that data has been received and should be processed
         *
         * @param[in] stream_id     Transport stream ID
         * @param[in] data_ctx_id	If known, Data context id that the data was received on
         * @param[in] is_bidir      True if the message is from a bidirectional stream
         */
        virtual void OnRecvStream(std::uint64_t stream_id,
                                  std::optional<std::uint64_t> data_ctx_id,
                                  bool is_bidir = false);

        /**
         * @brief Callback notification that a stream has been closed by either FIN or RST.
         *
         * @param stream_id         Transport stream id.
         * @param rx_ctx            Stream Rx context with the handler info.
         * @param data_ctx_id       Optional data context ID the stream belonged to
         * @param flag              Flag value for how the stream was closed. Values are FIN or RST
         */
        virtual void OnStreamClosed(std::uint64_t stream_id,
                                    std::shared_ptr<StreamRxContext> rx_ctx,
                                    std::optional<uint64_t> data_ctx_id,
                                    StreamClosedFlag flag);

        // TODO: Move these to be private.
      public:
        std::optional<std::uint64_t> tx_ctrl_data_ctx_id;

        std::optional<std::uint64_t> tx_ctrl_stream_id;

        std::optional<std::uint64_t> rx_ctrl_stream_id;

        ///< True if both client and server setup messages have completed
        bool setup_complete{ false };

        bool closed{ false };

        ///< Control message buffers for streams.
        std::map<std::uint64_t, InitialStreamData> stream_buffers;

        ///< Connection metrics
        ConnectionMetrics metrics{};

      protected:
        std::uint64_t id{ 0 };

        /**
         * Next Connection request Id. This value is shifted left when setting Request Id.
         * The least significant bit is used to indicate client (0) vs server (1).
         */
        std::atomic<uint64_t> next_request_id{ 0 };

        /// The API the connection uses. Default is Native Quic.
        API api = API::kNativeQuic;

        Status status_;

        std::weak_ptr<Delegate> delegate_;
    };
}
