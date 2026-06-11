#include <quicr/client.h>

#include <future>

namespace quicr_test {
    class TestClient final : public quicr::Client
    {
      public:
        explicit TestClient(const quicr::ClientConfig& cfg);

        // Connection.
        void SetConnectedPromise(std::promise<quicr::messages::ServerSetupAttributes> promise)
        {
            client_connected_ = std::move(promise);
        }
        void ServerSetupReceived(const quicr::messages::ServerSetupAttributes& server_setup_attributes) override;
        // Publish Namespace received.
        void SetPublishNamespaceReceivedPromise(std::promise<quicr::TrackNamespace> promise)
        {
            publish_namespace_received_ = std::move(promise);
        }
        void PublishNamespaceReceived(
          const quicr::TrackNamespace& track_namespace,
          const quicr::messages::PublishNamespaceAttributes& publish_namespace_attributes) override;

        // Publish received.
        void SetPublishReceivedPromise(std::promise<quicr::FullTrackName> promise)
        {
            publish_received_ = std::move(promise);
        }

        std::shared_ptr<quicr::SubscribeTrackHandler> GetLastPublishReceivedSubHandler() const
        {
            return last_publish_received_sub_handler_;
        }

        void PublishReceived(std::uint64_t connection_handle,
                             uint64_t request_id,
                             const quicr::messages::PublishAttributes& publish_attributes,
                             std::weak_ptr<quicr::SubscribeNamespaceHandler> ns_handler) override;

        // Publish Namespace status changed.
        void SetPublishNamespaceStatusChangedPromise(std::promise<std::uint64_t> promise)
        {
            publish_namespace_status_changed_ = std::move(promise);
        }
        void PublishNamespaceStatusChanged(std::uint64_t request_id,
                                           const quicr::PublishNamespaceStatus status) override;

      private:
        std::optional<std::promise<quicr::messages::ServerSetupAttributes>> client_connected_;
        std::optional<std::promise<quicr::TrackNamespace>> publish_namespace_received_;
        std::optional<std::promise<quicr::FullTrackName>> publish_received_;
        std::optional<std::promise<std::uint64_t>> publish_namespace_status_changed_;
        std::shared_ptr<quicr::SubscribeTrackHandler> last_publish_received_sub_handler_;
    };
}
