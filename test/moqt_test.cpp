#include <doctest/doctest.h>


#include <moqt/client.h>
#include <moqt/server.h>
#include <moqt/publish_track_handler.h>
#include <moqt/subscribe_track_handler.h>

using namespace moq::transport;

TEST_CASE("Track Handler")
{
    class PHandler : public PublishTrackHandler
    {
        PHandler(const bytes& track_namespace,
                 const bytes& track_name,
                 TrackMode track_mode,
                 uint8_t default_priority,
                 uint32_t default_ttl,
                 const cantina::LoggerPointer& logger)
          : PublishTrackHandler(track_namespace, track_name, track_mode, default_priority, default_ttl, logger)
        {
        }
    };

    std::string track_ns = "abc";
    std::string track_n = "track";
    PHandler phandler({ track_ns.begin(), track_ns.end() },
                      { track_n.begin(), track_n.end() },
                      PublishTrackHandler::TrackMode::STREAM_PER_GROUP,
                      1,
                      100,
                      std::make_shared<cantina::Logger>("PUB"));
}