#include <doctest/doctest.h>

#include <quicr/common.h>
#include <quicr/track_name.h>

#include <vector>

using namespace quicr;
using namespace std::string_literals;

std::vector<TrackNamespace>
FindTracks(Span<const TrackNamespace> tracks, const TrackNamespace& track)
{
    std::vector<TrackNamespace> matching_tracks;

    std::for_each(tracks.begin(), tracks.end(), [&](const auto& t) {
        if (track.Contains(t))
            matching_tracks.emplace_back(t);
    });

    return matching_tracks;
}

const std::vector<TrackNamespace> tracks{
    TrackNamespace{ "example"s, "chat555"s, "user1"s, "dev1"s, "time1"s },
    TrackNamespace{ "example"s, "chat555"s, "user1"s, "dev2"s, "time1"s },
    TrackNamespace{ "example"s, "chat555"s, "user1"s, "dev1"s, "time3"s },
    TrackNamespace{ "example"s, "chat555"s, "user2"s, "dev1"s, "time4"s },
};

TEST_CASE("Full match")
{
    auto matching_tracks = FindTracks(tracks, TrackNamespace{ "example"s, "chat555"s });
    CHECK_EQ(matching_tracks, tracks);
}

TEST_CASE("Partial match (many entries)")
{
    const std::vector<TrackNamespace> expected_tracks{
        TrackNamespace{ "example"s, "chat555"s, "user1"s, "dev1"s, "time1"s },
        TrackNamespace{ "example"s, "chat555"s, "user1"s, "dev2"s, "time1"s },
        TrackNamespace{ "example"s, "chat555"s, "user1"s, "dev1"s, "time3"s },
    };

    auto matching_tracks = FindTracks(tracks, TrackNamespace{ "example"s, "chat555"s, "user1"s });
    CHECK_EQ(matching_tracks, expected_tracks);
}

TEST_CASE("Partial match (single entry)")
{
    const std::vector<TrackNamespace> expected_tracks{ TrackNamespace{
      "example"s, "chat555"s, "user2"s, "dev1"s, "time4"s } };

    auto matching_tracks = FindTracks(tracks, TrackNamespace{ "example"s, "chat555"s, "user2"s });
    CHECK_EQ(matching_tracks, expected_tracks);
}
