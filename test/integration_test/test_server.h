#pragma once

#include <future>
#include <optional>
#include <quicr/server.h>

namespace quicr_test {
    class TestServer final : public quicr::Server
    {
      public:
        explicit TestServer(const quicr::ServerConfig& config);
        struct SubscribeDetails
        {
            quicr::ConnectionHandle connection_handle;
            uint64_t request_id;
            quicr::messages::FilterType filter_type;
            quicr::FullTrackName track_full_name;
            quicr::messages::SubscribeAttributes subscribe_attributes;
        };

        struct SubscribeNamespaceDetails
        {
            quicr::ConnectionHandle connection_handle;
            quicr::TrackNamespace prefix_namespace;
            quicr::PublishNamespaceAttributes announce_attributes;
        };

        // Set up promise for subscription event
        void SetSubscribePromise(std::promise<SubscribeDetails> promise) { subscribe_promise_ = std::move(promise); }

        // Set up promise for subscribe namespace event
        void SetSubscribeNamespacePromise(std::promise<SubscribeNamespaceDetails> promise)
        {
            subscribe_namespace_promise_ = std::move(promise);
        }

      protected:
        ClientSetupResponse ClientSetupReceived(
          [[maybe_unused]] quicr::ConnectionHandle connection_handle,
          [[maybe_unused]] const quicr::ClientSetupAttributes& client_setup_attributes) override
        {
            return {};
        };
        std::vector<quicr::ConnectionHandle> UnannounceReceived(
          [[maybe_unused]] quicr::ConnectionHandle connection_handle,
          [[maybe_unused]] const quicr::TrackNamespace& track_namespace) override
        {
            return {};
        };
        void UnsubscribeNamespaceReceived([[maybe_unused]] quicr::ConnectionHandle connection_handle,
                                          [[maybe_unused]] const quicr::TrackNamespace& prefix_namespace) override {};
        void UnsubscribeReceived([[maybe_unused]] quicr::ConnectionHandle connection_handle,
                                 [[maybe_unused]] uint64_t request_id) override {};
        void FetchCancelReceived([[maybe_unused]] quicr::ConnectionHandle connection_handle,
                                 [[maybe_unused]] uint64_t request_id) override {};

        void SubscribeReceived(quicr::ConnectionHandle connection_handle,
                               uint64_t request_id,
                               quicr::messages::FilterType filter_type,
                               const quicr::FullTrackName& track_full_name,
                               const quicr::messages::SubscribeAttributes& subscribe_attributes) override;

        void PublishReceived(quicr::ConnectionHandle connection_handle,
                             uint64_t request_id,
                             const quicr::FullTrackName& track_full_name,
                             const quicr::messages::PublishAttributes& publish_attributes) override;
        void SubscribeDoneReceived(quicr::ConnectionHandle connection_handle, uint64_t request_id) override;

        SubscribeAnnouncesResponse SubscribeNamespaceReceived(
          quicr::ConnectionHandle connection_handle,
          const quicr::TrackNamespace& prefix_namespace,
          const quicr::PublishNamespaceAttributes& announce_attributes) override;

      private:
        std::optional<std::promise<SubscribeDetails>> subscribe_promise_;
        std::optional<std::promise<SubscribeNamespaceDetails>> subscribe_namespace_promise_;
    };
}
