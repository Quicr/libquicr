#include <quicr/client.h>

#include <future>

namespace quicr_test {
    class TestClient final : public quicr::Client
    {
      public:
        struct TrackStatusResult
        {
            std::uint64_t request_id;
            quicr::TrackStatusResponse response;
        };

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

        std::shared_ptr<quicr::SubscribeTrackHandler> GetLastPublishReceivedSubHandler() const
        {
            return last_publish_received_sub_handler_;
        }

        void PublishReceived(std::uint64_t connection_id,
                             uint64_t request_id,
                             const quicr::PublishAttributes& publish_attributes,
                             std::weak_ptr<quicr::SubscribeNamespaceHandler> ns_handler) override;

        void SetTrackStatusResponsePromise(std::promise<TrackStatusResult> promise)
        {
            track_status_response_ = std::move(promise);
        }

        void TrackStatusResponseReceived(std::uint64_t connection_id,
                                         std::uint64_t request_id,
                                         const quicr::TrackStatusResponse& response) override;

        void SetRequestStreamClosedPromise(std::promise<bool> promise) { request_stream_closed_ = std::move(promise); }

      protected:
        void OnStreamClosed(const std::uint64_t& connection_id,
                            std::uint64_t stream_id,
                            std::shared_ptr<quicr::StreamRxContext> rx_ctx,
                            std::optional<std::uint64_t> data_ctx_id,
                            quicr::StreamClosedFlag flag) override;

      private:
        std::optional<std::promise<quicr::ServerSetupAttributes>> client_connected_;
        std::optional<std::promise<quicr::TrackNamespace>> publish_namespace_received_;
        std::optional<std::promise<quicr::FullTrackName>> publish_received_;
        std::optional<std::promise<std::uint64_t>> publish_namespace_status_changed_;
        std::optional<std::promise<TrackStatusResult>> track_status_response_;
        std::optional<std::promise<bool>> request_stream_closed_;
        std::shared_ptr<quicr::SubscribeTrackHandler> last_publish_received_sub_handler_;
    };
}
