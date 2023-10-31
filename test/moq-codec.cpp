#include <doctest/doctest.h>

#include <qname>

#include <quicr/message_buffer.h>
#include <quicr/quicr_common.h>
#include <quicr/moq_message_types.h>

#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace quicr;
using namespace quicr::messages;


const FullTrackName ftn_alice_audio = {
  .track_namespace  = "moqt://example.com/conference123",
  .track_name = "alice/audio"
};


const FullTrackName ftn_alice_video = {
  .track_namespace  = "moqt://example.com/conference123",
  .track_name = "alice/video"
};

TEST_CASE("FullTrackName encode/decode") {
  FullTrackName ftn = {"moq://go.webex.com/meeting123", "alice/audio"};

  MessageBuffer buffer;
  buffer << ftn;
  FullTrackName out;
  buffer >> out;
  CHECK_EQ(out.track_namespace, ftn.track_namespace);
  CHECK_EQ(out.track_name, ftn.track_name);
}


TEST_CASE("Location encode/decode") {
  Location loc = {0x1, 0x1000};
  MessageBuffer buffer;
  buffer << loc;
  Location out;
  buffer >> out;
  CHECK_EQ(out.mode, loc.mode);
  CHECK_EQ(out.value, loc.value);
}


TEST_CASE("MoqSubscribe encode/decode") {
  MoqSubscribe in = {
    .track = ftn_alice_audio,
    .start_group = Location { LocationMode::Absolute, 0x1000},
    .start_object = Location {LocationMode::RelativeNext, 0},
    .end_group = Location {LocationMode::Absolute, 0x2000},
    .end_object = Location {LocationMode::RelativeNext, 0}
  };
  MessageBuffer buffer;
  buffer << in;
  MoqSubscribe out;
  buffer >> out;
  CHECK_EQ(out.track, in.track);
  CHECK_EQ(out.start_group, in.start_group);
  CHECK_EQ(out.start_object, in.start_object);
  CHECK_EQ(out.end_group, in.end_group);
  CHECK_EQ(out.end_object, in.end_object);
}

TEST_CASE("MoqSubscribeOk encode/decode") {
  MoqSubscribeOk in = {
    .track = ftn_alice_audio,
    .track_id = 0x1000,
    .expires = 0
  };

  MessageBuffer buffer;
  buffer << in;
  MoqSubscribeOk out;
  buffer >> out;
  CHECK_EQ(out.track, in.track);
  CHECK_EQ(out.track_id, in.track_id);
  CHECK_EQ(out.expires, in.expires);
}

TEST_CASE("MoqSubscribeError encode/decode") {
  MoqSubscribeError in = {
    .track = ftn_alice_audio,
    .err_code = 0,
    .reason_phrase = ""
  };

  MessageBuffer buffer;
  buffer << in;
  MoqSubscribeError out;
  buffer >> out;
  CHECK_EQ(out.track, in.track);
  CHECK_EQ(out.err_code, in.err_code);
  CHECK_EQ(out.reason_phrase, in.reason_phrase);
}

TEST_CASE("MoqSubscribeFin encode/decode") {
  MoqSubscribeFin in = {
    .track = ftn_alice_audio,
    .final_group = 0x1000,
    .final_object = 0x9000
  };

  MessageBuffer buffer;
  buffer << in;
  MoqSubscribeFin out;
  buffer >> out;
  CHECK_EQ(out.track, in.track);
  CHECK_EQ(out.final_group, in.final_group);
  CHECK_EQ(out.final_object, in.final_object);
}

TEST_CASE("MoqSubscribeRst encode/decode") {
  MoqSubscribeRst in = {
    .track = ftn_alice_audio,
    .err_code = 0x1000,
    .reason_phrase = "Publisher Vanished",
    .final_group = 0x1000,
    .final_object = 0x9000,
  };

  MessageBuffer buffer;
  buffer << in;
  MoqSubscribeRst out;
  buffer >> out;
  CHECK_EQ(out.track, in.track);
  CHECK_EQ(out.err_code, in.err_code);
  CHECK_EQ(out.reason_phrase, in.reason_phrase);
  CHECK_EQ(out.final_group, in.final_group);
  CHECK_EQ(out.final_object, in.final_object);
}


TEST_CASE("MoqAnnounce encode/decode") {
  MoqAnnounce in = {
    .track_namespace = ftn_alice_video.track_namespace,
    .parameters = {},
  };

  MessageBuffer buffer;
  buffer << in;
  MoqAnnounce out;
  buffer >> out;
  CHECK_EQ(out.track_namespace, in.track_namespace);
  CHECK_EQ(out.parameters.size(), in.parameters.size());
}

TEST_CASE("MoqAnnounceOk encode/decode") {
  MoqAnnounceOk in = {
    .track_namespace = ftn_alice_video.track_namespace,
  };

  MessageBuffer buffer;
  buffer << in;
  MoqAnnounceOk out;
  buffer >> out;
  CHECK_EQ(out.track_namespace, in.track_namespace);
}

TEST_CASE("MoqAnnounceError encode/decode") {
  MoqAnnounceError in = {
    .track_namespace = ftn_alice_video.track_namespace,
    .err_code = 0x1000,
    .reason_phrase = "Announce what ?"
  };

  MessageBuffer buffer;
  buffer << in;
  MoqAnnounceError out;
  buffer >> out;
  CHECK_EQ(out.track_namespace, in.track_namespace);
  CHECK_EQ(out.err_code, in.err_code);
  CHECK_EQ(out.reason_phrase, in.reason_phrase);
}


TEST_CASE("MoqUnnnounce encode/decode") {
  MoqUnannounce in = {
    .track_namespace = ftn_alice_video.track_namespace,
  };

  MessageBuffer buffer;
  buffer << in;
  MoqUnannounce out;
  buffer >> out;
  CHECK_EQ(out.track_namespace, in.track_namespace);
}


TEST_CASE("Client Setup encode/decode") {
  ClientSetup in = {
    .supported_versions = {Version(0x0001), Version(0x002)},
    .parameters = {},
  };

  MessageBuffer buffer;
  buffer << in;
  ClientSetup out;
  buffer >> out;

  CHECK_EQ(out.supported_versions.size(), in.supported_versions.size());
  CHECK_EQ(out.supported_versions, in.supported_versions);
  CHECK_EQ(out.parameters.size(), in.parameters.size());
}



