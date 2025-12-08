#pragma once

#include "quicr/publish_track_handler.h"
#include "quicr/track_name.h"

#include <span>
#include <vector>

namespace quicr {
    class PublishNamespaceHandler : public PublishTrackHandler
    {
      protected:
        PublishNamespaceHandler(const TrackNamespace& prefix,
                                TrackMode track_mode,
                                uint8_t default_priority,
                                uint32_t default_ttl,
                                std::optional<messages::StreamHeaderType> stream_mode = std::nullopt)
          : PublishTrackHandler({ prefix, {} }, track_mode, default_priority, default_ttl, stream_mode)
          , prefix_{ prefix }
        {
        }

      public:
        static std::shared_ptr<PublishNamespaceHandler> Create(
          const TrackNamespace& prefix,
          TrackMode track_mode,
          uint8_t default_priority,
          uint32_t default_ttl,
          std::optional<messages::StreamHeaderType> stream_mode = std::nullopt)
        {
            return std::shared_ptr<PublishNamespaceHandler>(
              new PublishNamespaceHandler(prefix, track_mode, default_priority, default_ttl, stream_mode));
        }

        /**
         * @brief Get the track alias
         *
         * @details If the track alias is set, it will be returned, otherwise std::nullopt.
         *
         * @return Track alias if set, otherwise std::nullopt.
         */
        const TrackNamespace& GetPrefix() const noexcept { return prefix_; }

      private:
        const TrackNamespace prefix_;
    };
}
