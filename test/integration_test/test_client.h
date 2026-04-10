#include <future>
#include <quicr/client.h>
#include <quicr/detail/attributes.h>

namespace quicr_test {
    class TestClient final : public quicr::Client
    {
      public:
        explicit TestClient(const quicr::ClientConfig& cfg);

        struct JoiningFetchDetails
        {
            quicr::ConnectionHandle connection_handle;
            uint64_t request_id;
            quicr::FullTrackName track_full_name;
            quicr::messages::JoiningFetchAttributes attributes;
        };

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

        void PublishReceived(quicr::ConnectionHandle connection_handle,
                             uint64_t request_id,
                             const quicr::messages::PublishAttributes& publish_attributes,
                             std::weak_ptr<quicr::SubscribeNamespaceHandler> ns_handler) override;

        // Publish Namespace status changed.
        void SetPublishNamespaceStatusChangedPromise(std::promise<quicr::messages::RequestID> promise)
        {
            publish_namespace_status_changed_ = std::move(promise);
        }
        void PublishNamespaceStatusChanged(quicr::messages::RequestID request_id,
                                           const quicr::PublishNamespaceStatus status) override;

        // Joining fetch received.
        void SetJoiningFetchPromise(std::promise<JoiningFetchDetails> promise)
        {
            joining_fetch_received_ = std::move(promise);
        }
        void JoiningFetchReceived(quicr::ConnectionHandle connection_handle,
                                  uint64_t request_id,
                                  const quicr::FullTrackName& track_full_name,
                                  const quicr::messages::JoiningFetchAttributes& attributes) override;

      private:
        std::optional<std::promise<quicr::ServerSetupAttributes>> client_connected_;
        std::optional<std::promise<quicr::TrackNamespace>> publish_namespace_received_;
        std::optional<std::promise<quicr::FullTrackName>> publish_received_;
        std::optional<std::promise<quicr::messages::RequestID>> publish_namespace_status_changed_;
        std::shared_ptr<quicr::SubscribeTrackHandler> last_publish_received_sub_handler_;
        std::optional<std::promise<JoiningFetchDetails>> joining_fetch_received_;
    };
}
