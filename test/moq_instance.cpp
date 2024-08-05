#include <doctest/doctest.h>

#include <quicr/moqt_core.h>

using namespace quicr;

TEST_CASE("Track Hash")
{
    std::string n1 = "namespace using some very long name";
    std::string tn1 = "track name can also be long. This track name is long.";
    std::vector<uint8_t> n1_v { n1.begin(), n1.end()};
    std::vector<uint8_t> tn1_v { tn1.begin(), tn1.end()};

    std::string n2 = "2 namespace using some very long name";
    std::string tn2 = "track name can also be long This track name is long. 2";
    std::vector<uint8_t> n2_v { n2.begin(), n2.end()};
    std::vector<uint8_t> tn2_v { tn2.begin(), tn2.end()};

    auto tfn_1 = MoQInstance::TrackFullName{ n1_v, tn1_v };
    auto th_1 = MoQInstance::TrackHash(tfn_1);
    auto th_1_b = MoQInstance::TrackHash(tfn_1);
    CHECK_EQ(th_1.track_fullname_hash, th_1_b.track_fullname_hash);

    auto tfn_2 = MoQInstance::TrackFullName{ n2_v, tn2_v };
    auto th_2 = MoQInstance::TrackHash(tfn_2);

    CHECK_NE(th_1.track_fullname_hash, th_2.track_fullname_hash);
}

