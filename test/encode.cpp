#include <doctest/doctest.h>
#include <memory>
#include <quicr/quicr_client.h>

// TODO (suhas) : Can this be done better ?
#include "../src/encode.h"

using namespace quicr;
using namespace quicr::messages;

TEST_CASE("Subscribe Message encode/decode")
{
  QUICRNamespace qnamespace{ 0x1000, 0x2000, 3 };

  Subscribe s{ 1, 1, qnamespace, SubscribeIntent::immediate };
  MessageBuffer buffer;
  buffer << s;
  Subscribe s_out;
  buffer >> s_out;
  CHECK_EQ(s_out.intent, s.intent);
}