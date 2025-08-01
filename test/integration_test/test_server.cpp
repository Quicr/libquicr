#include "test_server.h"

using namespace quicr;
using namespace quicr_test;

TestServer::TestServer(const ServerConfig& config)
  : Server(config)
{
}

void
SubscribeDoneReceived([[maybe_unused]] quicr::ConnectionHandle connection_handle, [[maybe_unused]] uint64_t request_id)
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
}

void
TestServer::SubscribeDoneReceived([[maybe_unused]] quicr::ConnectionHandle connection_handle,
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
