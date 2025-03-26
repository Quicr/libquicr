// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/messages.h"
#include <quicr/config.h>
#include <quicr/detail/transport.h>
#include <quicr/object.h>
#include <quicr/publish_fetch_handler.h>
#include <quicr/track_name.h>

namespace quicr {
    using namespace quicr;

    /**
     * @brief MoQ Server
     *
     * @details MoQ Server is the handler of the MoQ QUIC listening socket
     */
    class Server : public Transport
    {
      public:
        /**
         * @brief Response to received MOQT ClientSetup message
         */
        struct ClientSetupResponse
        {};

        /**
         * @brief Response to received MOQT Announce message
         */
        struct AnnounceResponse
        {
            /**
             * @details **kOK** indicates that the announce is accepted and OK should be sent. Any other
             *       value indicates that the announce is not accepted and the reason code and other
             *       fields will be set.
             */
            enum class ReasonCode : uint8_t
            {
                kOk = 0,
                kInternalError
            };
            ReasonCode reason_code;

            std::optional<Bytes> reason_phrase;
        };

        /**
         * @brief MoQ Server constructor to create the MOQ server mode instance
         *
         * @param cfg           MoQ Server Configuration
         */
        Server(const ServerConfig& cfg)
          : Transport(cfg, std::make_shared<ThreadedTickService>())
        {
        }

        Server(const ServerConfig& cfg, std::shared_ptr<ThreadedTickService> tick_service)
          : Transport(cfg, tick_service)
        {
        }

        ~Server() = default;

        /**
         * @brief Starts server transport thread to listen for new connections
         *
         * @details Creates a new transport thread to listen for new connections. All control and track
         *   callbacks will be run based on events.
         *
         * @return Status indicating state or error. If successful, status will be
         *    READY.
         */
        Status Start();

        /**
         * Stop the server transport
         */
        void Stop();

        /**
         * @brief Bind a server publish track handler based on a subscribe
         *
         * @details The server will create a server publish track handler based on a received
         *      subscribe. It will use this handler to send objects to subscriber.
         *
         * @param connection_handle         Connection ID of the client/subscriber
         * @param subscribe_id              Subscribe ID from the received subscribe
         * @param track_handler             Server publish track handler
         * @param ephemeral                 Bool value to indicate if the state tracking is needed
         */
        void BindPublisherTrack(ConnectionHandle connection_handle,
                                uint64_t subscribe_id,
                                const std::shared_ptr<PublishTrackHandler>& track_handler,
                                bool ephemeral = false);

        /**
         * @brief Unbind a server publish track handler
         *
         * @details Removes a server publish track handler state.
         *
         * @param connection_handle         Connection ID of the client/subscriber
         * @param track_handler             Server publish track handler
         */
        void UnbindPublisherTrack(ConnectionHandle connection_handle,
                                  const std::shared_ptr<PublishTrackHandler>& track_handler);

        /**
         * @brief Bind a server fetch publisher track handler.
         * @param conn_id Connection Id of the client/fetcher.
         * @param track_handler The fetch publisher.
         */
        void BindFetchTrack(TransportConnId conn_id, std::shared_ptr<PublishFetchHandler> track_handler);

        /**
         * @brief Unbind a server fetch publisher track handler.
         * @param conn_id Connection ID of the client/fetcher.
         * @param track_handler The fetch publisher.
         */
        void UnbindFetchTrack(ConnectionHandle conn_id, const std::shared_ptr<PublishFetchHandler>& track_handler);

        /**
         * @brief Accept or reject an subscribe that was received
         *
         * @details Accept or reject an subscribe received via SubscribeReceived(). The MoQ Transport
         *      will send the protocol message based on the SubscribeResponse
         *
         * @param connection_handle        source connection ID
         * @param subscribe_id             subscribe ID
         * @param subscribe_response       response to for the subscribe
         */
        virtual void ResolveSubscribe(ConnectionHandle connection_handle,
                                      uint64_t subscribe_id,
                                      const SubscribeResponse& subscribe_response);

        // --BEGIN CALLBACKS ----------------------------------------------------------------------------------
        /** @name Server Calbacks
         *      slient transport specific callbacks
         */
        ///@{

        /**
         * @brief Callback notification on new connection
         * @details Callback notification that a new connection has been accepted
         *
         * @param connection_handle          Transport connection ID
         * @param remote                     Transport remote connection information
         */
        void NewConnectionAccepted(ConnectionHandle connection_handle, const ConnectionRemoteInfo& remote) override;

        /**
         * @brief Callback notification for connection status/state change
         * @details Callback notification indicates state change of connection, such as disconnected
         *
         * @param connection_handle          Transport connection ID
         * @param status                     ConnectionStatus of connection id
         */
        void ConnectionStatusChanged(ConnectionHandle connection_handle, ConnectionStatus status) override;

        /**
         * @brief Notification callback to provide sampled metrics
         *
         * @details Callback will be triggered on Config::metrics_sample_ms to provide the sampled data based
         *      on the sample period.  After this callback, the period/sample based metrics will reset and start over
         *      for the new period.
         *
         * @param connection_handle           Source connection ID
         * @param metrics                     Copy of the connection metrics for the sample period
         */
        void MetricsSampled(ConnectionHandle connection_handle, const ConnectionMetrics& metrics) override;

        /**
         * @brief Callback on client setup message
         * @details In server mode, client will send a setup message on new connection.
         *         Server responds with server setup.
         *
         * @param connection_handle                    Transport connection ID
         * @param client_setup_attributes              Decoded client setup message
         *
         * @return ClientSetupResponse indicating the status of processing the setup message.
         */
        virtual ClientSetupResponse ClientSetupReceived(ConnectionHandle connection_handle,
                                                        const ClientSetupAttributes& client_setup_attributes) = 0;

        /**
         * @brief Callback notification for new announce received that needs to be authorized
         *
         * @note The caller **MUST** respond to this via ResolveAnnounce(). If the caller does not
         * override this method, the default will call ResolveAnnounce() with the status of OK
         *
         * @param connection_handle             Source connection ID
         * @param track_namespace               Track namespace
         * @param publish_announce_attributes   Publish announce attributes received
         */
        virtual void AnnounceReceived(ConnectionHandle connection_handle,
                                      const TrackNamespace& track_namespace,
                                      const PublishAnnounceAttributes& publish_announce_attributes);

        /**
         * @brief Accept or reject an announce that was received
         *
         * @details Accept or reject an announce received via AnnounceReceived(). The MoQ Transport
         *      will send the protocol message based on the AnnounceResponse. Subscribers
         *      defined will be sent a copy of the announcement
         *
         * @param connection_handle        source connection ID
         * @param track_namespace          track namespace
         * @param subscribers              Vector/list of subscriber connection handles that should be sent the announce
         * @param announce_response        response to for the announcement
         */
        void ResolveAnnounce(ConnectionHandle connection_handle,
                             const TrackNamespace& track_namespace,
                             const std::vector<ConnectionHandle>& subscribers,
                             const AnnounceResponse& announce_response);

        /**
         * @brief Callback notification for unannounce received
         *
         * @details The callback will indicate that a new unannounce has been received. The
         *    app should return a vector of connection handler ids that should receive a
         *    copy of the unannounce. The returned list is based on subscribe announces prefix
         *    matching.
         *
         * @param connection_handle         Source connection ID
         * @param track_namespace           Track namespace
         *
         * @returns vector of subscribe announces connection handler ids matching prefix to the namespace being
         * unannounced.
         */
        virtual std::vector<ConnectionHandle> UnannounceReceived(ConnectionHandle connection_handle,
                                                                 const TrackNamespace& track_namespace) = 0;

        /**
         * @brief Callback notification for Unsubscribe announces received
         *
         * @param connection_handle         Source connection ID
         * @param prefix_namespace           Prefix namespace
         *
         */
        virtual void UnsubscribeAnnouncesReceived(ConnectionHandle connection_handle,
                                                  const TrackNamespace& prefix_namespace) = 0;

        /**
         * @brief Callback notification for new subscribe announces received
         *
         * @note The caller must return the appropriate SubscribeAnnouncesErrorCode on error.
         *    If no error, nullopt is returned for error code and the vector should contain
         *    all the matching track namespaces to the prefix.  Each of the returned namespaces
         *    will be announced to the subscriber.
         *
         * @param connection_handle             Source connection ID
         * @param prefix_namespace               Track namespace
         * @param announce_attributes   Announces attributes received
         */
        using SubscribeAnnouncesResponse =
          std::pair<std::optional<quicr::ctrl_messages::SubscribeAnnouncesErrorCodeEnum>, std::vector<TrackNamespace>>;

        virtual SubscribeAnnouncesResponse SubscribeAnnouncesReceived(
          ConnectionHandle connection_handle,
          const TrackNamespace& prefix_namespace,
          const PublishAnnounceAttributes& announce_attributes);

        /**
         * @brief Callback notification for new subscribe received
         *
         * @note The caller **MUST** respond to this via ResolveSubscribe(). If the caller does not
         * override this method, the default will call ResolveSubscribe() with the status of OK
         *
         * @param connection_handle     Source connection ID
         * @param subscribe_id          Subscribe ID received
         * @param proposed_track_alias  The proposed track alias the subscriber would like to use
         * @param track_full_name       Track full name
         * @param subscribe_attributes  Subscribe attributes received
         */
        virtual void SubscribeReceived(ConnectionHandle connection_handle,
                                       uint64_t subscribe_id,
                                       uint64_t proposed_track_alias,
                                       quicr::ctrl_messages::FilterTypeEnum filter_type,
                                       const FullTrackName& track_full_name,
                                       const quicr::ctrl_messages::SubscribeAttributes& subscribe_attributes);

        /**
         * @brief Callback notification on unsubscribe received
         *
         * @param connection_handle   Source connection ID
         * @param subscribe_id        Subscribe ID received
         */
        virtual void UnsubscribeReceived(ConnectionHandle connection_handle, uint64_t subscribe_id) = 0;

        // TODO: Their is probably a distinction between track not found, and no objects.
        using LargestAvailable = std::optional<std::pair<ctrl_messages::GroupId, ctrl_messages::ObjectId>>;

        /**
         * @brief Get the largest available object for the given track, if any.
         * @param track_name The track to lookup on.
         * @return The largest available object, if any.
         */
        virtual LargestAvailable GetLargestAvailable(const FullTrackName& track_name);

        /**
         * @brief Event to run on sending FetchOk.
         *
         * @param connection_handle Source connection ID.
         * @param subscribe_id      Subscribe ID received.
         * @param track_full_name   Track full name
         * @param attributes        Fetch attributes received.
         *
         * @returns True to indicate fetch will send data, False if no data is within the requested range
         */
        virtual bool OnFetchOk(ConnectionHandle connection_handle,
                               uint64_t subscribe_id,
                               const FullTrackName& track_full_name,
                               const quicr::ctrl_messages::FetchAttributes& attributes);

        /**
         * @brief Callback notification on receiving a FetchCancel message.
         *
         * @param connection_handle Source connection ID.
         * @param subscribe_id      Subscribe ID received.
         */
        virtual void FetchCancelReceived(ConnectionHandle connection_handle, uint64_t subscribe_id) = 0;

        virtual void NewGroupRequested(ConnectionHandle connection_handle, uint64_t subscribe_id, uint64_t track_alias);

        ///@}
        // --END OF CALLBACKS ----------------------------------------------------------------------------------

      private:
        bool ProcessCtrlMessage(ConnectionContext& conn_ctx, BytesSpan msg_bytes) override;
        PublishTrackHandler::PublishObjectStatus SendFetchObject(PublishFetchHandler& track_handler,
                                                                 uint8_t priority,
                                                                 uint32_t ttl,
                                                                 bool stream_header_needed,
                                                                 uint64_t group_id,
                                                                 uint64_t subgroup_id,
                                                                 uint64_t object_id,
                                                                 std::optional<Extensions> extensions,
                                                                 BytesSpan data) const;

        bool stop_{ false };
    };

} // namespace moq
