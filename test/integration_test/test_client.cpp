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

void
TestClient::PublishReceived(const ConnectionHandle connection_handle,
                            const FullTrackName& track,
                            const messages::RequestID request_id)
{
    if (publish_received_) {
        publish_received_->set_value(track);
    }

    // Accept the publish with default subscribe attributes
    messages::SubscribeAttributes attributes = { .priority = 128,
                                                 .group_order = messages::GroupOrder::kOriginalPublisherOrder,
                                                 .delivery_timeout = std::chrono::milliseconds(0),
                                                 .forward = 1,
                                                 .new_group_request_id = std::nullopt,
                                                 .is_publisher_initiated = false };
    ResolvePublish(connection_handle, request_id, true, attributes);
}
