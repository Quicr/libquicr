#pragma once

#include <future>
#include <map>
#include <optional>

#include <quicr/publish_track_handler.h>
#include <quicr/session.h>
#include <quicr/subscribe_track_handler.h>

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

        void StreamClosed(std::uint64_t stream_id, bool reset) override
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

    class TestServer final : public quicr::Session
    {
      public:
        explicit TestServer(const quicr::ServerConfig& config);

        struct SubscribeDetails
        {
            std::uint64_t connection_handle;
            uint64_t request_id;
            quicr::FullTrackName track_full_name;
            quicr::messages::SubscribeAttributes subscribe_attributes;
        };

        struct SubscribeNamespaceDetails
        {
            std::uint64_t connection_handle;
            std::uint64_t data_ctx_id{ 0 };
            quicr::TrackNamespace prefix_namespace;
            quicr::messages::SubscribeNamespaceAttributes attributes;
        };

        struct PublishNamespaceDetails
        {
            std::uint64_t connection_handle;
            quicr::TrackNamespace track_namespace;
            quicr::messages::PublishNamespaceAttributes attributes;
        };

        // Data to respond with when a fetch is received
        struct FetchResponseData
        {
            quicr::ObjectHeaders headers{};
            std::vector<uint8_t> payload;
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

        // Set up data to respond with when a fetch is received
        void SetFetchResponseData(std::vector<FetchResponseData> data) { fetch_response_data_ = std::move(data); }

        void AddKnownPublishedNamespace(const quicr::TrackNamespace& track_namespace);
        void AddKnownPublishedTrack(const quicr::FullTrackName& track,

                                    const std::optional<quicr::messages::Location>& largest_location,
                                    const quicr::messages::PublishAttributes& attributes);

      protected:
        std::vector<std::uint64_t> PublishNamespaceDoneReceived(std::uint64_t, std::uint64_t request_id) override
        {
            return {};
        }

        void UnsubscribeNamespaceReceived([[maybe_unused]] std::uint64_t connection_handle,
                                          [[maybe_unused]] const quicr::TrackNamespace& prefix_namespace) override {};
        void UnsubscribeReceived([[maybe_unused]] std::uint64_t connection_handle,
                                 [[maybe_unused]] uint64_t request_id) override
        {
        }

        void FetchCancelReceived([[maybe_unused]] std::uint64_t connection_handle,
                                 [[maybe_unused]] uint64_t request_id) override
        {
        }

        void StandaloneFetchReceived(std::uint64_t connection_handle,
                                     uint64_t request_id,
                                     const quicr::FullTrackName& track_full_name,
                                     const quicr::messages::StandaloneFetchAttributes& attrs) override;

        void SubscribeReceived(std::uint64_t connection_handle,
                               uint64_t request_id,
                               const quicr::FullTrackName& track_full_name,
                               const quicr::messages::SubscribeAttributes& subscribe_attributes) override;

        void PublishReceived(std::uint64_t connection_handle,
                             uint64_t request_id,
                             const quicr::messages::PublishAttributes& publish_attributes,
                             std::weak_ptr<quicr::SubscribeNamespaceHandler> ns_handler) override;

        void PublishDoneReceived(std::uint64_t connection_handle, uint64_t request_id) override;

        void SubscribeTracksReceived(std::uint64_t connection_handle,
                                     std::uint64_t data_ctx_id,
                                     const quicr::TrackNamespace& prefix_namespace,
                                     const quicr::messages::SubscribeNamespaceAttributes& attributes) override;

        void SubscribeNamespaceReceived(std::uint64_t connection_handle,
                                        std::uint64_t data_ctx_id,
                                        const quicr::TrackNamespace& prefix_namespace,
                                        const quicr::messages::SubscribeNamespaceAttributes& attributes) override;

        void PublishNamespaceReceived(
          std::uint64_t connection_handle,
          const quicr::TrackNamespace& track_namespace,
          const quicr::messages::PublishNamespaceAttributes& publish_announce_attributes) override;

        void NewGroupRequested(const quicr::FullTrackName& track_full_name, std::uint64_t group_id) override;

      public:
        std::optional<std::promise<SubscribeDetails>> publish_accepted_promise_;

      private:
        mutable std::mutex state_mutex_;

        std::optional<std::promise<SubscribeDetails>> subscribe_promise_;
        std::optional<std::promise<SubscribeNamespaceDetails>> subscribe_namespace_promise_;
        std::optional<std::promise<PublishNamespaceDetails>> publish_namespace_promise_;
        std::vector<quicr::TrackNamespace> known_published_namespaces_;
        std::shared_ptr<quicr::PublishNamespaceHandler> publish_namespace_handler_;
        struct AvailableTrack
        {
            quicr::FullTrackName full_track_name;
            quicr::messages::Location start_location;
            quicr::messages::PublishAttributes attributes;
        };

        std::vector<AvailableTrack> known_published_tracks_;
        std::unordered_map<quicr::TrackNamespace,
                           std::map<std::uint64_t, std::shared_ptr<quicr::PublishNamespaceHandler>>>
          namespace_subscribers_;
        std::vector<FetchResponseData> fetch_response_data_;

        // Subscriber publish handlers: [track_alias][connection_handle] -> PublishTrackHandler
        std::map<std::uint64_t, std::map<std::uint64_t, std::shared_ptr<TestPublishTrackHandler>>> subscribes_;

        // Publisher subscribe handlers: [track_alias][connection_handle] -> SubscribeTrackHandler
        std::map<std::uint64_t, std::map<std::uint64_t, std::shared_ptr<TestSubscribeTrackHandler>>> pub_subscribes_;
    };

}
