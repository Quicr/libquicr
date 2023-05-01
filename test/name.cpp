#include <doctest/doctest.h>

#include <quicr/quicr_name.h>

#include <type_traits>

TEST_CASE("quicr::Name Constructor Tests")
{
  constexpr quicr::Name val42(0x42_name);
  constexpr quicr::Name hex42(0x42_name);
  CHECK_EQ(val42, hex42);

  CHECK_LT(0x123_name, 0x124_name);
  CHECK_GT(0x123_name, 0x122_name);
  CHECK_NE(0x123_name, 0x122_name);

  CHECK_GT(0x20000000000000001_name, 0x10000000000000002_name);
  CHECK_LT(0x10000000000000002_name, 0x20000000000000001_name);

  CHECK_NOTHROW(0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_name);
  CHECK_THROWS(0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF0_name);

  CHECK(std::is_trivial_v<quicr::Name>);
  CHECK(std::is_trivially_constructible_v<quicr::Name>);
  CHECK(std::is_trivially_default_constructible_v<quicr::Name>);
  CHECK(std::is_trivially_destructible_v<quicr::Name>);
  CHECK(std::is_trivially_copyable_v<quicr::Name>);
  CHECK(std::is_trivially_copy_assignable_v<quicr::Name>);
  CHECK(std::is_trivially_move_constructible_v<quicr::Name>);
  CHECK(std::is_trivially_move_assignable_v<quicr::Name>);
}

TEST_CASE("quicr::Name To Hex Tests")
{
  {
    std::string_view original_hex = "0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";
    quicr::Name name = original_hex;

    CHECK_EQ(name.to_hex(), original_hex);
  }
  {
    std::string_view original_hex = "0xFFFFFFFFFFFFFFFF0000000000000000";
    quicr::Name name = original_hex;

    CHECK_EQ(name.to_hex(), original_hex);
  }
  {
    std::string_view long_hex = "0x0000000000000000FFFFFFFFFFFFFFFF";
    quicr::Name long_name = long_hex;

    std::string_view short_hex = "0xFFFFFFFFFFFFFFFF";
    quicr::Name not_short_name = short_hex;
    CHECK_EQ(long_name.to_hex(), long_hex);
    CHECK_NE(not_short_name.to_hex(), short_hex);
    CHECK_EQ(long_name.to_hex(), not_short_name.to_hex());
    CHECK_EQ(long_name, not_short_name);
  }
}

TEST_CASE("quicr::Name Bit Shifting Tests")
{
  CHECK_EQ((0x1234_name >> 4), 0x123_name);
  CHECK_EQ((0x1234_name << 4), 0x12340_name);

  {
    const quicr::Name unshifted_32bit = 0x123456789abcdeff00000000_name;
    const quicr::Name shifted_32bit = 0x123456789abcdeff_name;
    CHECK_EQ((unshifted_32bit >> 32), shifted_32bit);
    CHECK_EQ((shifted_32bit << 32), unshifted_32bit);
  }

  {
    quicr::Name unshifted_64bit = 0x123456789abcdeff123456789abcdeff_name;
    quicr::Name shifted_64bit = 0x123456789abcdeff_name;
    quicr::Name shifted_72bit = 0x123456789abcde_name;
    CHECK_EQ((unshifted_64bit >> 64), shifted_64bit);
    CHECK_EQ((unshifted_64bit >> 72), shifted_72bit);
    CHECK_EQ((shifted_64bit >> 8), shifted_72bit);
  }

  {
    quicr::Name unshifted_64bit = 0x123456789abcdeff_name;
    quicr::Name shifted_64bit = 0x123456789abcdeff0000000000000000_name;
    quicr::Name shifted_72bit = 0x3456789abcdeff000000000000000000_name;
    CHECK_EQ((unshifted_64bit << 64), shifted_64bit);
    CHECK_EQ((unshifted_64bit << 72), shifted_72bit);
    CHECK_EQ((shifted_64bit << 8), shifted_72bit);
  }

  {
    const quicr::Name unshifted_bits = 0x00000000000000000000000000000001_name;
    quicr::Name bits = unshifted_bits;
    for (int i = 0; i < 64; ++i)
      bits <<= 1;

    CHECK_EQ(bits, 0x00000000000000010000000000000000_name);

    for (int i = 0; i < 64; ++i)
      bits >>= 1;

    CHECK_EQ(bits, unshifted_bits);
  }
}

TEST_CASE("quicr::Name Arithmetic Tests")
{
  quicr::Name val42 = 0x42_name;
  quicr::Name val41 = 0x41_name;
  quicr::Name val43 = 0x43_name;
  CHECK_EQ(val42 + 1, val43);
  CHECK_EQ(val42 - 1, val41);

  CHECK_EQ(0x00000000000000010000000000000000_name + 1,
           0x00000000000000010000000000000001_name);
  CHECK_EQ(0x0FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_name + 1,
           0x10000000000000000000000000000000_name);
  CHECK_EQ(0x0000000000000000FFFFFFFFFFFFFFFF_name + 0xFFFFFFFF,
           0x000000000000000100000000FFFFFFFE_name);

  CHECK_EQ(0x00000000000000010000000000000000_name - 1,
           0x0000000000000000FFFFFFFFFFFFFFFF_name);
  CHECK_EQ(0x0FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_name - 1,
           0x0FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE_name);
  CHECK_EQ(0x0000000000000000FFFFFFFFFFFFFFFF_name - 0xFFFFFFFFFFFFFFFF,
           0x00000000000000000000000000000000_name);
  CHECK_EQ(0x00000000000000010000000000000000_name - 2,
           0x0000000000000000FFFFFFFFFFFFFFFE_name);

  quicr::Name val42_copy(val42);
  CHECK_EQ(val42_copy, val42);
  CHECK_NE(val42_copy++, val43);
  CHECK_EQ(val42_copy, val43);
  CHECK_NE(val42_copy--, val42);
  CHECK_EQ(val42_copy, val42);
  CHECK_EQ(++val42_copy, val43);
  CHECK_EQ(--val42_copy, val42);
}

TEST_CASE("quicr::Name Bitwise Not Tests")
{
  constexpr quicr::Name zeros = 0x0_name;
  constexpr quicr::Name ones = ~zeros;

  quicr::Name expected_ones = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_name;
  auto literal_ones = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_name;

  CHECK_NE(ones, zeros);
  CHECK_EQ(ones, expected_ones);
  CHECK_EQ(literal_ones, expected_ones);
}

TEST_CASE("quicr::Name Byte Array Tests")
{
  std::vector<uint8_t> byte_arr(sizeof(quicr::Name));
  for (size_t i = 0; i < sizeof(quicr::Name) / 2; ++i) {
    byte_arr[i] = static_cast<uint8_t>(0x0);
    byte_arr[i + sizeof(quicr::Name) / 2] =
      static_cast<uint8_t>((0x1000000000000000 >> 8 * i));
  }

  CHECK_FALSE(byte_arr.empty());
  CHECK_EQ(byte_arr.size(), 16);

  quicr::Name name_to_bytes = 0x10000000000000000000000000000000_name;
  quicr::Name name_from_bytes(byte_arr);
  CHECK_EQ(name_from_bytes, name_to_bytes);

  quicr::Name name_from_byte_ptr(byte_arr.data(), byte_arr.size());
  CHECK_EQ(name_from_byte_ptr, name_to_bytes);
}

TEST_CASE("quicr::Name Logical Arithmetic Tests")
{
  auto arith_and = 0x01010101010101010101010101010101_name &
                   0x10101010101010101010101010101010_name;
  CHECK_EQ(arith_and, 0x0_name);

  auto arith_and2 = 0x0101010101010101_name & 0x1010101010101010;
  CHECK_EQ(arith_and2, 0x0_name);

  auto arith_or = 0x01010101010101010101010101010101_name |
                  0x10101010101010101010101010101010_name;
  CHECK_EQ(arith_or, 0x11111111111111111111111111111111_name);

  auto arith_or2 = 0x0101010101010101_name | 0x1010101010101010;
  CHECK_EQ(arith_or2, 0x1111111111111111_name);
}
