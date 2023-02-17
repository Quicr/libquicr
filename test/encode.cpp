#include <doctest/doctest.h>
#include <memory>

#include <quicr/encode.h>

using namespace quicr;
using namespace quicr::messages;

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
  quicr::Namespace qnamespace{ { "0x10000000000000002000" }, 125 };

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
  quicr::Namespace qnamespace{ { "0x10000000000000002000" }, 125 };

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
  SubscribeEnd s{ MessageType::Unknown,
                  uintVar_t{ 0x1000 },
                  { 1, 2, 3, 4, 5 } };
  MessageBuffer buffer;
  buffer << s;
  SubscribeEnd s_out;
  CHECK_NOTHROW((buffer >> s_out));

  CHECK_EQ(s_out.message_type, s.message_type);
  CHECK_EQ(s_out.media_id, s.media_id);
  CHECK_EQ(s_out.payload, s.payload);
}

/*===========================================================================*/
// Publish Message Types
/*===========================================================================*/

TEST_CASE("PublishIntent Message encode/decode")
{
  quicr::Namespace qnamespace{ { "0x10000000000000002000" }, 125 };
  PublishIntent pi{ MessageType::Publish, 0x1000,
                    qnamespace,           1u,
                    { 0, 1, 2, 3, 4 },    uintVar_t{ 0x0100 },
                    uintVar_t{ 0x0000 } };
  MessageBuffer buffer;
  buffer << pi;
  PublishIntent pi_out;
  CHECK_NOTHROW((buffer >> pi_out));

  CHECK_EQ(pi_out.message_type, pi.message_type);
  CHECK_EQ(pi_out.transaction_id, pi.transaction_id);
  CHECK_EQ(pi_out.quicr_namespace, pi.quicr_namespace);
  CHECK_EQ(pi_out.mask, pi.mask);
  CHECK_EQ(pi_out.payload, pi.payload);
  CHECK_EQ(pi_out.media_id, pi.media_id);
  CHECK_EQ(pi_out.datagram_capable, pi.datagram_capable);
}

TEST_CASE("PublishIntentResponse Message encode/decode")
{
  PublishIntentResponse pir{ MessageType::Publish, Response::Ok, 0x1000 };
  MessageBuffer buffer;
  buffer << pir;
  PublishIntentResponse pir_out;
  CHECK_NOTHROW((buffer >> pir_out));

  CHECK_EQ(pir_out.message_type, pir.message_type);
  CHECK_EQ(pir_out.response, pir.response);
  CHECK_EQ(pir_out.transaction_id, pir.transaction_id);
}

TEST_CASE("Publish Message encode/decode")
{
  quicr::Name qn{ "0x10000000000000002000" };
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
  const std::string name = "12345";
  PublishIntentEnd pie{ MessageType::Publish,
                        uintVar_t{ 5 },
                        { name.begin(), name.end() },
                        { 0, 1, 2, 3, 4 } };
  MessageBuffer buffer;
  buffer << pie;
  PublishIntentEnd pie_out;
  CHECK_NOTHROW((buffer >> pie_out));

  CHECK_EQ(pie_out.message_type, pie.message_type);
  CHECK_EQ(pie_out.name_length, pie.name_length);
  CHECK_EQ(pie_out.name, pie.name);
  CHECK_EQ(pie_out.payload, pie.payload);
}

TEST_CASE("VarInt Encode/Decode")
{
  MessageBuffer buffer;
  std::vector<uintVar_t> values = { uintVar_t{128}, uintVar_t{16384}, uintVar_t{536870912} };
  for (const auto& value : values)
  {
    buffer << value;
    uintVar_t out;
    buffer >> out;

    CHECK_NE(out, uintVar_t{0});
  }
}
