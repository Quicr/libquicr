// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/attributes.h"
#include "quicr/config.h"
#include "quicr/connection.h"
#include "quicr/containers/stream_buffer.h"
#include "quicr/handlers/fetch_track_handler.h"
#include "quicr/handlers/publish_fetch_handler.h"
#include "quicr/handlers/publish_namespace_handler.h"
#include "quicr/handlers/publish_track_handler.h"
#include "quicr/handlers/subscribe_namespace_handler.h"
#include "quicr/handlers/subscribe_track_handler.h"
#include "quicr/messages/message.h"
#include "quicr/messages/messages.h"
#include "quicr/metrics.h"
#include "quicr/transport.h"

#include <timeq/tick_service.h>

#include <atomic>
#include <chrono>
#include <map>
#include <span>
#include <string>
#include <string_view>

namespace quicr {

    class Logger;

    /**
     * @brief Response to a received subscribe or track status request
     */
    struct RequestResponse
    {
        bool is_publisher_initiated = false;
        std::optional<messages::Location> largest_location = std::nullopt;
        messages::GroupOrder publisher_default_group_order = messages::GroupOrder::kAscending;
    };

    /**
     * @brief Response to a received MOQT Fetch message
     */
    struct FetchResponse
    {
        std::optional<messages::Location> largest_location = std::nullopt;
        messages::GroupOrder publisher_default_group_order = messages::GroupOrder::kAscending;
    };

    /**
     * @brief MoQ Session endpoint supporting connection-explicit operations
     *
     * @details Unified MoQ transport endpoint that operates in either client or server mode depending on
     *   configuration. `Client` provides the single-connection convenience API for outbound clients.
     */
    class Session
      : public Connection::Delegate
      , public std::enable_shared_from_this<Session>
    {
        static constexpr std::size_t kControlMessageBufferSize = 4096;

      public:
        /**
         * @brief Status of the transport
         */
        enum class Status : uint8_t
        {
            kReady = 0,
            kNotReady,

            kInternalError,

            kInvalidParams,

            kConnecting,
            kDisconnecting,
            kNotConnected,
            kFailedToConnect,
            kPendingServerSetup,
        };

        /**
         * @brief Callback interfaces for session events
         *
         * @details Nested under `Session` so that callback signatures can reference `Session::Status` (and other
         *      nested `Session` types) directly. Defined out-of-line in `session_callbacks.h`, which is included
         *      after this class is fully defined.
         */
        struct Callbacks;
        struct ClientCallbacks;
        struct ServerCallbacks;

        /**
         * @brief Control message status codes
         */
        enum class ControlMessageStatus : uint8_t
        {
            kMessageIncomplete,        ///< control message is incomplete and more data is needed
            kMessageComplete,          ///< control message is complete and stream buffer get any has complete message
            kStreamBufferCannotBeZero, ///< stream buffer cannot be zero when parsing message type
            kStreamBufferMissingType,  ///< connection context is missing message type
            kUnsupportedMessageType,   ///< Unsupported MOQT message type
        };

        enum class StreamDataMessageStatus : uint8_t
        {
        };

        /**
         * @brief Connection status codes
         */
        enum class ConnectionStatus : uint8_t
        {
            kNotConnected = 0,
            kConnecting,
            kConnected,
            kIdleTimeout,
            kClosedByRemote
        };

        /**
         * @brief Connection remote information
         */
        struct ConnectionRemoteInfo
        {
            std::string ip; ///< remote IPv4/v6 address
            uint16_t port;  ///< remote port
        };

        /// Subscribe Context by received subscribe IDs
        /// Used to map published tracks to subscribes in client mode and to handle joining fetch lookups
        struct SubscribeContext
        {
            FullTrackName track_full_name;
            TrackHash track_hash{ 0, 0 };
            std::optional<messages::Location> largest_location{ std::nullopt };
            std::shared_ptr<Stream> stream;
        };

        struct RequestUpdateResponse
        {
            struct Error
            {
                const messages::ErrorCode error_code;
                const std::chrono::milliseconds retry_interval;
                const std::string reason;
            };
            const std::optional<Error> error;
            const messages::Parameters params;
        };

      public:
        /**
         * @brief Create a client-mode endpoint
         *
         * @param cfg MoQ Client Configuration
         */
        static std::shared_ptr<Session> Create(const ClientConfig& cfg,
                                               std::shared_ptr<Transport> transport,
                                               std::shared_ptr<Connection> connection,
                                               std::shared_ptr<ClientCallbacks> callbacks,
                                               std::shared_ptr<timeq::tick_service> tick_service,
                                               std::shared_ptr<Logger> logger = nullptr)
        {
            return std::shared_ptr<Session>(new Session(cfg,
                                                        std::move(transport),
                                                        std::move(connection),
                                                        std::move(callbacks),
                                                        std::move(tick_service),
                                                        std::move(logger)));
        }

        /**
         * @brief Create a server-mode endpoint
         *
         * @param cfg MoQ Server Configuration
         */
        static std::shared_ptr<Session> Create(const ServerConfig& cfg,
                                               std::shared_ptr<Transport> transport,
                                               std::shared_ptr<Connection> connection,
                                               std::shared_ptr<ServerCallbacks> callbacks,
                                               std::shared_ptr<timeq::tick_service> tick_service,
                                               std::shared_ptr<Logger> logger = nullptr)
        {
            return std::shared_ptr<Session>(new Session(cfg,
                                                        std::move(transport),
                                                        std::move(connection),
                                                        std::move(callbacks),
                                                        std::move(tick_service),
                                                        std::move(logger)));
        }

        virtual ~Session();

        const std::shared_ptr<Connection>& GetConnection() const noexcept { return current_connection_; }

        const std::shared_ptr<timeq::tick_service>& GetTickService() const noexcept { return tick_service_; }

        const std::shared_ptr<Logger>& GetLogger() const noexcept { return logger_; }

        Status GetStatus() const noexcept { return status_; }

        /**
         * @brief Close the underlying transport connection and detach this session as delegate.
         */
        void Disconnect();

        /*===================================================================*/
        // Public API MoQ Instance API methods
        /*===================================================================*/

        /**
         * @brief Subscribe to a track
         *
         * @param track_handler     Track handler to use for track related functions and callbacks
         *
         */
        void SubscribeTrack(std::shared_ptr<SubscribeTrackHandler> track_handler);

        /**
         * @brief Unsubscribe track
         *
         * @param track_handler     Track handler to use for track related functions and callbacks
         */
        void UnsubscribeTrack(const std::shared_ptr<SubscribeTrackHandler>& track_handler);

        /**
         * @brief Update Subscription to a track
         *
         * @param track_handler     Track handler to use for track related functions and callbacks
         *
         */
        void UpdateTrackSubscription(std::shared_ptr<SubscribeTrackHandler> track_handler);

        /**
         * @brief Publish to a track
         *
         * @param track_handler     Track handler to use for track related functions and callbacks
         */
        void PublishTrack(std::shared_ptr<PublishTrackHandler> track_handler);

        /**
         * @brief Unpublish track
         *
         * @param track_handler     Track handler used when published track
         */
        void UnpublishTrack(const std::shared_ptr<PublishTrackHandler>& track_handler);

        /**
         * @brief Publish a track namespace
         *
         * @param ns_handler        Namespace handler to use for track related functions and callbacks
         * @param passive           True indicates that PUBLISH_NAMESPACE will not be sent
         */
        void PublishNamespace(std::shared_ptr<PublishNamespaceHandler> ns_handler, bool passive = false);

        /**
         * @brief Unpublish track namespace
         *
         * @param track_handler     Track handler used when published track
         */
        void PublishNamespaceDone(const std::shared_ptr<PublishNamespaceHandler>& track_handler);

        /**
         * @brief Subscribe to a prefix namespace on a specific connection
         *
         * @param handler           Namespace handler to subscribe with
         */
        void SubscribeNamespace(std::shared_ptr<SubscribeNamespaceHandler> handler);

        /**
         * @brief Unsubscribe from a prefix namespace on a specific connection
         *
         * @param handler           Namespace handler to unsubscribe
         */
        void UnsubscribeNamespace(const std::shared_ptr<SubscribeNamespaceHandler>& handler);

        /**
         * @brief Fetch track
         *
         * @param track_handler         Track handler used for fetching
         */
        void FetchTrack(std::shared_ptr<FetchTrackHandler> track_handler);

        /**
         * @brief Cancel Fetch track
         *
         * @param track_handler         Fetch Track handler to cancel.
         */
        void CancelFetchTrack(std::shared_ptr<FetchTrackHandler> track_handler);

        /**
         * @brief Request track status
         *
         * @param track_full_name           Track full name
         * @param subscribe_attributes      Subscribe attributes for track status
         *
         * @returns Request ID that is used for the track status request
         */
        std::uint64_t RequestTrackStatus(const FullTrackName& track_full_name,
                                         const SubscribeAttributes& subscribe_attributes);

        // --BEGIN RESOLVE METHODS ---------------------------------------------------------------------------
        /** @name Resolve Methods
         *      Methods to accept or reject inbound requests. Most are used in server mode; `ResolveSubscribe()`
         *      is also used when acting as a publisher in client mode.
         */
        ///@{

        /**
         * @brief Accept or reject a subscribe that was received
         *
         * @details Accept or reject a subscribe received via `SubscribeReceived()` (server mode) or when acting
         *      as a publisher in client mode. The MoQ transport will send the protocol message based on the
         *      `RequestResponse`.
         *
         * @param request_id         Request ID
         * @param track_alias        Track alias the subscriber should use
         * @param subscribe_response Response for the subscribe
         */
        void ResolveSubscribe(uint64_t request_id, uint64_t track_alias, const RequestResponse& subscribe_response);

        /**
         * @brief Accept or reject a fetch that was received
         *
         * @details Accept or reject a fetch received via `StandaloneFetchReceived()` or
         *      `JoiningFetchReceived()`.
         *
         * @param request_id        Request ID
         * @param priority          Subscriber priority for the fetch response
         * @param group_order       Optional group order for the fetch response
         * @param response          Response to the fetch
         */
        void ResolveFetch(uint64_t request_id,
                          std::optional<messages::GroupOrder> group_order,
                          const FetchResponse& response);

        /**
         * @brief Accept or reject a request update
         *
         * @param request_id            Request being updated
         * @param response              Request update response
         */
        void ResolveRequestUpdate(uint64_t request_id, const RequestUpdateResponse& response);

        /**
         * @brief Accept or reject track status that was received
         *
         * @details Accept or reject track status received via TrackStatusReceived(). The MoQ Transport
         *      will send the protocol message based on the RequestResponse. Per MOQT draft-14,
         *      track status request, ok, and error are the same as subscribe
         *
         * @param request_id               Request ID that was provided by TrackStatusReceived
         * @param subscribe_response       Response to the track status request, either Ok or Error.
         *                                 Largest loation should be set if kOk and there is content
         */
        void ResolveTrackStatus(uint64_t request_id, const RequestResponse& subscribe_response);

        ///@}
        // --END RESOLVE METHODS -----------------------------------------------------------------------------

        // --BEGIN SERVER RELAY METHODS ----------------------------------------------------------------------
        /** @name Server Relay Methods
         *      Methods for relaying published content to subscribers. Server mode only.
         */
        ///@{

        /**
         * @brief Bind a server publish track handler based on a subscribe
         *
         * @details The server will create a server publish track handler based on a received subscribe. It will
         *      use this handler to send objects to the subscriber.
         *
         * @param src_id            Connection or peering ID for publisher origin
         * @param request_id        Request ID from the received subscribe
         * @param track_handler     Server publish track handler
         * @param ephemeral         Indicates if persistent state tracking is needed
         */
        void BindPublisherTrack(std::uint64_t src_id,
                                uint64_t request_id,
                                const std::shared_ptr<PublishTrackHandler>& track_handler,
                                bool ephemeral = false);

        /**
         * @brief Unbind a server publish track handler
         *
         * @details Removes a server publish track handler state.
         *
         * @param src_id            Connection or peering ID of the receiving publisher
         * @param track_handler     Server publish track handler
         * @param send_publish_done Indicates to send publish done or not
         */
        void UnbindPublisherTrack(std::uint64_t src_id,
                                  const std::shared_ptr<PublishTrackHandler>& track_handler,
                                  bool send_publish_done = false);

        /**
         * @brief Bind a server fetch publisher track handler
         *
         * @param track_handler The fetch publisher
         */
        void BindFetchTrack(std::shared_ptr<PublishFetchHandler> track_handler);

        /**
         * @brief Unbind a server fetch publisher track handler
         *
         * @param track_handler The fetch publisher
         */
        void UnbindFetchTrack(const std::shared_ptr<PublishFetchHandler>& track_handler);

        ///@}
        // --END SERVER RELAY METHODS ------------------------------------------------------------------------

        // --BEGIN CALLBACKS ---------------------------------------------------------------------------------
        /** @name Client Callbacks
         *      Callbacks invoked in client mode unless noted otherwise.
         */
        ///@{

        /**
         * @brief Notification callback to provide sampled metrics
         *
         * @details Client mode only. Callback will be triggered on `Config::metrics_sample_ms` to provide the
         *      sampled data based on the sample period. After this callback, the period/sample based metrics will
         *      reset and start over for the new period.
         *
         * @param metrics Copy of the connection metrics for the sample period
         */
        virtual void MetricsSampled(const ConnectionMetrics& metrics);

        ///@}

        /** @name Server Callbacks
         *      Callbacks invoked in server mode unless noted otherwise.
         */
        ///@{

        // --END CALLBACKS -----------------------------------------------------------------------------------

      protected:
        Session() = delete;

        /**
         * @brief Client mode constructor
         *
         * @param cfg MoQ Client Configuration
         */
        Session(const ClientConfig& cfg,

                std::shared_ptr<Transport> transport,

                std::shared_ptr<Connection> connection,
                std::shared_ptr<ClientCallbacks> callbacks,
                std::shared_ptr<Logger> logger)
          : Session(cfg,
                    std::move(transport),
                    std::move(connection),
                    std::move(callbacks),
                    std::make_shared<timeq::threaded_tick_service>(cfg.tick_service_sleep_delay_us),
                    std::move(logger))
        {
        }

        /**
         * @brief Server mode constructor
         *
         * @param cfg MoQ Server Configuration
         */
        Session(const ServerConfig& cfg,

                std::shared_ptr<Transport> transport,

                std::shared_ptr<Connection> connection,
                std::shared_ptr<ServerCallbacks> callbacks,
                std::shared_ptr<Logger> logger)
          : Session(cfg,
                    std::move(transport),
                    std::move(connection),
                    std::move(callbacks),
                    std::make_shared<timeq::threaded_tick_service>(cfg.tick_service_sleep_delay_us),
                    std::move(logger))
        {
        }

        /**
         * @brief Client mode constructor with explicit tick service
         *
         * @param cfg            MoQ Instance Client Configuration
         * @param tick_service   Shared pointer to the tick service to use
         */
        Session(const ClientConfig& cfg,
                std::shared_ptr<Transport> transport,
                std::shared_ptr<Connection> connection,
                std::shared_ptr<ClientCallbacks> callbacks,
                std::shared_ptr<timeq::tick_service> tick_service,
                std::shared_ptr<Logger> logger);

        /**
         * @brief Server mode constructor with explicit tick service
         *
         * @param cfg            MoQ Server Configuration
         * @param tick_service   Shared pointer to the tick service to use
         */
        Session(const ServerConfig& cfg,
                std::shared_ptr<Transport> transport,
                std::shared_ptr<Connection> connection,
                std::shared_ptr<ServerCallbacks> callbacks,
                std::shared_ptr<timeq::tick_service> tick_service,
                std::shared_ptr<Logger> logger);

        void OnStreamClosed(std::uint64_t stream_id,
                            std::shared_ptr<StreamRxContext> rx_ctx,
                            StreamClosedFlag flag) override;

      private:
        /*===================================================================*/
        // Transport Delegate/callback functions
        /*===================================================================*/

        void OnConnectionStatus(Connection::Status status) override;

        void OnRecvStream(uint64_t stream_id,
                          const std::shared_ptr<StreamRxContext>& rx_ctx,
                          const std::shared_ptr<Stream>& stream,
                          const bool is_bidir = false) override;

        void OnRecvDgram() override;

        void OnConnectionMetricsSampled(MetricsTimeStamp sample_time,

                                        const QuicConnectionMetrics& quic_connection_metrics) override;

        void OnStreamMetricsStampled(MetricsTimeStamp sample_time,
                                     std::uint64_t stream_id,
                                     const QuicStreamMetrics& quic_stream_metrics) override;

        /*===================================================================*/
        // Private methods
        /*===================================================================*/

        void Init();

        std::shared_ptr<Session> GetSharedPtr();

        bool ProcessRequestMessage(const std::shared_ptr<Stream>& stream,
                                   messages::ControlMessageType msg_type,
                                   BytesSpan msg_bytes);

        bool ProcessCtrlMessage(messages::ControlMessageType msg_type, BytesSpan msg_bytes);

        void SetStatus(Status status);

        void SendCtrlMsg(const std::shared_ptr<Stream>& stream, std::shared_ptr<const std::vector<uint8_t>> data);

        template<typename... Fields>
        void SendCtrlMsg(const std::shared_ptr<Stream>& stream, messages::ControlMessageType type, Fields&&... args)
        {
            messages::Message msg = messages::Message{}.PrependType(type).ReserveLength();

            (msg.Append(args), ...);

            SendCtrlMsg(stream, msg.ToBytes());
        }

        void SendSetup();

        /*===================================================================*/
        // Requests
        /*===================================================================*/

        void SendTrackStatusOk(const std::shared_ptr<Stream>& stream,
                               const std::optional<messages::Location>& largest_object,
                               const messages::TrackExtensions& track_properties);

        void SendSubscribeNamespaceOk(const std::shared_ptr<Stream>& stream);

        void SendSubscribeTracksOk(const std::shared_ptr<Stream>& stream) { SendSubscribeNamespaceOk(stream); }

        void SendPublishNamespaceOk(const std::shared_ptr<Stream>& stream) { SendSubscribeNamespaceOk(stream); }

        void SendRequestUpdateOk(const std::shared_ptr<Stream>& stream,
                                 std::optional<std::uint64_t> expires,
                                 const std::optional<messages::Location>& largest_object);

        // Prefer the above typed overloads.
        void SendRequestOk(const std::shared_ptr<Stream>& stream,
                           const messages::Parameters& params,
                           const messages::TrackExtensions& track_properties = {});

        void SendRequestUpdate(const std::shared_ptr<Stream>& stream,
                               TrackHash th,
                               std::optional<std::uint64_t> end_group_id,
                               std::uint8_t priority,
                               bool forward);

        void SendRequestError(const std::shared_ptr<Stream>& stream,
                              std::uint64_t request_id,
                              messages::ErrorCode error,
                              std::chrono::milliseconds retry_interval,
                              const std::string& reason);

        /*===================================================================*/
        // Publish Namespace
        /*===================================================================*/

        void SendPublishNamespace(const std::shared_ptr<Stream>& stream,
                                  std::uint64_t request_id,
                                  const TrackNamespace& track_namespace);

        /*===================================================================*/
        // Subscribe Namespace
        /*===================================================================*/

        void SendSubscribeNamespace(const std::shared_ptr<Stream>& stream,
                                    std::uint64_t request_id,
                                    const TrackNamespace& prefix,
                                    const messages::Filter& filter,
                                    messages::ControlMessageType type);

        void SendUnsubscribeNamespace(const std::shared_ptr<Stream>& stream, const TrackNamespace& prefix);

        /*===================================================================*/
        // Subscribe
        /*===================================================================*/

        void SendSubscribe(const std::shared_ptr<Stream>& stream,
                           std::uint64_t request_id,
                           const FullTrackName& tfn,
                           TrackHash th,
                           std::uint8_t priority,
                           std::optional<messages::GroupOrder> group_order,
                           const messages::Filter& filter,
                           std::optional<std::chrono::milliseconds> delivery_timeout);

        void SendSubscribeOk(const std::shared_ptr<Stream>& stream,
                             std::uint64_t request_id,
                             uint64_t track_alias,
                             uint64_t expires,
                             const std::optional<messages::Location>& largest_location,
                             messages::GroupOrder publisher_default_group_order);

        /*===================================================================*/
        // Publish
        /*===================================================================*/

        void SendPublish(const std::shared_ptr<Stream>& stream,
                         std::uint64_t request_id,
                         const PublishAttributes& publish);

        void SendPublishDone(const std::shared_ptr<Stream>& stream,
                             std::uint64_t request_id,
                             messages::PublishDoneStatusCode status,
                             const std::string& reason);

        void SendPublishOk(const std::shared_ptr<Stream>& stream, const PublishOkAttributes& attributes);

        std::shared_ptr<Stream> FindSubscribeNamespaceStream(const TrackNamespace& track_namespace) const;

        std::shared_ptr<Stream> ResponseStream(const std::uint64_t request_id) const;

        /*===================================================================*/
        // Track Status
        /*===================================================================*/

        void SendTrackStatus(std::uint64_t request_id, const FullTrackName& tfn);

        /*===================================================================*/
        // Fetch
        /*===================================================================*/

        void SendFetch(const std::shared_ptr<Stream>& stream,
                       std::uint64_t request_id,
                       const FullTrackName& tfn,
                       std::uint8_t priority,
                       std::optional<messages::GroupOrder> group_order,
                       const messages::Location& start_location,
                       const messages::FetchEndLocation& end_location);

        void SendJoiningFetch(const std::shared_ptr<Stream>& stream,
                              std::uint64_t request_id,
                              std::uint8_t priority,
                              std::optional<messages::GroupOrder> group_order,
                              std::uint64_t joining_request_id,
                              std::uint64_t joining_start,
                              bool absolute);

        void SendFetchOk(const std::shared_ptr<Stream>& stream,
                         messages::GroupOrder publisher_default_group_order,
                         bool end_of_track,
                         messages::Location end_location);

        /*===================================================================*/
        // Other member functions
        /*===================================================================*/

        void RemoveSubscribeTrack(SubscribeTrackHandler& handler, bool remove_handler = true);

        void RemoveSubscribeNamespace(SubscribeNamespaceHandler& handler,
                                      bool remove_handler = true,
                                      bool send_unsubscribe = true);

        void CloseRequestHandler(std::uint64_t request_id, std::uint64_t stream_id, StreamClosedFlag flag);

        void ClosePublishTrackLocal(PublishTrackHandler& handler, std::uint64_t stream_id, bool is_reset);

        std::shared_ptr<PublishTrackHandler> GetPubTrackHandler(TrackHash& th);

        void RemoveAllTracksForConnectionClose();

        uint64_t GetNextRequestID();

        bool OnRecvSubgroup(std::uint64_t track_alias, StreamRxContext& rx_ctx, std::uint64_t stream_id);

        bool OnRecvFetch(std::uint64_t request_id, StreamRxContext& rx_ctx, std::uint64_t stream_id);

        /**
         * @brief Create a data stream for a track.
         *
         * @param request_id  Track the stream carries, so that its metrics find the right handler.
         */
        std::shared_ptr<Stream> CreateStream(std::uint64_t request_id, uint8_t priority);

        /**
         * @brief Send published data.
         *
         * @param stream  Stream to send on, ignored when the flags ask for a datagram.
         */
        TransportError Enqueue(const std::shared_ptr<Stream>& stream,
                               std::shared_ptr<const std::vector<uint8_t>> bytes,
                               const uint8_t priority,
                               const uint32_t ttl_ms,
                               const Transport::EnqueueFlags flags);

      private:
        std::shared_ptr<Connection> current_connection_;

        std::shared_ptr<Callbacks> callbacks_;

        std::optional<std::uint64_t> rx_ctrl_stream_id_;

        std::shared_ptr<Stream> tx_ctrl_stream_;

        ///< Control message buffers for streams.
        std::map<std::uint64_t, InitialStreamData> stream_buffers;

        /**
         * Next Connection request Id. This value is shifted left when setting Request Id.
         * The least significant bit is used to indicate client (0) vs server (1).
         */
        std::atomic<uint64_t> next_request_id_;

        std::map<std::uint64_t, SubscribeContext> recv_req_id;

        struct StreamRequest
        {
            std::uint64_t request_id;

            /**
             * Whether this is the request's own bidirectional stream, whose close ends the request.
             *
             * @details A track's data streams are mapped here too, but only so that their metrics
             *      find the right handler; they come and go while the request stays open.
             */
            bool is_request_stream;
        };

        /// Lookup the request each stream carries, by stream ID.
        std::map<std::uint64_t, StreamRequest> request_by_stream;

        /// Active inbound publish namespace notifications (not handler based).
        std::vector<std::uint64_t> recv_publish_namespaces;

        /// Handlers by request ID
        std::map<std::uint64_t, std::shared_ptr<TrackHandler>> request_handlers;

        /**
         * Data is received with a track alias that is set by the publisher. The map key
         * track alias is the received publisher track alias specific to the connection. Data received
         * is matched to this track alias to find the subscriber handler that matches. The
         * subscribe handler has both received track alias and generated track alias.
         */
        std::map<std::uint64_t, std::shared_ptr<SubscribeTrackHandler>> sub_by_recv_track_alias;

        /**
         * Publish tracks by namespace and name. map[track namespace][track name] = track handler
         * Used mainly in client mode only
         */
        std::map<std::uint64_t, std::map<std::uint64_t, std::shared_ptr<PublishTrackHandler>>> pub_tracks_by_name;

        /// Publish tracks to subscriber by source id of publisher - required for multi-publisher
        std::map<std::uint64_t, std::map<uint64_t, std::shared_ptr<PublishTrackHandler>>> pub_tracks_by_track_alias;

        /// Fetch Publishers by request ID.
        std::map<std::uint64_t, std::shared_ptr<PublishTrackHandler>> pub_fetch_tracks_by_request_id;

        std::mutex state_mutex_;

        const bool client_mode_;

        std::shared_ptr<Logger> logger_;

        bool stop_{ false };

        const ServerConfig server_config_;

        const ClientConfig client_config_;

        Status status_{ Status::kNotReady };

        std::shared_ptr<timeq::tick_service> tick_service_;

        std::shared_ptr<Transport> quic_transport_; // **MUST** be last for proper order of destruction

        friend class PublishTrackHandler;
        friend class PublishFetchHandler;
        friend class SubscribeTrackHandler;
    };

} // namespace quicr
