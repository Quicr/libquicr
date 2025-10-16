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

void
TestClient::SubscribeNamespaceStatusChanged(
  const TrackNamespace& prefix_namespace,
  [[maybe_unused]] std::optional<messages::SubscribeNamespaceErrorCode> error_code,
  [[maybe_unused]] std::optional<messages::ReasonPhrase> error_reason)
{
    if (subscribe_namespace_ok_ && !error_code.has_value()) {
        subscribe_namespace_ok_->set_value(prefix_namespace);
    }
}

void
TestClient::PublishNamespaceReceived([[maybe_unused]] const TrackNamespace& track_namespace,
                                     [[maybe_unused]] const PublishNamespaceAttributes& publish_namespace_attributes)
{
    if (publish_namespace_received_) {
        publish_namespace_received_->set_value(track_namespace);
    }
}
