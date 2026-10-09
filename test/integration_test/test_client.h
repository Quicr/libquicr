#include <quicr/config.h>
#include <quicr/session_callbacks.h>

#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

namespace quicr {
    class Session;
    class SubscribeTrackHandler;
}

namespace quicr_test {
    class TestClient final : public quicr::Session::ClientCallbacks
    {
      public:
        // Connection.
        void SetConnectedPromise(std::promise<quicr::ServerSetupAttributes> promise)
        {
            client_connected_ = std::move(promise);
        }

        std::optional<quicr::Session::Status> GetStatusAtServerSetup() const
        {
            std::lock_guard lock(status_mutex_);
            return status_at_server_setup_;
        }

        quicr::Expected<void, quicr::Error<quicr::ErrorCode>> ServerSetupReceived(
          const std::shared_ptr<quicr::Session>& session,
          const quicr::ServerSetupAttributes& server_setup_attributes) override;

        // Publish Namespace received.
        void SetPublishNamespaceReceivedPromise(std::promise<quicr::TrackNamespace> promise)
        {
            publish_namespace_received_ = std::move(promise);
        }

        quicr::Reply<void, quicr::PublishNamespaceErrorCode> PublishNamespaceReceived(
          const std::shared_ptr<quicr::Session>& session,
          const quicr::TrackNamespace& track_namespace,
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

        quicr::Reply<const quicr::PublishResponse, quicr::PublishErrorCode> PublishReceived(
          const std::shared_ptr<quicr::Session>& session,
          uint64_t request_id,
          const quicr::PublishAttributes& publish_attributes,
          std::weak_ptr<quicr::SubscribeNamespaceHandler> ns_handler) override;

        /**
         * Check the state of a stream.
         * @param stream_id The stream to query.
         * @return Stream closure, or std::nullopt if not closed.
         */
        std::optional<std::set<quicr::StreamClosedFlag>> CheckStreamState(std::uint64_t stream_id)
        {
            std::lock_guard _(stream_state_mutex_);
            const auto it = closed_streams_.find(stream_id);
            if (it == closed_streams_.end()) {
                return std::nullopt;
            }
            return it->second;
        }

        std::map<std::uint64_t, std::set<quicr::StreamClosedFlag>> GetStreamClosures()
        {
            std::lock_guard lock(stream_state_mutex_);
            return closed_streams_;
        }

        std::vector<std::uint64_t> GetClosedStreamIds()
        {
            std::lock_guard _(stream_state_mutex_);
            std::vector<std::uint64_t> stream_ids;
            for (const auto& [stream_id, reset] : closed_streams_) {
                stream_ids.push_back(stream_id);
            }
            return stream_ids;
        }

      protected:
        void OnStreamClosed(std::uint64_t stream_id, quicr::StreamClosedFlag flag) override
        {
            std::lock_guard lock(stream_state_mutex_);
            if (flag != quicr::StreamClosedFlag::kStopSending && closed_streams_.contains(stream_id) &&
                closed_streams_[stream_id].size() > 1) {
                throw std::logic_error("Can't have more than one close type");
            }
            closed_streams_[stream_id].insert(flag);
        }

      private:
        mutable std::mutex status_mutex_;
        std::optional<quicr::Session::Status> status_at_server_setup_;
        std::mutex stream_state_mutex_;
        std::map<std::uint64_t, std::set<quicr::StreamClosedFlag>> closed_streams_;
        std::optional<std::promise<quicr::ServerSetupAttributes>> client_connected_;
        std::optional<std::promise<quicr::TrackNamespace>> publish_namespace_received_;
        std::optional<std::promise<quicr::FullTrackName>> publish_received_;
        std::optional<std::promise<std::uint64_t>> publish_namespace_status_changed_;
        std::shared_ptr<quicr::SubscribeTrackHandler> last_publish_received_sub_handler_;
    };
}
