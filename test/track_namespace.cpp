#include <doctest/doctest.h>
#include <map>

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
        if (track.IsPrefixOf(t))
            matching_tracks.emplace_back(t);
    });

    return matching_tracks;
}

const std::vector<TrackNamespace> kTracks{
    TrackNamespace{ "example"s, "chat555"s, "user1"s, "dev1"s, "time1"s },
    TrackNamespace{ "example"s, "chat555"s, "user1"s, "dev2"s, "time1"s },
    TrackNamespace{ "example"s, "chat555"s, "user1"s, "dev1"s, "time3"s },
    TrackNamespace{ "example"s, "chat555"s, "user2"s, "dev1"s, "time4"s },
};

TEST_CASE("Hash namespace")
{
    TrackNamespace ns{ "example"s, "chat555"s, "user1"s, "dev1"s, "time1"s };

    auto h = hash({ ns.begin(), ns.end() });
    CHECK_EQ(15211761882639286592, h);

    TrackHash th({ ns, {}, std::nullopt });
    CHECK_EQ(h, th.track_namespace_hash);

    auto ns_hash = std::hash<TrackNamespace>{}(ns);
    CHECK_EQ(ns_hash, h);
}

TEST_CASE("Full match")
{
    auto matching_tracks = FindTracks(kTracks, TrackNamespace{ "example"s, "chat555"s });
    CHECK_EQ(matching_tracks, kTracks);
}

TEST_CASE("Partial match (many entries)")
{
    const std::vector<TrackNamespace> expected_tracks{
        TrackNamespace{ "example"s, "chat555"s, "user1"s, "dev1"s, "time1"s },
        TrackNamespace{ "example"s, "chat555"s, "user1"s, "dev2"s, "time1"s },
        TrackNamespace{ "example"s, "chat555"s, "user1"s, "dev1"s, "time3"s },
    };

    auto matching_tracks = FindTracks(kTracks, TrackNamespace{ "example"s, "chat555"s, "user1"s });
    CHECK_EQ(matching_tracks, expected_tracks);
}

TEST_CASE("Partial match (single entry)")
{
    const std::vector<TrackNamespace> expected_tracks{ TrackNamespace{
      "example"s, "chat555"s, "user2"s, "dev1"s, "time4"s } };

    auto matching_tracks = FindTracks(kTracks, TrackNamespace{ "example"s, "chat555"s, "user2"s });
    CHECK_EQ(matching_tracks, expected_tracks);
}

TEST_CASE("No match")
{
    auto matching_tracks = FindTracks(kTracks, TrackNamespace{ "example"s, "chat555"s, "user"s });
    CHECK(matching_tracks.empty());
}

TEST_CASE("IsPrefix vs HasPrefix")
{
    TrackNamespace long_ns{ "example"s, "chat555"s, "user2"s, "dev1"s, "time4"s };
    TrackNamespace short_ns{ "example"s, "chat555"s, "user2"s };

    CHECK(short_ns.IsPrefixOf(long_ns));
    CHECK_FALSE(long_ns.IsPrefixOf(short_ns));

    CHECK(long_ns.HasSamePrefix(short_ns));
    CHECK(short_ns.HasSamePrefix(long_ns));
}

TEST_CASE("Find prefix matching map of namespaces")
{
    std::map<TrackNamespace, std::string> ns_map;
    ns_map.emplace(TrackNamespace{ "example"s, "chat1"s, "user1"s, "dev1"s }, "chat-1-user-1-dev-1");
    ns_map.emplace(TrackNamespace{ "example"s, "chat1"s, "user1"s, "dev2"s }, "chat-1-user-1-dev-2");
    ns_map.emplace(TrackNamespace{ "example"s, "chat2"s, "user1"s, "dev1"s }, "chat-2-user-1-dev-1");
    ns_map.emplace(TrackNamespace{ "example"s, "chat2"s, "user2"s, "dev1"s }, "chat-2-user-2-dev-1");
    ns_map.emplace(TrackNamespace{ "example"s, "chat3"s, "user1"s, "dev1"s }, "chat-3-user-1-dev-1");

    TrackNamespace prefix_ns{ "example"s, "chat2"s };
    int found = 0;
    for (auto it = ns_map.lower_bound(prefix_ns); it != ns_map.end(); ++it) {
        if (!it->first.HasSamePrefix(prefix_ns)) {
            break;
        }
        found++;
    }

    CHECK_EQ(found, 2);
}

