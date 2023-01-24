#include <doctest/doctest.h>
#include <memory>

// TODO (suhas) : Can this be done better ?
#include "../src/encode.h"

using namespace quicr;
using namespace quicr::messages;

TEST_CASE("Subscribe Message encode/decode")
{
  QUICRNamespace qnamespace{ 0x1000, 0x2000, 3 };

  Subscribe s{ 1, 0x1000, qnamespace, SubscribeIntent::immediate };
  MessageBuffer buffer;
  buffer << s;
  Subscribe s_out;
  CHECK((buffer >> s_out));

  CHECK_EQ(s_out.transaction_id, s.transaction_id);
  CHECK_EQ(s_out.quicr_namespace.mask, s.quicr_namespace.mask);
  CHECK_EQ(s_out.quicr_namespace.low, s.quicr_namespace.low);
  CHECK_EQ(s_out.quicr_namespace.hi, s.quicr_namespace.hi);
  CHECK_EQ(s_out.intent, s.intent);
}

TEST_CASE("SubscribeResponse Message encode/decode")
{
  SubscribeResponse s{
    MessageType::Unknown, Response::Ok, 0x1000, uintVar_t{ 0x0100 }
  };
  MessageBuffer buffer;
  buffer << s;
  SubscribeResponse s_out;
  CHECK((buffer >> s_out));

  CHECK_EQ(s_out.message_type, s.message_type);
  CHECK_EQ(s_out.response, s.response);
  CHECK_EQ(s_out.transaction_id, s.transaction_id);
  CHECK_EQ(s_out.media_id, s.media_id);
}

TEST_CASE("SubscribeEnd Message encode/decode")
{
  SubscribeEnd s{ MessageType::Unknown,
                  uintVar_t{ 0x1000 },
                  { 1, 2, 3, 4, 5 } };
  MessageBuffer buffer;
  buffer << s;
  SubscribeEnd s_out;
  CHECK((buffer >> s_out));

  CHECK_EQ(s_out.message_type, s.message_type);
  CHECK_EQ(s_out.media_id, s.media_id);
  CHECK_EQ(s_out.payload, s.payload);
}

TEST_CASE("Publish Message encode/decode")
{
  Header d{ uintVar_t{ 0x1000 },
            uintVar_t{ 0x0100 },
            uintVar_t{ 0x0010 },
            uintVar_t{ 0x0001 },
            0x0000 };
  PublishDatagram p{ d, MediaType::Text, uintVar_t{ 5 }, { 1, 2, 3, 4, 5 } };
  MessageBuffer buffer;
  buffer << p;
  PublishDatagram p_out;
  CHECK((buffer >> p_out));

  CHECK_EQ(p_out.header.media_id, p.header.media_id);
  CHECK_EQ(p_out.header.group_id, p.header.group_id);
  CHECK_EQ(p_out.header.object_id, p.header.object_id);
  CHECK_EQ(p_out.header.offset_and_fin, p.header.offset_and_fin);
  CHECK_EQ(p_out.header.flags, p.header.flags);
  CHECK_EQ(p_out.media_type, p.media_type);
  CHECK_EQ(p_out.media_data_length, p.media_data_length);
  CHECK_EQ(p_out.media_data, p.media_data);
}