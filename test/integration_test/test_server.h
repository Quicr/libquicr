#pragma once

#include <quicr/server.h>

namespace quicr_test {
    class TestServer final : public quicr::Server
    {
      public:
        explicit TestServer(const quicr::ServerConfig& config);

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
        void UnsubscribeAnnouncesReceived([[maybe_unused]] quicr::ConnectionHandle connection_handle,
                                          [[maybe_unused]] const quicr::TrackNamespace& prefix_namespace) override {};
        void UnsubscribeReceived([[maybe_unused]] quicr::ConnectionHandle connection_handle,
                                 [[maybe_unused]] uint64_t request_id) override {};
        void FetchCancelReceived([[maybe_unused]] quicr::ConnectionHandle connection_handle,
                                 [[maybe_unused]] uint64_t request_id) override {};
    };
}