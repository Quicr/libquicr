#include <future>
#include <quicr/client.h>

namespace quicr_test {
    class TestClient final : public quicr::Client
    {
      public:
        explicit TestClient(const quicr::ClientConfig& cfg);

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

        void PublishReceived(quicr::ConnectionHandle connection_handle,
                             uint64_t request_id,
                             const quicr::messages::PublishAttributes& publish_attributes) override;

        // Publish Namespace status changed.
        void SetPublishNamespaceStatusChangedPromise(std::promise<quicr::TrackNamespace> promise)
        {
            publish_namespace_status_changed_ = std::move(promise);
        }
        void PublishNamespaceStatusChanged(const quicr::TrackNamespace& track_namespace,
                                           const quicr::PublishNamespaceStatus status) override;

      private:
        std::optional<std::promise<quicr::ServerSetupAttributes>> client_connected_;
        std::optional<std::promise<quicr::TrackNamespace>> publish_namespace_received_;
        std::optional<std::promise<quicr::FullTrackName>> publish_received_;
        std::optional<std::promise<quicr::TrackNamespace>> publish_namespace_status_changed_;
    };
}
