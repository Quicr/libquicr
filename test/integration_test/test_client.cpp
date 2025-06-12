#include "test_client.h"

using namespace quicr;
using namespace quicr_test;

TestClient::TestClient(const ClientConfig& cfg)
  : Client(cfg)
{
}

void
TestClient::SetClientConnectedCallback(const ClientConnectedCallback& cb)
{
    client_connected_ = std::move(cb);
}

void
TestClient::ServerSetupReceived(const ServerSetupAttributes& server_setup_attributes)
{
    assert(client_connected_);
    client_connected_(server_setup_attributes);
}
