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
TestClient::PublishNamespaceReceived([[maybe_unused]] const TrackNamespace& track_namespace,
                                     [[maybe_unused]] const PublishNamespaceAttributes& publish_namespace_attributes)
{
    if (publish_namespace_received_) {
        publish_namespace_received_->set_value(track_namespace);
    }
}

void
TestClient::PublishReceived(quicr::ConnectionHandle connection_handle,
                            uint64_t request_id,
                            const quicr::messages::PublishAttributes& publish_attributes)
{
    if (publish_received_) {
        publish_received_->set_value(publish_attributes.track_full_name);
    }

    ResolvePublish(
      connection_handle, request_id, publish_attributes, { .reason_code = PublishResponse::ReasonCode::kOk });
}

void
TestClient::PublishNamespaceStatusChanged(quicr::messages::RequestID request_id, const PublishNamespaceStatus status)
{
    if (publish_namespace_status_changed_ && status == PublishNamespaceStatus::kOK) {
        publish_namespace_status_changed_->set_value(request_id);
    }
}
