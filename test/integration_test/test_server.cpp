#include "test_server.h"

using namespace quicr;
using namespace quicr_test;

TestServer::TestServer(const ServerConfig& config)
  : Server(config)
{
}

void
PublishDoneReceived([[maybe_unused]] quicr::ConnectionHandle connection_handle, [[maybe_unused]] uint64_t request_id)
{
}

void
PublishReceived([[maybe_unused]] quicr::ConnectionHandle connection_handle, [[maybe_unused]] uint64_t request_id)
{
}

void
TestServer::PublishReceived([[maybe_unused]] quicr::ConnectionHandle connection_handle,
                            [[maybe_unused]] uint64_t request_id,
                            [[maybe_unused]] const quicr::FullTrackName& track_full_name,
                            [[maybe_unused]] const quicr::messages::PublishAttributes& publish_attributes)
{
    ResolvePublish(connection_handle,
                   request_id,
                   publish_attributes.forward,
                   publish_attributes.priority,
                   publish_attributes.group_order,
                   {});
}

void
TestServer::PublishDoneReceived([[maybe_unused]] quicr::ConnectionHandle connection_handle,
                                [[maybe_unused]] uint64_t request_id)
{
}

void
TestServer::SubscribeReceived(ConnectionHandle connection_handle,
                              uint64_t request_id,
                              messages::FilterType filter_type,
                              const FullTrackName& track_full_name,
                              const messages::SubscribeAttributes& subscribe_attributes)
{
    if (subscribe_promise_.has_value()) {
        subscribe_promise_->set_value(
          { connection_handle, request_id, filter_type, track_full_name, subscribe_attributes });
    }
    const auto th = TrackHash(track_full_name);
    ResolveSubscribe(
      connection_handle, request_id, th.track_fullname_hash, { .reason_code = SubscribeResponse::ReasonCode::kOk });
}

TestServer::SubscribeNamespaceResponse
TestServer::SubscribeNamespaceReceived(ConnectionHandle connection_handle,
                                       const TrackNamespace& prefix_namespace,
                                       const PublishNamespaceAttributes& announce_attributes)
{
    if (subscribe_namespace_promise_.has_value()) {
        subscribe_namespace_promise_->set_value({ connection_handle, prefix_namespace, announce_attributes });
    }
    return { std::nullopt, {} };
}
