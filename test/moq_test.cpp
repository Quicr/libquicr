#include <doctest/doctest.h>

#include <quicr/moqt_client.h>
#include <quicr/moqt_core.h>
#include <quicr/moqt_server.h>

using namespace quicr;

TEST_CASE("Track Handler")
{
    class PHandler : public MOQTPublishTrackHandler
    {
        PHandler(const bytes& track_namespace,
                 const bytes& track_name,
                 TrackMode track_mode,
                 uint8_t default_priority,
                 uint32_t default_ttl,
                 const cantina::LoggerPointer& logger)
          : MOQTPublishTrackHandler(track_namespace, track_name, track_mode, default_priority, default_ttl, logger)
        {
        }

      public:
        void statusCallback(Status status) override;
        void congestedCallback(bool cleared, uint64_t objects_in_queue) override;
    };

    std::string track_ns = "abc";
    std::string track_n = "track";
    PHandler phandler({ track_ns.begin(), track_ns.end() },
                      { track_n.begin(), track_n.end() },
                      MOQTBaseTrackHandler::TrackMode::STREAM_PER_GROUP,
                      1,
                      100,
                      std::make_shared<cantina::Logger>("PUB"));
}