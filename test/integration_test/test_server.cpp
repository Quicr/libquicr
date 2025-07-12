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
                              uint64_t proposed_track_alias,
                              messages::FilterType filter_type,
                              const FullTrackName& track_full_name,
                              const messages::SubscribeAttributes& subscribe_attributes)
{
    if (subscribe_promise_.has_value()) {
        subscribe_promise_->set_value(
          { connection_handle, request_id, proposed_track_alias, filter_type, track_full_name, subscribe_attributes });
    }
    ResolveSubscribe(connection_handle, request_id, { .reason_code = SubscribeResponse::ReasonCode::kOk });
}
