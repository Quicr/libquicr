#pragma once

#include "detail/transport.h"
#include "quicr/config.h"
#include "quicr/detail/attributes.h"
#include "quicr/detail/messages.h"
#include "quicr/detail/transport.h"
#include "quicr/object.h"
#include "quicr/publish_fetch_handler.h"
#include "quicr/track_name.h"

namespace quicr {
    class Session : public Transport
    {
      protected:
        /**
         * @brief Response to received MOQT Announce message
         */
        struct PublishNamespaceResponse
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

            std::optional<messages::ReasonPhrase> error_reason;
        };

        Session(const ClientConfig& cfg)
          : Transport(cfg, std::make_shared<ThreadedTickService>(cfg.tick_service_sleep_delay_us))
        {
        }

        Session(const ServerConfig& cfg)
          : Transport(cfg, std::make_shared<ThreadedTickService>(cfg.tick_service_sleep_delay_us))
        {
        }

      public:
        ~Session() = default;

        static std::shared_ptr<Session> Create(const ClientConfig& cfg)
        {
            return std::shared_ptr<Session>(new Session(cfg));
        }

        static std::shared_ptr<Session> Create(const ServerConfig& cfg)
        {
            return std::shared_ptr<Session>(new Session(cfg));
        }

        Status Connect();
        Status Disconnect();
        Status Start();
        void Stop();

        virtual void NewGroupRequested(const FullTrackName& track_full_name, messages::GroupId group_id);
        virtual void PublishNamespaceReceived(ConnectionHandle connection_handle,
                                              const TrackNamespace& track_namespace,
                                              const PublishNamespaceAttributes& publish_announce_attributes);
        virtual void PublishNamespaceStatusChanged(messages::RequestID request_id, const PublishNamespaceStatus status);
        virtual void ResolveFetch(ConnectionHandle connection_handle,
                                  uint64_t request_id,
                                  std::uint8_t priority,
                                  std::optional<messages::GroupOrder> group_order,
                                  const FetchResponse& response);
        virtual void ResolveSubscribe(ConnectionHandle connection_handle,
                                      uint64_t request_id,
                                      uint64_t track_alias,
                                      const RequestResponse& subscribe_response);
        virtual void ResolveSubscribeNamespace(ConnectionHandle connection_handle,
                                               DataContextId data_ctx_id,
                                               uint64_t request_id,
                                               const TrackNamespace& prefix,
                                               const SubscribeNamespaceResponse& response);
        virtual void ServerSetupReceived(const ServerSetupAttributes& server_setup_attributes);
        virtual void SubscribeReceived(ConnectionHandle connection_handle,
                                       uint64_t request_id,
                                       const FullTrackName& track_full_name,
                                       const messages::SubscribeAttributes& subscribe_attributes);

        virtual void ClientSetupReceived(ConnectionHandle connection_handle,
                                         const ClientSetupAttributes& client_setup_attributes);
        virtual std::vector<ConnectionHandle> PublishNamespaceDoneReceived(ConnectionHandle connection_handle,
                                                                           messages::RequestID request_id);
        virtual void PublishDoneReceived(ConnectionHandle connection_handle, uint64_t request_id);
        virtual void SubscribeNamespaceReceived(ConnectionHandle connection_handle,
                                                DataContextId data_ctx_id,
                                                const TrackNamespace& prefix_namespace,
                                                const messages::SubscribeNamespaceAttributes& attributes);
        virtual void UnpublishedSubscribeReceived(const FullTrackName& track_full_name,
                                                  const messages::SubscribeAttributes& subscribe_attributes);
        virtual void UnsubscribeNamespaceReceived(ConnectionHandle connection_handle,
                                                  const TrackNamespace& prefix_namespace);
        virtual void UnsubscribeReceived(ConnectionHandle connection_handle, uint64_t request_id);

        void ConnectionStatusChanged(ConnectionHandle connection_handle, ConnectionStatus status) override;
        void FetchCancelReceived(ConnectionHandle connection_handle, uint64_t request_id) override;
        void JoiningFetchReceived(ConnectionHandle connection_handle,
                                  uint64_t request_id,
                                  const FullTrackName& track_full_name,
                                  const quicr::messages::JoiningFetchAttributes& attributes) override;
        void MetricsSampled(ConnectionHandle connection_handle, const ConnectionMetrics& metrics) override;
        void MetricsSampled(const ConnectionMetrics& metrics) override;
        void NewConnectionAccepted(ConnectionHandle connection_handle, const ConnectionRemoteInfo& remote) override;
        void PublishReceived(ConnectionHandle connection_handle,
                             uint64_t request_id,
                             const messages::PublishAttributes& publish_attributes,
                             std::weak_ptr<SubscribeNamespaceHandler> sub_ns_handler) override;
        void RequestUpdateReceived(ConnectionHandle connection_handle,
                                   uint64_t request_id,
                                   uint64_t existing_request_id,
                                   const messages::Parameters& params) override;
        void ResolveRequestUpdate(ConnectionHandle connection_handle,
                                  uint64_t request_id,
                                  uint64_t existing_request_id,
                                  const messages::Parameters& params) override;
        void StandaloneFetchReceived(ConnectionHandle connection_handle,
                                     uint64_t request_id,
                                     const FullTrackName& track_full_name,
                                     const quicr::messages::StandaloneFetchAttributes& attributes) override;

        std::optional<ConnectionHandle> GetConnectionHandle() const noexcept { return connection_handle_; }
        PublishNamespaceStatus GetPublishNamespaceStatus(const TrackNamespace& track_namespace);
        void BindFetchTrack(TransportConnId conn_id, std::shared_ptr<PublishFetchHandler> track_handler);
        void BindPublisherTrack(ConnectionHandle connection_handle,
                                ConnectionHandle src_id,
                                uint64_t request_id,
                                const std::shared_ptr<PublishTrackHandler>& track_handler,
                                bool ephemeral = false);
        void PublishNamespaceDone(const std::shared_ptr<PublishNamespaceHandler>& handler);
        void ResolvePublishNamespace(ConnectionHandle connection_handle,
                                     uint64_t request_id,
                                     const TrackNamespace& track_namespace,
                                     const std::vector<ConnectionHandle>& subscribers,
                                     const PublishNamespaceResponse& announce_response);
        void ResolvePublishNamespaceDone(ConnectionHandle connection_handle,
                                         messages::RequestID request_id,
                                         const std::vector<ConnectionHandle>& subscribers);
        void SubscribeNamespace(std::shared_ptr<SubscribeNamespaceHandler> handler);
        void UnbindFetchTrack(ConnectionHandle conn_id, const std::shared_ptr<PublishFetchHandler>& track_handler);
        void UnbindPublisherTrack(ConnectionHandle connection_handle,
                                  ConnectionHandle src_id,
                                  const std::shared_ptr<PublishTrackHandler>& track_handler,
                                  bool send_publish_done = false);
        void UnsubscribeNamespace(const std::shared_ptr<SubscribeNamespaceHandler>& handler);

        void SubscribeTrack(std::shared_ptr<SubscribeTrackHandler> track_handler)
        {
            if (connection_handle_) {
                Transport::SubscribeTrack(*connection_handle_, std::move(track_handler));
            }
        }

        virtual uint64_t RequestTrackStatus(const FullTrackName& track_full_name,
                                            const messages::SubscribeAttributes& subscribe_attributes = {})
        {
            if (connection_handle_) {
                return Transport::RequestTrackStatus(*connection_handle_, track_full_name, subscribe_attributes);
            }
            return 0;
        }

        void UnsubscribeTrack(std::shared_ptr<SubscribeTrackHandler> track_handler)
        {
            if (connection_handle_) {
                Transport::UnsubscribeTrack(*connection_handle_, std::move(track_handler));
            }
        }

        void PublishTrack(std::shared_ptr<PublishTrackHandler> track_handler)
        {
            if (connection_handle_) {
                Transport::PublishTrack(*connection_handle_, std::move(track_handler));
            }
        }

        void UnpublishTrack(std::shared_ptr<PublishTrackHandler> track_handler)
        {
            if (connection_handle_) {
                Transport::UnpublishTrack(*connection_handle_, std::move(track_handler));
            }
        }

        void FetchTrack(std::shared_ptr<FetchTrackHandler> track_handler)
        {
            if (connection_handle_.has_value()) {
                Transport::FetchTrack(connection_handle_.value(), std::move(track_handler));
            }
        }

        void CancelFetchTrack(std::shared_ptr<FetchTrackHandler> track_handler)
        {
            if (connection_handle_.has_value()) {
                Transport::CancelFetchTrack(connection_handle_.value(), std::move(track_handler));
            }
        }

      private:
        bool ProcessCtrlMessage(ConnectionContext& conn_ctx,
                                uint64_t data_ctx_id,
                                messages::ControlMessageType msg_type,
                                BytesSpan msg_bytes) override;

        void SetConnectionHandle(ConnectionHandle connection_handle) override
        {
            connection_handle_ = connection_handle;
        }

        void SetStatus(Status status)
        {
            status_ = status;
            StatusChanged(status);
        }

      private:
        std::optional<ConnectionHandle> connection_handle_; ///< Connection ID for the client
    };
}
