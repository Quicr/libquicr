#include "test_client.h"

#include "quicr/handlers/subscribe_track_handler.h"
#include "quicr/session.h"

using namespace quicr;
using namespace quicr_test;

quicr::Reply<void, quicr::ErrorCode>
TestClient::ServerSetupReceived([[maybe_unused]] const std::shared_ptr<Session>& session,
                                const ServerSetupAttributes& server_setup_attributes)
{
    if (client_connected_) {
        client_connected_->set_value(server_setup_attributes);
    }

    return {};
}

quicr::Reply<void, quicr::PublishNamespaceErrorCode>
TestClient::PublishNamespaceReceived([[maybe_unused]] const std::shared_ptr<Session>& session,
                                     [[maybe_unused]] const TrackNamespace& track_namespace,
                                     [[maybe_unused]] const PublishNamespaceAttributes& publish_namespace_attributes)
{
    if (publish_namespace_received_) {
        publish_namespace_received_->set_value(track_namespace);
    }

    return {};
}

quicr::Reply<const quicr::PublishResponse, quicr::PublishErrorCode>
TestClient::PublishReceived([[maybe_unused]] const std::shared_ptr<Session>& session,
                            [[maybe_unused]] std::uint64_t request_id,
                            const quicr::PublishAttributes& publish_attributes,
                            [[maybe_unused]] std::weak_ptr<SubscribeNamespaceHandler> ns_handler)
{
    auto sub_handler =
      SubscribeTrackHandler::Create(publish_attributes.track_full_name, publish_attributes.default_publisher_priority);
    last_publish_received_sub_handler_ = sub_handler;

    if (publish_received_) {
        publish_received_->set_value(publish_attributes.track_full_name);
    }

    return quicr::PublishResponse{ {}, sub_handler };
}
