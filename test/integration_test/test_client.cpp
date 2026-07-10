#include "test_client.h"

#include "quicr/handlers/subscribe_track_handler.h"

using namespace quicr;
using namespace quicr_test;

TestClient::TestClient(const ClientConfig& cfg,
                       std::shared_ptr<Transport> transport,
                       std::shared_ptr<Connection> connection,
                       std::shared_ptr<timeq::tick_service> tick_service)
  : Session(cfg, std::move(transport), std::move(connection), std::move(tick_service))
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
TestClient::PublishReceived(uint64_t request_id,
                            const quicr::PublishAttributes& publish_attributes,
                            [[maybe_unused]] std::weak_ptr<SubscribeNamespaceHandler> ns_handler)
{
    auto sub_handler =
      SubscribeTrackHandler::Create(publish_attributes.track_full_name, publish_attributes.default_publisher_priority);
    last_publish_received_sub_handler_ = sub_handler;

    if (publish_received_) {
        publish_received_->set_value(publish_attributes.track_full_name);
    }

    ResolvePublish(request_id, publish_attributes, { .reason_code = PublishResponse::ReasonCode::kOk }, sub_handler);
}
