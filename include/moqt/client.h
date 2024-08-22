/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <optional>
#include <moqt/config.h>
#include <moqt/common.h>
#include <moqt/core/transport.h>

namespace moq::transport {
    using namespace qtransport;

    /**
     * @brief MoQT Client
     *
     * @details MoQT Client is the handler of the MoQT QUIC transport IP connection.
     */
    class Client : public Transport
    {
      public:
        /**
         * @brief MoQ Client Constructor to create the client mode instance
         *
         * @param cfg           MoQT Client Configuration
         */
        Client(const ClientConfig& cfg)
          : Transport(cfg)
        {
        }

        ~Client() = default;

        /**
         * @brief Starts a client connection via a transport thread (non-blocking)
         *
         * @details Makes a client connection session and runs in a newly created thread. All control and track
         *   callbacks will be run based on events.
         *
         * @return Status indicating state or error. If successful, status will be
         *    kClientConnecting.
         */
        Status Connect();

        /**
         * @brief Disconnect the client connection gracefully (blocking)
         *
         * @details Unsubscribes and unpublishes all remaining active ones, sends MoQT control messages
         *   for those and then closes the QUIC connection gracefully. Stops the transport thread. The class
         *   destructor calls this method as well. Status will be updated to reflect not connected.
         */
        void Disconnect();

        /**
         * @brief Callback notification for connection status/state change
         * @details Callback notification indicates state change of connection, such as disconnected
         *
         * @param status           Transport status of connection id
         */
        virtual void ConnectionChanged(TransportStatus status) = 0;

        /**
         * @brief Callback on server setup message
         * @details Server will send sever setup in response to client setup message sent. This callback is called
         *  when a server setup has been received.
         *
         * @param server_setup_attributes     Server setup attributes received
         */
        virtual void ServerSetup(const ServerSetupAttributes& server_setup_attributes) = 0;

        /**
         * @brief Callback notification for new subscribe received that doesn't match an existing publish track
         *
         * @details When a new subscribe is received that doesn't match any existing publish track, this
         *      method will be called to signal the client application that there is a new subscribe full
         *      track name. The client app should PublishTrack() within this callback (or afterwards) and return
         *      **true** if the subscribe is accepted and publishing will commence. If the subscribe is rejected
         *      and a publish track will not begin, then **false** should be returned. The MOQT protocol
         *      will send the appropriate message to indicate the accept/reject.
         *
         * @param track_full_name           Track full name
         * @param subscribe_attributes      Subscribe attributes received
         *
         * @return Caller returns **true** to accept the subscribe and will start to publish, **false** to reject
         *      the subscribe and will not publish.
         */
        virtual bool SubscribeReceived(const FullTrackName& track_full_name,
                                       const SubscribeAttributes& subscribe_attributes) = 0;

        /**
         * @brief Notification callback to provide sampled metrics
         *
         * @details Callback will be triggered on Config::metrics_sample_ms to provide the sampled data based
         *      on the sample period.  After this callback, the period/sample based metrics will reset and start over
         *      for the new period.
         *
         * @param metrics           Copy of the connection metrics for the sample period
         */
        virtual void MetricsSampled(const ConnectionMetrics&& metrics) = 0;

      protected:
        /**
         * @brief Subscribe to a track
         *
         * @param track_delegate    Track delegate to use for track related functions and callbacks
         *
         * @returns `track_alias` if no error and nullopt on error
         */
        std::optional<uint64_t> SubscribeTrack(std::shared_ptr<SubscribeTrackHandler> track_delegate) {
            if (conn_id_) {
                return Transport::SubscribeTrack(*conn_id_, std::move(track_delegate));
            } else {
                return std::nullopt;
            }
        }

        /**
         * @brief Unsubscribe track
         *
         * @param track_delegate    Track delegate to use for track related functions and callbacks
         */
        void UnsubscribeTrack(std::shared_ptr<SubscribeTrackHandler> track_delegate) {
            if (conn_id_) {
                Transport::UnsubscribeTrack(*conn_id_, std::move(track_delegate));
            }
        }

        /**
         * @brief Publish a track namespace
         *
         * @details In MoQT, a publish namespace will result in an announce being sent. Announce OK will
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
         * @param track_delegate    Track delegate to use for track related functions
         *                          and callbacks
         */
        void PublishTrackNamespace(std::shared_ptr<PublishTrackHandler> track_delegate) {
            if (conn_id_) {
                Transport::PublishTrack(*conn_id_, std::move(track_delegate));
            }
        }

        /**
         * @brief Publish to a track
         *
         * @param track_delegate    Track delegate to use for track related functions
         *                          and callbacks
         *
         * @returns `track_alias` if no error and nullopt on error
         */
        std::optional<uint64_t> PublishTrack(std::shared_ptr<PublishTrackHandler> track_delegate) {
            if (conn_id_) {
                return Transport::PublishTrack(*conn_id_, std::move(track_delegate));
            } else {
                return std::nullopt;
            }
        }

        /**
         * @brief Unpublish track
         *
         * @param track_delegate    Track delegate used when published track
         */
        void UnpublishTrack(std::shared_ptr<PublishTrackHandler> track_delegate) {
            if (conn_id_) {
                Transport::UnpublishTrack(*conn_id_, std::move(track_delegate));
            }
        }

      private:
        std::optional<TransportConnId> conn_id_;               ///< Connection ID for the client
    };

} // namespace moq::transport
