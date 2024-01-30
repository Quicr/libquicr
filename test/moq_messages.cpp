#include <doctest/doctest.h>

#include <quicr/moq_message_types.h>
#include <quicr/message_buffer.h>
#include <quicr/quicr_common.h>

#include <memory>
#include <sstream>
#include <string>

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

  auto announce_ok = MoqAnnounceOk {
      .track_namespace = ns
  };
  buffer << announce_ok;
  MoqAnnounceOk announce_ok_out;
  buffer >> announce_ok_out;
  CHECK_EQ(ns, announce_ok_out.track_namespace);

  auto announce_err = MoqAnnounceError {
      .track_namespace = ns,
      .err_code = {0},
      .reason_phrase = "All Good Here",
  };

  buffer << announce_err;
  MoqAnnounceError announce_err_out;
  buffer >> announce_err_out;
  CHECK_EQ(announce_err.track_namespace, announce_err_out.track_namespace);
  CHECK_EQ(announce_err.err_code, announce_err_out.err_code);
  CHECK_EQ(announce_err.reason_phrase, announce_err_out.reason_phrase);

}


TEST_CASE("Subscribe Message encode/decode")
{
    quicr::Namespace qnamespace{ 0x10000000000000002000_name, 125 };
    std::stringstream  ss;
    ss << qnamespace;
    auto ns = "moq://" + ss.str();
    auto subscribe  = MoqSubscribe {
        .subscribe_id = 2,
        .track_alias = 1,
        .track = qnamespace.name(),
        .start_group = Location { .mode = LocationMode::Absolute, .value = 100},
        .start_object = Location {.mode = LocationMode::Absolute, .value = 0},
        .end_group = Location { .mode = LocationMode::Absolute, .value = 1000},
        .end_object = Location {.mode = LocationMode::Absolute, .value = 0},
        //.track_params = {}
    };

    MessageBuffer buffer;
    buffer <<  subscribe;
    MoqSubscribe subscribe_out;
    buffer >> subscribe_out;
    CHECK_EQ(subscribe.track, subscribe_out.track);
    CHECK_EQ(subscribe.subscribe_id, subscribe_out.subscribe_id);
    CHECK_EQ(subscribe.start_group.mode, subscribe_out.start_group.mode);
    CHECK_EQ(subscribe.start_object.mode, subscribe_out.start_object.mode);
    CHECK_EQ(subscribe.end_group.value, subscribe_out.end_group.value);
    CHECK_EQ(subscribe.end_object.value, subscribe_out.end_object.value);
}
