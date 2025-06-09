#include <quicr/client.h>

namespace quicr_test {
    class TestClient final : public quicr::Client
    {
      public:
        explicit TestClient(const quicr::ClientConfig& cfg);

        // Connection.
        using ClientConnected = std::function<void(const quicr::ServerSetupAttributes&)>;
        void SetClientConnected(const ClientConnected& cb);
        void ServerSetupReceived(const quicr::ServerSetupAttributes& server_setup_attributes) override;

      private:
        ClientConnected client_connected_;
    };
}