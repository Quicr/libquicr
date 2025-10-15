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

        // Subscribe Namespace.
        void SetSubscribeNamespaceOkPromise(std::promise<quicr::TrackNamespace> promise)
        {
            subscribe_namespace_ok_ = std::move(promise);
        }
        void SubscribeNamespaceStatusChanged(const quicr::TrackNamespace& prefix_namespace,
                                             std::optional<quicr::messages::SubscribeNamespaceErrorCode> error_code,
                                             std::optional<quicr::messages::ReasonPhrase> error_reason) override;

      private:
        std::optional<std::promise<quicr::ServerSetupAttributes>> client_connected_;
        std::optional<std::promise<quicr::TrackNamespace>> subscribe_namespace_ok_;
    };
}
