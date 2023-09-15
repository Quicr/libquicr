#include <doctest/doctest.h>

#include <qname>
#include <quicr/encode.h>
#include <quicr/message_buffer.h>
#include <quicr/quicr_common.h>

#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace quicr;
using namespace quicr::messages;

TEST_CASE("MessageBuffer Swap Bytes")
{
  uint16_t u16 = 0x1234u;
  uint32_t u32 = 0x12345678u;
  uint64_t u64 = 0x123456789ABCDEF0u;
  Name u128 = 0x123456789ABCDEF0123456789ABCDEF0_name;

  CHECK_NE(u16, swap_bytes(u16));
  CHECK_NE(u32, swap_bytes(u32));
  CHECK_NE(u64, swap_bytes(u64));
  CHECK_NE(u128, swap_bytes(u128));
}

TEST_CASE("MessageBuffer Decode Exception")
{
  uint8_t max = std::numeric_limits<uint8_t>::max();
  MessageBuffer buffer;
  buffer << max;
  uint64_t out;
  CHECK_THROWS((buffer >> out));
}

/*===========================================================================*/
// Subscribe Message Types
/*===========================================================================*/

TEST_CASE("Subscribe Message encode/decode")
{
  quicr::Namespace qnamespace{ 0x10000000000000002000_name, 128 };

  Subscribe s{ 1, 0x1000, qnamespace, SubscribeIntent::immediate };
  MessageBuffer buffer;
  buffer << s;
  Subscribe s_out;
  CHECK_NOTHROW((buffer >> s_out));

  CHECK_EQ(s_out.transaction_id, s.transaction_id);
  CHECK_EQ(s_out.quicr_namespace, s.quicr_namespace);
  CHECK_EQ(s_out.intent, s.intent);
}

TEST_CASE("SubscribeResponse Message encode/decode")
{
  quicr::Namespace qnamespace{ 0x10000000000000002000_name, 125 };

  SubscribeResponse s{ qnamespace,
                       SubscribeResult::SubscribeStatus::Ok,
                       0x1000 };
  MessageBuffer buffer;
  buffer << s;
  SubscribeResponse s_out;
  CHECK_NOTHROW((buffer >> s_out));
  CHECK_EQ(s_out.quicr_namespace, s.quicr_namespace);
  CHECK_EQ(s_out.response, s.response);
  CHECK_EQ(s_out.transaction_id, s.transaction_id);
}

TEST_CASE("SubscribeEnd Message encode/decode")
{
  quicr::Namespace qnamespace{ 0x10000000000000002000_name, 125 };

  SubscribeEnd s{ .quicr_namespace = qnamespace,
                  .reason = SubscribeResult::SubscribeStatus::Ok };

  MessageBuffer buffer;
  buffer << s;
  SubscribeEnd s_out;
  CHECK_NOTHROW((buffer >> s_out));

  CHECK_EQ(s_out.quicr_namespace, s.quicr_namespace);
  CHECK_EQ(s_out.reason, s.reason);
}

TEST_CASE("Unsubscribe Message encode/decode")
{
  quicr::Namespace qnamespace{ 0x10000000000000002000_name, 125 };

  Unsubscribe us{ .quicr_namespace = qnamespace };

  MessageBuffer buffer;
  buffer << us;
  Unsubscribe us_out;
  CHECK_NOTHROW((buffer >> us_out));

  CHECK_EQ(us_out.quicr_namespace, us.quicr_namespace);
}

/*===========================================================================*/
// Publish Message Types
/*===========================================================================*/

TEST_CASE("PublishIntent Message encode/decode")
{
  quicr::Namespace qnamespace{ 0x10000000000000002000_name, 125 };
  PublishIntent pi{ MessageType::Publish, 0x1000,
                    qnamespace,           { 0, 1, 2, 3, 4 },
                    uintVar_t{ 0x0100 },  uintVar_t{ 0x0000 } };
  MessageBuffer buffer;
  buffer << pi;
  PublishIntent pi_out;
  CHECK_NOTHROW((buffer >> pi_out));

  CHECK_EQ(pi_out.message_type, pi.message_type);
  CHECK_EQ(pi_out.transaction_id, pi.transaction_id);
  CHECK_EQ(pi_out.quicr_namespace, pi.quicr_namespace);
  CHECK_EQ(pi_out.payload, pi.payload);
  CHECK_EQ(pi_out.media_id, pi.media_id);
  CHECK_EQ(pi_out.datagram_capable, pi.datagram_capable);
}

TEST_CASE("PublishIntentResponse Message encode/decode")
{
  PublishIntentResponse pir{ MessageType::Publish, {}, Response::Ok, 0x1000 };
  MessageBuffer buffer;
  buffer << pir;
  PublishIntentResponse pir_out;
  CHECK_NOTHROW((buffer >> pir_out));

  CHECK_EQ(pir_out.message_type, pir.message_type);
  CHECK_EQ(pir_out.quicr_namespace, pir.quicr_namespace);
  CHECK_EQ(pir_out.response, pir.response);
  CHECK_EQ(pir_out.transaction_id, pir.transaction_id);
}

TEST_CASE("Publish Message encode/decode")
{
  quicr::Name qn = 0x10000000000000002000_name;
  Header d{ uintVar_t{ 0x1000 }, qn,
            uintVar_t{ 0x0100 }, uintVar_t{ 0x0010 },
            uintVar_t{ 0x0001 }, 0x0000 };

  std::vector<uint8_t> data(256);
  for (int i = 0; i < 256; ++i)
    data[i] = i;

  PublishDatagram p{ d, MediaType::Text, uintVar_t{ 256 }, data };
  MessageBuffer buffer;
  buffer << p;
  PublishDatagram p_out;
  CHECK_NOTHROW((buffer >> p_out));

  CHECK_EQ(p_out.header.media_id, p.header.media_id);
  CHECK_EQ(p_out.header.name, p.header.name);
  CHECK_EQ(p_out.header.group_id, p.header.group_id);
  CHECK_EQ(p_out.header.object_id, p.header.object_id);
  CHECK_EQ(p_out.header.offset_and_fin, p.header.offset_and_fin);
  CHECK_EQ(p_out.header.flags, p.header.flags);
  CHECK_EQ(p_out.media_type, p.media_type);
  CHECK_EQ(p_out.media_data_length, p.media_data_length);
  CHECK_EQ(p_out.media_data, p.media_data);
  CHECK_EQ(p_out.media_data, data);
}

TEST_CASE("PublishStream Message encode/decode")
{
  PublishStream ps{ uintVar_t{ 5 }, { 0, 1, 2, 3, 4 } };
  MessageBuffer buffer;
  buffer << ps;
  PublishStream ps_out;
  CHECK_NOTHROW((buffer >> ps_out));

  CHECK_EQ(ps_out.media_data_length, ps.media_data_length);
  CHECK_EQ(ps_out.media_data, ps.media_data);
}

TEST_CASE("PublishIntentEnd Message encode/decode")
{
  PublishIntentEnd pie{ MessageType::Publish,
                        { 12345_name, 0u },
                        { 0, 1, 2, 3, 4 } };
  MessageBuffer buffer;
  buffer << pie;
  PublishIntentEnd pie_out;
  CHECK_NOTHROW((buffer >> pie_out));

  CHECK_EQ(pie_out.message_type, pie.message_type);
  CHECK_EQ(pie_out.quicr_namespace, pie.quicr_namespace);
  CHECK_EQ(pie_out.payload, pie.payload);
}

TEST_CASE("VarInt Encode/Decode")
{
  constexpr uintVar_t in{ 56u };
  (void)in;
  std::vector<uintVar_t> values = {
    56_uV, 127_uV, 128_uV, 16384_uV, 536870912_uV
  };
  std::vector<uintVar_t> out_values(values.size());
  std::vector<size_t> sizes = { 1, 1, 2, 4, 8 };

  int i = 0;
  for (const auto& value : values) {
    MessageBuffer buffer;
    CHECK_NOTHROW(buffer << value);
    REQUIRE_EQ(buffer.size(), sizes[i]);
    CHECK_NOTHROW(buffer >> out_values[i++]);
  }

  CHECK_EQ(out_values[0], values[0]);
  CHECK_EQ(out_values[1], values[1]);
  CHECK_EQ(out_values[2], values[2]);
  CHECK_EQ(out_values[3], values[3]);
  CHECK_EQ(out_values[4], values[4]);
}

/*===========================================================================*/
// Fetch Tests
/*===========================================================================*/

TEST_CASE("Fetch Message encode/decode")
{
  Fetch f{ 0x1000, 0x10000000000000002000_name };
  MessageBuffer buffer;
  buffer << f;
  Fetch fout;
  CHECK_NOTHROW((buffer >> fout));

  CHECK_EQ(fout.transaction_id, f.transaction_id);
  CHECK_EQ(fout.name, f.name);
}