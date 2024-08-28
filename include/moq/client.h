/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <optional>
#include <moq/config.h>
#include <moq/common.h>
#include <moq/track_name.h>
#include <moq/object.h>
#include <moq/detail/transport.h>

namespace moq {
    using namespace qtransport;

    /**
     * @brief MoQ Client
     *
     * @details MoQ Client is the handler of the MoQ QUIC transport IP connection.
     */
    class Client : public Transport
    {
      public:
        /**
         * @brief MoQ Client Constructor to create the client mode instance
         *
         * @param cfg           MoQ Client Configuration
         */
        Client(const ClientConfig& cfg)
          : Transport(cfg)
        {
        }

        ~Client() = default;

        /**
         * @brief Starts a client connection via a transport thread
         *
         * @details Makes a client connection session and runs in a newly created thread. All control and track
         *   callbacks will be run based on events.
         *
         * @return Status indicating state or error. If successful, status will be
         *    kClientConnecting.
         */
        Status Connect();

        /**
         * @brief Disconnect the client connection gracefully
         *
         * @details Unsubscribes and unpublishes all remaining active ones, sends MoQ control messages
         *   for those and then closes the QUIC connection gracefully. Stops the transport thread. The class
         *   destructor calls this method as well. Status will be updated to reflect not connected.
         *
         * @return Status of kDisconnecting
         */
        Status Disconnect();

        /**
         * @brief Callback notification for connection status/state change
         * @details Callback notification indicates state change of connection, such as disconnected
         *
         * @param status           Status change
         */
        virtual void StatusChanged(Status status) {}

         /**
         * @brief Get the status of the Client
         *
         * @return Status of the Client
         */
         Status GetStatus() const noexcept { return status_; }

        /**
         * @brief Callback on server setup message
         *
         * @details Server will send sever setup in response to client setup message sent. This callback is called
         *  when a server setup has been received.
         *
         * @param server_setup_attributes     Server setup attributes received
         */
        virtual void ServerSetupReceived(const ServerSetupAttributes& server_setup_attributes) {}

        /**
         * @brief Notification on publish announcement status change
         *
         * @details Callback notification for a change in publish announcement status
         *
         * @param track_namespace             Track namespace to announce
         * @param status                      Publish announce status
         */
        virtual void AnnounceStatusChanged(const TrackNamespace& track_namespace, const PublishAnnounceStatus status) {}

        /**
         * @brief Callback notification for new subscribe received that doesn't match an existing publish track
         *
         * @details When a new subscribe is received that doesn't match any existing publish track, this
         *      method will be called to signal the client application that there is a new subscribe full
         *      track name. The client app should PublishTrack() within this callback (or afterwards) and return
         *      **true** if the subscribe is accepted and publishing will commence. If the subscribe is rejected
         *      and a publish track will not begin, then **false** should be returned. The Transport
         *      will send the appropriate message to indicate the accept/reject.
         *
         * @note The caller **MUST** respond to this via ResolveSubscribe(). If the caller does not
         *      override this method, the default will call ResolveSubscribe() with the status of error as
         *      not exists.
         *
         * @param track_full_name           Track full name
         * @param subscribe_attributes      Subscribe attributes received
         */
        virtual void UnpublishedSubscribeReceived(const FullTrackName& track_full_name,
                                                  const SubscribeAttributes& subscribe_attributes) {
            // TODO: add the default response
        }

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
                                      SubscribeResponse subscribe_response);


        /**
         * @brief Notification callback to provide sampled metrics
         *
         * @details Callback will be triggered on Config::metrics_sample_ms to provide the sampled data based
         *      on the sample period.  After this callback, the period/sample based metrics will reset and start over
         *      for the new period.
         *
         * @param metrics           Copy of the connection metrics for the sample period
         */
        virtual void MetricsSampled(const ConnectionMetrics&& metrics) {}

        /**
         * @brief Get announce status for namespace
         * @param track_namespace           Track namespace of the announcement
         *
         * @return PublishAnnounceStatus of the namespace
         */
        PublishAnnounceStatus GetAnnounceStatus(const TrackNamespace& track_namespace);

        /**
         * @brief Subscribe to a track
         *
         * @param track_handler    Track handler to use for track related functions and callbacks
         */
        void SubscribeTrack(std::shared_ptr<SubscribeTrackHandler> track_handler) {
            if (connection_handle_) {
                Transport::SubscribeTrack(*connection_handle_, std::move(track_handler));
            }
        }

        /**
         * @brief Unsubscribe track
         *
         * @param track_handler    Track handler to use for track related functions and callbacks
         */
        void UnsubscribeTrack(std::shared_ptr<SubscribeTrackHandler> track_handler) {
            if (connection_handle_) {
                Transport::UnsubscribeTrack(*connection_handle_, std::move(track_handler));
            }
        }

        /**
         * @brief Publish a track namespace
         *
         * @details In MoQ, a publish namespace will result in an announce being sent. Announce OK will
         *      be reflected in the Status() of the PublishTrackHandler passed. This method can be called at any time,
         *      but normally it would be called before publishing any tracks to the same namespace.
         *
         *      If this method is called after a publish track with a matching namespace that already exists or if called
         *      more than once, this will result in this track handler being added to the active state of the
         *      announce, but it will not result in a repeated announce being sent. Adding track handler to
         *      the announce state ensures that the announce will remain active if the other tracks are
         *      removed.
         *
         * @note
         *      The PublishTrackHandler with this method only needs to have the FullTrackName::name_space defined.
         *      Name and track alias is not used.
         *
         *
         * @param track_namespace    Track handler to use for track related functions
         *                           and callbacks
         */
        void PublishAnnounce(const TrackNamespace& track_namespace);

        /**
         * @brief Unannounce a publish namespace
         *
         * @details Unannounce a publish namespace. **ALL** tracks will be marked unpublish, as if called
         *    by UnpublishTrack()
         *
         * @param track_namespace         Track namespace to unannounce
         */
        void PublishUnannounce(const TrackNamespace& track_namespace);

        /**
         * @brief Publish to a track
         *
         * @param track_handler    Track handler to use for track related functions
         *                          and callbacks
         */
        void PublishTrack(std::shared_ptr<PublishTrackHandler> track_handler) {
            if (connection_handle_) {
                Transport::PublishTrack(*connection_handle_, std::move(track_handler));
            }
        }

        /**
         * @brief Unpublish track
         *
         * @details Unpublish a track that was previously published
         *
         * @param track_handler    Track handler used when published track
         */
        void UnpublishTrack(std::shared_ptr<PublishTrackHandler> track_handler) {
            if (connection_handle_) {
                Transport::UnpublishTrack(*connection_handle_, std::move(track_handler));
            }
        }

        virtual bool ProcessCtrlMessage(ConnectionContext& conn_ctx,
                                                  std::shared_ptr<StreamBuffer<uint8_t>>& stream_buffer);

      private:
        void SetStatus(Status status) { status_ = status; StatusChanged(status); }

        std::optional<ConnectionHandle> connection_handle_;               ///< Connection ID for the client
    };

} // namespace moq
