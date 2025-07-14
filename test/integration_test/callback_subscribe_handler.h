#pragma once

#include <functional>
#include <quicr/subscribe_track_handler.h>

namespace quicr_test {
    class CallbackSubscribeHandler final : public quicr::SubscribeTrackHandler
    {
      public:
        using StatusChange = std::function<void(Status)>;
        explicit CallbackSubscribeHandler(const quicr::FullTrackName&);
        void StatusChanged(Status) override;
        void SetStatusChange(StatusChange);

      private:
        StatusChange status_change_;
    };
}
