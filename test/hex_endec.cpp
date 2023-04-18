#include <doctest/doctest.h>

#include <quicr/hex_endec.h>

TEST_CASE("quicr::HexEndec 256bit Encode/Decode Test")
{
  const std::string hex_value =
    "0x1111111111111111222222222222222233333333333333334444444444444400";
  const uint64_t first_part = 0x1111111111111111ull;
  const uint64_t second_part = 0x2222222222222222ull;
  const uint64_t third_part = 0x3333333333333333ull;
  const uint64_t fourth_part = 0x44444444444444ull;
  const uint8_t last_part = 0x00ull;

  quicr::HexEndec<256, 64, 64, 64, 56, 8> formatter_256bit;
  const std::string mask = formatter_256bit.Encode(
    first_part, second_part, third_part, fourth_part, last_part);
  CHECK_EQ(mask, hex_value);

  const auto [one, two, three, four, last] =
    formatter_256bit.Decode(std::string_view(hex_value));
  CHECK_EQ(one, first_part);
  CHECK_EQ(two, second_part);
  CHECK_EQ(three, third_part);
  CHECK_EQ(four, fourth_part);
  CHECK_EQ(last, last_part);
}

TEST_CASE("quicr::HexEndec 128bit Encode/Decode Test")
{
  const std::string hex_value = "0x11111111111111112222222222222200";
  const uint64_t first_part = 0x1111111111111111ull;
  const uint64_t second_part = 0x22222222222222ull;
  const uint8_t third_part = 0x00ull;

  quicr::HexEndec<128, 64, 56, 8> formatter_128bit;
  const std::string mask =
    formatter_128bit.Encode(first_part, second_part, third_part);
  CHECK_EQ(mask, hex_value);

  const auto [one, two, three] =
    formatter_128bit.Decode(std::string_view(hex_value));
  CHECK_EQ(one, first_part);
  CHECK_EQ(two, second_part);
  CHECK_EQ(three, third_part);
}

TEST_CASE("quicr::HexEndec 128bit Encode/Decode Container Test")
{
  const std::string_view hex_value = "0x11111111111111112222222222222200";
  const uint64_t first_part = 0x1111111111111111ull;
  const uint64_t second_part = 0x22222222222222ull;
  const uint8_t third_part = 0x00ull;

  std::array<uint8_t, 3> dist = { 64, 56, 8 };
  std::array<uint64_t, 3> vals = { first_part, second_part, third_part };
  const std::string mask = quicr::HexEndec<128>::Encode(dist, vals);
  CHECK_EQ(mask, hex_value);

  std::vector<uint64_t> out = quicr::HexEndec<128>::Decode(dist, hex_value);
  CHECK_EQ(out[0], first_part);
  CHECK_EQ(out[1], second_part);
  CHECK_EQ(out[2], third_part);
}

TEST_CASE("quicr::HexEndec 64bit Encode/Decode Test")
{
  const std::string_view hex_value = "0x1111111122222200";
  const uint64_t first_part = 0x11111111ull;
  const uint64_t second_part = 0x222222ull;
  const uint8_t third_part = 0x00ull;

  quicr::HexEndec<64, 32, 24, 8> formatter_64bit;
  const std::string mask =
    formatter_64bit.Encode(first_part, second_part, third_part);
  CHECK_EQ(mask, hex_value);

  const auto [one, two, three] = formatter_64bit.Decode(hex_value);
  CHECK_EQ(one, first_part);
  CHECK_EQ(two, second_part);
  CHECK_EQ(three, third_part);
}

TEST_CASE("quicr::HexEndec Decode Throw Test")
{
  const std::string_view valid_hex_value = "0x11111111111111112222222222222200";
  const std::string_view invalid_hex_value = "0x111111111111111122222222222222";
  const std::string_view another_invalid_hex_value =
    "0x1111111111111111222222222222220000";

  quicr::HexEndec<128, 64, 56, 8> formatter_128bit;
  CHECK_NOTHROW(formatter_128bit.Decode(valid_hex_value));
  CHECK_THROWS(formatter_128bit.Decode(invalid_hex_value));
  CHECK_THROWS(formatter_128bit.Decode(another_invalid_hex_value));
}
