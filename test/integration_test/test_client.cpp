#include "test_client.h"

#include "quicr/handlers/subscribe_track_handler.h"

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
TestClient::PublishReceived(std::uint64_t connection_id,
                            uint64_t request_id,
                            const quicr::PublishAttributes& publish_attributes,
                            [[maybe_unused]] std::weak_ptr<SubscribeNamespaceHandler> ns_handler)
{
    auto sub_handler =
      SubscribeTrackHandler::Create(publish_attributes.track_full_name, publish_attributes.default_publisher_priority);
    last_publish_received_sub_handler_ = sub_handler;

    if (publish_received_) {
        publish_received_->set_value(publish_attributes.track_full_name);
    }

    ResolvePublish(
      connection_id, request_id, publish_attributes, { .reason_code = PublishResponse::ReasonCode::kOk }, sub_handler);
}

void
TestClient::TrackStatusResponseReceived([[maybe_unused]] std::uint64_t connection_id,
                                        std::uint64_t request_id,
                                        const TrackStatusResponse& response)
{
    if (track_status_response_) {
        track_status_response_->set_value({ request_id, response });
        track_status_response_.reset();
    }
}

void
TestClient::OnStreamClosed(const std::uint64_t& connection_id,
                           std::uint64_t stream_id,
                           std::shared_ptr<StreamRxContext> rx_ctx,
                           std::optional<std::uint64_t> data_ctx_id,
                           StreamClosedFlag flag)
{
    if (request_stream_closed_.has_value()) {
        request_stream_closed_->set_value(flag == StreamClosedFlag::kReset);
        request_stream_closed_.reset();
    }
    Session::OnStreamClosed(connection_id, stream_id, std::move(rx_ctx), data_ctx_id, flag);
}
