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

      private:
        std::optional<std::promise<quicr::ServerSetupAttributes>> client_connected_;
    };
}
