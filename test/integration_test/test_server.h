#pragma once

#include <future>
#include <map>
#include <optional>
#include <quicr/publish_fetch_handler.h>
#include <quicr/publish_track_handler.h>
#include <quicr/server.h>
#include <quicr/subscribe_track_handler.h>

namespace quicr_test {

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
                                  quicr::messages::FilterType::kLargestObject,
                                  std::nullopt,
                                  is_publisher_initiated)
        {
        }

        void SetPublishHandler(std::shared_ptr<quicr::PublishTrackHandler> pub_handler)
        {
            std::lock_guard lock(mutex_);
            pub_handler_ = pub_handler;
        }

        void ObjectReceived(const quicr::ObjectHeaders& object_headers, quicr::BytesSpan data) override
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
                pub_handler_->PublishObject(object_headers, data);
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
                                uint32_t default_ttl)
          : quicr::PublishTrackHandler(full_track_name, track_mode, default_priority, default_ttl)
        {
        }

        void StatusChanged([[maybe_unused]] Status status) override {}
    };

    class TestServer final : public quicr::Server
    {
      public:
        explicit TestServer(const quicr::ServerConfig& config);
        struct SubscribeDetails
        {
            quicr::ConnectionHandle connection_handle;
            uint64_t request_id;
            quicr::FullTrackName track_full_name;
            quicr::messages::SubscribeAttributes subscribe_attributes;
        };

        struct SubscribeNamespaceDetails
        {
            quicr::ConnectionHandle connection_handle;
            quicr::TrackNamespace prefix_namespace;
            quicr::SubscribeNamespaceAttributes attributes;
        };

        struct PublishNamespaceDetails
        {
            quicr::ConnectionHandle connection_handle;
            quicr::TrackNamespace track_namespace;
            quicr::PublishNamespaceAttributes attributes;
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
        ClientSetupResponse ClientSetupReceived(
          [[maybe_unused]] quicr::ConnectionHandle connection_handle,
          [[maybe_unused]] const quicr::ClientSetupAttributes& client_setup_attributes) override
        {
            return {};
        };

        std::vector<quicr::ConnectionHandle> PublishNamespaceDoneReceived(quicr::ConnectionHandle,
                                                                          messages::RequestID request_id) override
        {
            return {};
        }

        void UnsubscribeNamespaceReceived([[maybe_unused]] quicr::ConnectionHandle connection_handle,
                                          [[maybe_unused]] quicr::DataContextId data_ctx_id,
                                          [[maybe_unused]] const quicr::TrackNamespace& prefix_namespace) override {};
        void UnsubscribeReceived([[maybe_unused]] quicr::ConnectionHandle connection_handle,
                                 [[maybe_unused]] uint64_t request_id) override
        {
        }

        void FetchCancelReceived([[maybe_unused]] quicr::ConnectionHandle connection_handle,
                                 [[maybe_unused]] uint64_t request_id) override
        {
        }

        void StandaloneFetchReceived(quicr::ConnectionHandle connection_handle,
                                     uint64_t request_id,
                                     const quicr::FullTrackName& track_full_name,
                                     const quicr::messages::StandaloneFetchAttributes& attrs) override;

        void SubscribeReceived(quicr::ConnectionHandle connection_handle,
                               uint64_t request_id,
                               const quicr::FullTrackName& track_full_name,
                               const quicr::messages::SubscribeAttributes& subscribe_attributes) override;

        void PublishReceived(quicr::ConnectionHandle connection_handle,
                             uint64_t request_id,
                             const quicr::messages::PublishAttributes& publish_attributes) override;
        void PublishDoneReceived(quicr::ConnectionHandle connection_handle, uint64_t request_id) override;

        void SubscribeNamespaceReceived(quicr::ConnectionHandle connection_handle,
                                        quicr::DataContextId data_ctx_id,
                                        const quicr::TrackNamespace& prefix_namespace,
                                        const quicr::SubscribeNamespaceAttributes& attributes) override;

        void PublishNamespaceReceived(quicr::ConnectionHandle connection_handle,
                                      const quicr::TrackNamespace& track_namespace,
                                      const quicr::PublishNamespaceAttributes& publish_announce_attributes) override;

      private:
        mutable std::mutex state_mutex_;

        std::optional<std::promise<SubscribeDetails>> subscribe_promise_;
        std::optional<std::promise<SubscribeNamespaceDetails>> subscribe_namespace_promise_;
        std::optional<std::promise<PublishNamespaceDetails>> publish_namespace_promise_;
        std::vector<quicr::TrackNamespace> known_published_namespaces_;
        std::vector<quicr::SubscribeNamespaceResponse::AvailableTrack> known_published_tracks_;
        std::optional<std::promise<SubscribeDetails>> publish_accepted_promise_;
        std::unordered_map<quicr::messages::TrackNamespacePrefix, std::vector<quicr::ConnectionHandle>>
          namespace_subscribers_;
        std::vector<FetchResponseData> fetch_response_data_;

        // Subscriber publish handlers: [track_alias][connection_handle] -> PublishTrackHandler
        std::map<quicr::messages::TrackAlias,
                 std::map<quicr::ConnectionHandle, std::shared_ptr<TestPublishTrackHandler>>>
          subscribes_;

        // Publisher subscribe handlers: [track_alias][connection_handle] -> SubscribeTrackHandler
        std::map<quicr::messages::TrackAlias,
                 std::map<quicr::ConnectionHandle, std::shared_ptr<TestSubscribeTrackHandler>>>
          pub_subscribes_;
    };
}
