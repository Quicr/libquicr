#include <quicr/client.h>

#include <future>
#include <map>
#include <mutex>
#include <optional>

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

        std::shared_ptr<quicr::SubscribeTrackHandler> GetLastPublishReceivedSubHandler() const
        {
            return last_publish_received_sub_handler_;
        }

        void PublishReceived(std::uint64_t connection_id,
                             uint64_t request_id,
                             const quicr::PublishAttributes& publish_attributes,
                             std::weak_ptr<quicr::SubscribeNamespaceHandler> ns_handler) override;

        /**
         * Check the state of a stream.
         * @param stream_id The stream to query.
         * @return True for closed with RESET, false for cloesd with FIN, nullopt for not closed.
         */
        std::optional<bool> CheckStreamState(std::uint64_t stream_id)
        {
            std::lock_guard _(stream_state_mutex_);
            const auto it = closed_streams_.find(stream_id);
            if (it == closed_streams_.end()) {
                return std::nullopt;
            }
            return it->second;
        }

      protected:
        void OnStreamClosed(const std::uint64_t& connection_id,
                            std::uint64_t stream_id,
                            std::shared_ptr<quicr::StreamRxContext> rx_ctx,
                            std::optional<std::uint64_t> data_ctx_id,
                            quicr::StreamClosedFlag flag) override
        {
            if (flag != quicr::StreamClosedFlag::kStopSending) {
                std::lock_guard lock(stream_state_mutex_);
                closed_streams_[stream_id] = (flag == quicr::StreamClosedFlag::kReset);
            }
            Session::OnStreamClosed(connection_id, stream_id, std::move(rx_ctx), data_ctx_id, flag);
        }

      private:
        std::mutex stream_state_mutex_;
        std::map<std::uint64_t, bool> closed_streams_;
        std::optional<std::promise<quicr::ServerSetupAttributes>> client_connected_;
        std::optional<std::promise<quicr::TrackNamespace>> publish_namespace_received_;
        std::optional<std::promise<quicr::FullTrackName>> publish_received_;
        std::optional<std::promise<std::uint64_t>> publish_namespace_status_changed_;
        std::shared_ptr<quicr::SubscribeTrackHandler> last_publish_received_sub_handler_;
    };
}
