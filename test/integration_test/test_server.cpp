#include "test_server.h"

using namespace quicr;
using namespace quicr_test;

TestServer::TestServer(const ServerConfig& config)
  : Server(config)
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
