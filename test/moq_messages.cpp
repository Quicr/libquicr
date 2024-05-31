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

  buffer << announce_ok;

  std::vector<uint8_t> net_data = buffer.front(buffer.size());

  // ingress buffer
  qtransport::StreamBuffer<uint8_t> in_buffer;
  std::optional<uint64_t> message_type;

  MoqAnnounceOk announce_ok_out;
  for (auto& v: net_data) {
    in_buffer.push(v);
    if (!message_type) {
      message_type = in_buffer.decode_uintV();
      if (!message_type) {
        continue;
      }
      CHECK_EQ(*message_type, static_cast<uint64_t>(MESSAGE_TYPE_ANNOUNCE_OK));
      continue;
    }
    bool got = in_buffer >> announce_ok_out;
    if (!got) {
      continue;
    }
  }

  CHECK_EQ(TRACK_NAMESPACE_CONF, announce_ok_out.track_namespace);
}

TEST_CASE("Announce Message encode/decode")
{
  qtransport::StreamBuffer<uint8_t> buffer;

  auto announce  = MoqAnnounce {};
  announce.track_namespace = TRACK_NAMESPACE_CONF;
  announce.params = {};
  buffer <<  announce;
  std::vector<uint8_t> net_data = buffer.front(buffer.size());

  // ingress buffer
  qtransport::StreamBuffer<uint8_t> in_buffer;
  std::optional<uint64_t> message_type;

  MoqAnnounce announce_out;
  for (auto& v: net_data) {
    in_buffer.push(v);
    if (!message_type) {
      message_type = in_buffer.decode_uintV();
      if(!message_type) {
        continue;
      }
      CHECK_EQ(*message_type, static_cast<uint64_t>(MESSAGE_TYPE_ANNOUNCE));
      continue;
    }

    bool got = in_buffer >> announce_out;
    if (!got) {
      continue;
    }
  }

  CHECK_EQ(TRACK_NAMESPACE_CONF, announce_out.track_namespace);
  CHECK_EQ(0, announce_out.params.size());
}
