#include "test_client.h"

using namespace quicr;
using namespace quicr_test;

TestClient::TestClient(const ClientConfig& cfg)
  : Client(cfg)
{
}

void
TestClient::ServerSetupReceived(const ServerSetupAttributes& server_setup_attributes)
{
    if (client_connected_) {
        client_connected_->set_value(server_setup_attributes);
    }
}
