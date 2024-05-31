#include <doctest/doctest.h>
#include <memory>
#include <sstream>
#include <string>
#include <iostream>
#include <quicr/moq_messages.h>

using namespace quicr;
using namespace quicr::messages;

static bytes from_ascii(const std::string& ascii)
{
  return std::vector<uint8_t>(ascii.begin(), ascii.end());
}

const quicr::bytes TRACK_NAMESPACE_CONF = from_ascii("moqt://conf.example.com/conf/1");
const quicr::bytes TRACK_NAME_ALICE_VIDEO = from_ascii("alice/video");
const uintVar_t TRACK_ALIAS_ALICE_VIDEO = 0xA11CE;

TEST_CASE("AnnounceOk Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto announce_ok  = MoqAnnounceOk {};
  announce_ok.track_namespace = TRACK_NAMESPACE_CONF;

  buffer <<  announce_ok;

  auto message_type = buffer.decode_uintV();
  CHECK_EQ(*message_type, static_cast<uint64_t>(MESSAGE_TYPE_ANNOUNCE_OK));

  MoqAnnounceOk announce_ok_out;
  buffer >> announce_ok_out;
  CHECK_EQ(TRACK_NAMESPACE_CONF, announce_ok_out.track_namespace);
}

TEST_CASE("Announce Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;
  std::vector<uint8_t> net_data;

  auto announce  = MoqAnnounce {};
  announce.track_namespace = TRACK_NAMESPACE_CONF;
  announce.params = {};

  buffer <<  announce;

  auto message_type = buffer.decode_uintV();
  CHECK_EQ(*message_type, static_cast<uint64_t>(MESSAGE_TYPE_ANNOUNCE));

  MoqAnnounce announce_out;
  buffer >> announce_out;
  CHECK_EQ(TRACK_NAMESPACE_CONF, announce_out.track_namespace);
  CHECK_EQ(0, announce_out.params.size());
}