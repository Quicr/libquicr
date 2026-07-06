#include <quicr/session.h>

#include <future>

namespace quicr_test {
    class TestClient final : public quicr::Session
    {
      public:
        explicit TestClient(const quicr::ClientConfig& cfg,
                            std::shared_ptr<quicr::Transport> transport,
                            std::shared_ptr<quicr::Connection> connection,
                            std::shared_ptr<timeq::tick_service> tick_service);

        // Connection.
        void SetConnectedPromise(std::promise<quicr::ServerSetupAttributes> promise)
        {
            client_connected_ = std::move(promise);
        }
        void ServerSetupReceived(const quicr::ServerSetupAttributes& server_setup_attributes) override;
        // Publish Namespace received.
        void SetPublishNamespaceReceivedPromise(std::promise<quicr::TrackNamespace> promise)
        {
            publish_namespace_received_ = std::move(promise);
        }
        void PublishNamespaceReceived(const quicr::TrackNamespace& track_namespace,
                                      const quicr::PublishNamespaceAttributes& publish_namespace_attributes) override;

        // Publish received.
        void SetPublishReceivedPromise(std::promise<quicr::FullTrackName> promise)
        {
            publish_received_ = std::move(promise);
        }

        std::shared_ptr<quicr::SubscribeTrackHandler> GetLastPublishReceivedSubHandler() const
        {
            return last_publish_received_sub_handler_;
        }

        void PublishReceived(uint64_t request_id,
                             const quicr::PublishAttributes& publish_attributes,
                             std::weak_ptr<quicr::SubscribeNamespaceHandler> ns_handler) override;

      private:
        std::optional<std::promise<quicr::ServerSetupAttributes>> client_connected_;
        std::optional<std::promise<quicr::TrackNamespace>> publish_namespace_received_;
        std::optional<std::promise<quicr::FullTrackName>> publish_received_;
        std::optional<std::promise<std::uint64_t>> publish_namespace_status_changed_;
        std::shared_ptr<quicr::SubscribeTrackHandler> last_publish_received_sub_handler_;
    };
}
