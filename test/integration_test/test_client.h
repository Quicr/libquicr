#include <quicr/client.h>

namespace quicr_test {
    class TestClient final : public quicr::Client
    {
      public:
        explicit TestClient(const quicr::ClientConfig& cfg);

        // Connection.
        using ClientConnectedCallback = std::function<void(const quicr::ServerSetupAttributes&)>;
        void SetClientConnectedCallback(const ClientConnectedCallback& cb);
        void ServerSetupReceived(const quicr::ServerSetupAttributes& server_setup_attributes) override;

      private:
        ClientConnectedCallback client_connected_;
    };
}
