#pragma once

#include "quicr/handlers/publish_namespace_handler.h"
#include "quicr/handlers/publish_track_handler.h"
#include "quicr/handlers/subscribe_track_handler.h"
#include "quicr/session_callbacks.h"

#include <spdlog/spdlog.h>

#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace quicr_test {
    class TestServer;

    /**
     * @brief Subscribe track handler for receiving objects from publishers
     */
    class TestSubscribeTrackHandler : public quicr::SubscribeTrackHandler
    {
      public:
        TestSubscribeTrackHandler(const quicr::FullTrackName& full_track_name, bool is_publisher_initiated = false)
          : SubscribeTrackHandler(full_track_name,
                                  3,
                                  quicr::messages::GroupOrder::kAscending,
                                  std::monostate{},
                                  std::nullopt,
                                  is_publisher_initiated)
        {
        }

        void SetPublishHandler(std::shared_ptr<quicr::PublishTrackHandler> pub_handler)
        {
            std::lock_guard lock(mutex_);
            pub_handler_ = pub_handler;
        }

        void ObjectReceived(const quicr::ObjectHeaders& object_headers,
                            quicr::BytesSpan data,
                            std::optional<quicr::messages::StreamHeaderProperties> stream_mode = std::nullopt) override
        {
            std::lock_guard lock(mutex_);
            // Forward to subscriber if we have a publish handler bound
            SPDLOG_TRACE("Received conn_id: {} object group: {} subgroup: {} object: {} size: {}",
                         GetConnectionId(),
                         object_headers.group_id,
                         object_headers.subgroup_id,
                         object_headers.object_id,
                         data.size());
            if (pub_handler_) {
                pub_handler_->PublishObject(object_headers, data, stream_mode);
            }
        }

        void StatusChanged([[maybe_unused]] Status status) override {}

        void StreamClosed(std::uint64_t stream_id, [[maybe_unused]] bool reset) override
        {
            auto it = streams_.find(stream_id);
            if (it != streams_.end()) {
                SPDLOG_TRACE("Stream closed by {} stream_id: {} group: {} subgroup: {}",
                             reset ? "RESET" : "FIN",
                             stream_id,
                             it->second.current_group_id,
                             it->second.current_subgroup_id);

                quicr::ObjectHeaders object_headers;
                object_headers.group_id = it->second.current_group_id;
                object_headers.subgroup_id = it->second.current_subgroup_id;
                object_headers.payload_length = 0;
                object_headers.ttl = 5000; // TODO: Revisit TTL for end of subgroup/stream
                object_headers.object_id =
                  it->second.next_object_id.has_value() ? it->second.next_object_id.value() : 1;

                if (pub_handler_) {
                    pub_handler_->EndSubgroup(object_headers.group_id, object_headers.subgroup_id);
                }

                streams_.erase(it);
            }
        }

      private:
        mutable std::mutex mutex_;
        std::shared_ptr<quicr::PublishTrackHandler> pub_handler_;
    };

    /**
     * @brief Publish track handler for sending objects to subscribers
     */
    class TestPublishTrackHandler : public quicr::PublishTrackHandler
    {
      public:
        TestPublishTrackHandler(const quicr::FullTrackName& full_track_name,
                                quicr::TrackMode track_mode,
                                uint8_t default_priority,
                                uint32_t default_ttl,
                                const std::weak_ptr<TestServer> server = {})
          : quicr::PublishTrackHandler(full_track_name, track_mode, default_priority, default_ttl)
          , server_(server)
        {
        }

        void StatusChanged(Status status) override;

      private:
        std::weak_ptr<TestServer> server_;
    };

    class TestServer final
      : public quicr::Session::ServerCallbacks
      , public std::enable_shared_from_this<TestServer>
    {
      public:
        struct AvailableTrack
        {
            quicr::FullTrackName full_track_name;
            quicr::messages::Location start_location;
            quicr::PublishAttributes attributes;
        };

        /**
         * @brief State that must be shared across all per-connection TestServer instances
         *        belonging to the same logical relay/server.
         *
         * @details Since the session rework, a new quicr::Session (and therefore a new
         * TestServer) is created per accepted connection. Relaying data between two
         * different client connections (e.g. a publisher on one connection and a
         * subscriber on another) requires bridging state that outlives any single
         * connection's TestServer instance. This struct holds that bridging state so it
         * can be shared (via shared_ptr) across every TestServer created for the same
         * listening transport.
         */
        struct SharedState
        {
            mutable std::mutex mutex;

            std::vector<quicr::TrackNamespace> known_published_namespaces;
            std::vector<AvailableTrack> known_published_tracks;

            std::unordered_map<quicr::TrackNamespace, std::shared_ptr<quicr::PublishNamespaceHandler>>
              namespace_subscribers;

            // Subscriber publish handlers: [track_alias] -> PublishTrackHandler
            std::map<std::uint64_t, std::shared_ptr<TestPublishTrackHandler>> subscribes;

            // Publisher subscribe handlers: [track_alias] -> SubscribeTrackHandler
            std::map<std::uint64_t, std::shared_ptr<TestSubscribeTrackHandler>> pub_subscribes;
        };

        explicit TestServer(std::shared_ptr<SharedState> shared_state = nullptr);

        struct SubscribeDetails
        {
            uint64_t request_id;
            quicr::FullTrackName track_full_name;
            quicr::SubscribeAttributes subscribe_attributes;
        };

        struct SubscribeNamespaceDetails
        {
            std::uint64_t data_ctx_id{ 0 };
            quicr::TrackNamespace prefix_namespace;
            quicr::SubscribeNamespaceAttributes attributes;
        };

        struct PublishNamespaceDetails
        {
            quicr::TrackNamespace track_namespace;
            quicr::PublishNamespaceAttributes attributes;
        };

        struct UnsubscribeReceivedDetails
        {
            enum class HandlerType
            {
                kSubscribeTrack,
                kPublishTrack,
            };

            uint64_t request_id;
            HandlerType handler_type;
        };

        // Data to respond with when a fetch is received
        struct FetchResponseData
        {
            quicr::ObjectHeaders headers{};
            std::vector<uint8_t> payload;
        };

        struct JoiningFetchDetails
        {
            std::uint64_t connection_id;
            std::uint64_t request_id;
            quicr::FullTrackName track_full_name;
            quicr::JoiningFetchAttributes attributes;
        };

        // Set up promise for subscription event
        void SetSubscribePromise(std::promise<SubscribeDetails> promise) { subscribe_promise_ = std::move(promise); }

        // Set up promise for subscribe namespace event
        void SetSubscribeNamespacePromise(std::promise<SubscribeNamespaceDetails> promise)
        {
            subscribe_namespace_promise_ = std::move(promise);
        }

        void SetPublishAcceptedPromise(std::promise<SubscribeDetails> promise)
        {
            publish_accepted_promise_ = std::move(promise);
        }

        // Set up promise for publish namespace event
        void SetPublishNamespacePromise(std::promise<PublishNamespaceDetails> promise)
        {
            publish_namespace_promise_ = std::move(promise);
        }

        // Unsubscribe received via PublishTrackHandler::StatusChanged.
        void SetUnsubscribePromise(std::promise<uint64_t> promise) { unsubscribe_promise_ = std::move(promise); }

        // UnsubscribeReceived(const std::shared_ptr<quicr::Session>& session,) server callback from
        // CloseRequestHandler().
        void SetUnsubscribeReceivedPromise(std::promise<UnsubscribeReceivedDetails> promise)
        {
            unsubscribe_received_promise_ = std::move(promise);
        }

        void SetExpectedUnsubscribeHandlerType(UnsubscribeReceivedDetails::HandlerType handler_type)
        {
            expected_unsubscribe_handler_type_ = handler_type;
        }

        // PublishNamespaceDone received.
        void SetPublishNamespaceDonePromise(std::promise<uint64_t> promise)
        {
            publish_namespace_done_promise_ = std::move(promise);
        }

        // True = reset, false = FIN, nullopt = not closed.
        std::optional<bool> WasStreamReset(std::uint64_t stream_id) const
        {
            std::lock_guard lock(state_mutex_);
            const auto it = closed_streams_.find(stream_id);
            if (it == closed_streams_.end()) {
                return std::nullopt;
            }
            return it->second;
        }

        // Set up data to respond with when a fetch is received
        void SetFetchResponseData(std::vector<FetchResponseData> data) { fetch_response_data_ = std::move(data); }

        void SetJoiningFetchPromise(std::promise<JoiningFetchDetails> promise)
        {
            joining_fetch_promise_ = std::move(promise);
        }

        void AddKnownPublishedNamespace(const quicr::TrackNamespace& track_namespace);
        void AddKnownPublishedTrack(const quicr::FullTrackName& track,

                                    const std::optional<quicr::messages::Location>& largest_location,
                                    const quicr::PublishAttributes& attributes);

      protected:
        void OnStreamClosed(std::uint64_t stream_id, quicr::StreamClosedFlag flag) override
        {
            std::lock_guard lock(state_mutex_);
            closed_streams_[stream_id] = (flag == quicr::StreamClosedFlag::kReset);
        }

        quicr::Expected<std::vector<std::uint64_t>, quicr::Error<quicr::PublishNamespaceErrorCode>>
        PublishNamespaceDoneReceived(const std::shared_ptr<quicr::Session>& session, std::uint64_t request_id) override
        {
            std::lock_guard lock(state_mutex_);
            if (publish_namespace_done_promise_.has_value()) {
                publish_namespace_done_promise_->set_value(request_id);
                publish_namespace_done_promise_.reset();
            }
            return std::vector<std::uint64_t>{};
        }

        quicr::Expected<void, quicr::Error<int>> UnsubscribeNamespaceReceived(
          const std::shared_ptr<quicr::Session>& session,
          [[maybe_unused]] const quicr::TrackNamespace& prefix_namespace) override {};

        quicr::Expected<void, quicr::Error<quicr::FetchErrorCode>> FetchCancelReceived(
          const std::shared_ptr<quicr::Session>& session,
          [[maybe_unused]] std::uint64_t request_id) override
        {
        }

        quicr::Expected<const quicr::PublishResponse, quicr::Error<quicr::PublishErrorCode>> PublishReceived(
          const std::shared_ptr<quicr::Session>& session,
          std::uint64_t request_id,
          const quicr::PublishAttributes& publish_attributes,
          std::weak_ptr<quicr::SubscribeNamespaceHandler> ns_handler) override;

        quicr::Expected<const quicr::FetchResponse, quicr::Error<quicr::FetchErrorCode>> StandaloneFetchReceived(
          const std::shared_ptr<quicr::Session>& session,
          uint64_t request_id,
          const quicr::FullTrackName& track_full_name,
          const quicr::StandaloneFetchAttributes& attrs) override;

        quicr::Expected<const quicr::FetchResponse, quicr::Error<quicr::FetchErrorCode>> JoiningFetchReceived(
          const std::shared_ptr<quicr::Session>& session,
          uint64_t request_id,
          const quicr::FullTrackName& track_full_name,
          const quicr::JoiningFetchAttributes& attrs) override;

        quicr::Expected<quicr::RequestResponse, quicr::Error<quicr::RequestErrorCode>> SubscribeReceived(
          const std::shared_ptr<quicr::Session>& session,
          uint64_t request_id,
          const quicr::FullTrackName& track_full_name,
          const quicr::SubscribeAttributes& subscribe_attributes) override;

        quicr::Expected<void, quicr::Error<int>> PublishDoneReceived(const std::shared_ptr<quicr::Session>& session,
                                                                     uint64_t request_id) override;

        quicr::Expected<std::vector<quicr::TrackNamespace>, quicr::Error<quicr::RequestErrorCode>>
        SubscribeTracksReceived(const std::shared_ptr<quicr::Session>& session,
                                std::uint64_t data_ctx_id,
                                const quicr::TrackNamespace& prefix_namespace,
                                const quicr::SubscribeNamespaceAttributes& attributes) override;

        quicr::Expected<std::vector<quicr::TrackNamespace>, quicr::Error<quicr::RequestErrorCode>>
        SubscribeNamespaceReceived(const std::shared_ptr<quicr::Session>& session,
                                   std::uint64_t data_ctx_id,
                                   const quicr::TrackNamespace& prefix_namespace,
                                   const quicr::SubscribeNamespaceAttributes& attributes) override;

        quicr::Expected<void, quicr::Error<quicr::PublishNamespaceErrorCode>> PublishNamespaceReceived(
          const std::shared_ptr<quicr::Session>& session,
          const quicr::TrackNamespace& track_namespace,
          const quicr::PublishNamespaceAttributes& publish_announce_attributes) override;

        quicr::Expected<void, quicr::Error<int>> NewGroupRequested(const quicr::FullTrackName& track_full_name,
                                                                   std::uint64_t group_id) override;

        quicr::Expected<void, quicr::Error<int>> UnsubscribeReceived(const std::shared_ptr<quicr::Session>& session,
                                                                     std::uint64_t request_id) override;

      public:
        std::optional<std::promise<SubscribeDetails>> publish_accepted_promise_;
        std::optional<std::promise<uint64_t>> unsubscribe_promise_;

      private:
        mutable std::mutex state_mutex_;

        std::optional<std::promise<SubscribeDetails>> subscribe_promise_;
        std::optional<std::promise<SubscribeNamespaceDetails>> subscribe_namespace_promise_;
        std::optional<std::promise<PublishNamespaceDetails>> publish_namespace_promise_;
        std::optional<std::promise<JoiningFetchDetails>> joining_fetch_promise_;
        std::optional<std::promise<uint64_t>> publish_namespace_done_promise_;
        std::optional<std::promise<UnsubscribeReceivedDetails>> unsubscribe_received_promise_;
        std::optional<UnsubscribeReceivedDetails::HandlerType> expected_unsubscribe_handler_type_;
        std::map<std::uint64_t, bool> closed_streams_;
        std::shared_ptr<quicr::PublishNamespaceHandler> publish_namespace_handler_;
        std::vector<FetchResponseData> fetch_response_data_;

        // Bridging state shared across all TestServer instances (one per connection)
        // belonging to the same logical relay/server.
        std::shared_ptr<SharedState> shared_state_;
    };

}
