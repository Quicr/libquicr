#include "callback_subscribe_handler.h"
#include <doctest/doctest.h>

using namespace quicr;

namespace quicr_test {
    CallbackSubscribeHandler::CallbackSubscribeHandler(const FullTrackName& full_track_name)
      : SubscribeTrackHandler(full_track_name,
                              0,
                              messages::GroupOrder::kOriginalPublisherOrder,
                              messages::FilterType::kLatestObject)
    {
    }

    void CallbackSubscribeHandler::StatusChanged(const Status status)
    {
        REQUIRE(status_change_);
        status_change_(status);
    }

    void CallbackSubscribeHandler::SetStatusChange(StatusChange status_change)
    {
        status_change_ = std::move(status_change);
    }
}
