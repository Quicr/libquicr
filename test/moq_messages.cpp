#include <doctest/doctest.h>

#include <quicr/moq_message_types.h>
#include <quicr/message_buffer.h>
#include <quicr/quicr_common.h>
#include <quicr_name>

#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace quicr;
using namespace quicr::messages;


TEST_CASE("Announce Message encode/decode")
{
    quicr::Namespace qnamespace{ 0x10000000000000002000_name, 125 };
    std::stringstream  ss;
    ss << qnamespace;
    auto ns = "moq://" + ss.str();
    auto announce  = MoqAnnounce {
        .track_namespace = ns,
    };
    MessageBuffer buffer;
    buffer <<  announce;
    MoqAnnounce announce_out;
    buffer >> announce_out;
    CHECK_EQ(ns, announce_out.track_namespace);
}


TEST_CASE("Unannounce Message encode/decode")
{
    quicr::Namespace qnamespace{ 0x10000000000000002000_name, 125 };
    std::stringstream  ss;
    ss << "moq://" << qnamespace;
    auto unannounce  = MoqUnannounce {
            .track_namespace = ss.str(),
    };
    MessageBuffer buffer;
    buffer <<  unannounce;
    MoqUnannounce unannounce_out;
    buffer >> unannounce_out;
    CHECK_EQ(ss.str(), unannounce_out.track_namespace);
}


TEST_CASE("Unsubscribe Message encode/decode")
{
    quicr::Namespace qnamespace{ 0x10000000000000002000_name, 125 };
    std::stringstream  ss;
    ss << "moq://" << qnamespace.name();
    auto unsub  = MoqUnsubscribe {
            .track = ss.str(),
    };
    MessageBuffer buffer;
    buffer <<  unsub;
    MoqUnsubscribe unsub_out;
    buffer >> unsub_out;
    CHECK_EQ(ss.str(), unsub_out.track);
}