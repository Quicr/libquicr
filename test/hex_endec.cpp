#include <doctest/doctest.h>

#include <quicr/hex_endec.h>

TEST_CASE("quicr::HexEndec Encode/Decode Test")
{
  const std::string hex_value = "0x11111111111111112222222222222200";
  const uint64_t first_part = 0x1111111111111111ull;
  const uint64_t second_part = 0x22222222222222ull;
  const uint8_t third_part = 0x00ull;

  quicr::HexEndec<128, 64, 56, 8> formatter_128bit;
  const std::string mask =
    formatter_128bit.Encode(first_part, second_part, third_part);
  CHECK_EQ(mask, hex_value);

  const auto [one, two, three] = formatter_128bit.Decode(hex_value);
  CHECK_EQ(one, first_part);
  CHECK_EQ(two, second_part);
  CHECK_EQ(three, third_part);
}

TEST_CASE("quicr::HexEndec Decode Throw Test")
{
  const std::string valid_hex_value = "0x11111111111111112222222222222200";
  const std::string invalid_hex_value = "0x111111111111111122222222222222";
  const std::string another_invalid_hex_value =
    "0x1111111111111111222222222222220000";

  quicr::HexEndec<128, 64, 56, 8> formatter_128bit;
  CHECK_NOTHROW(formatter_128bit.Decode(valid_hex_value));
  CHECK_THROWS(formatter_128bit.Decode(invalid_hex_value));
  CHECK_THROWS(formatter_128bit.Decode(another_invalid_hex_value));
}
