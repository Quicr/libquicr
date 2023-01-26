#include <doctest/doctest.h>
#include <map>
#include <quicr/quicr_common.h>

#include <quicr/quicr_name.h>
#include <quicr/quicr_namespace.h>

TEST_CASE("quicr::Name Constructor Tests")
{
  quicr::Name val42("0x42");
  quicr::Name hex42("0x42");
  CHECK_EQ(val42, hex42);

  CHECK_LT(quicr::Name("0x123"), quicr::Name("0x124"));
  CHECK_GT(quicr::Name("0x123"), quicr::Name("0x122"));
  CHECK_NE(quicr::Name("0x123"), quicr::Name("0x122"));

  CHECK_NOTHROW(quicr::Name("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"));
  CHECK_THROWS(quicr::Name("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF0"));
}

TEST_CASE("quicr::Name Bit Shifting Tests")
{
  CHECK_EQ((quicr::Name("0x1234") >> 4), quicr::Name("0x123"));
  CHECK_EQ((quicr::Name("0x1234") << 4), quicr::Name("0x12340"));
  CHECK_EQ((quicr::Name("0x0123456789abcdef0123456789abcdef") >> 64),
           quicr::Name("0x123456789abcdef"));
}

TEST_CASE("quicr::Name Arithmetic Tests")
{
  quicr::Name val42("0x42");
  quicr::Name val41("0x41");
  quicr::Name val43("0x43");
  CHECK_EQ(val42 + 1, val43);
  CHECK_EQ(val42 - 1, val41);
  CHECK_EQ(quicr::Name("0x0FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF") + 1,
           quicr::Name("0x10000000000000000000000000000000"));

  quicr::Name val42_copy(val42);
  CHECK_NE(val42_copy++, val43);
  CHECK_EQ(val42_copy, val43);
  CHECK_NE(val42_copy--, val42);
  CHECK_EQ(val42_copy, val42);
  CHECK_EQ(++val42_copy, val43);
  CHECK_EQ(--val42_copy, val42);
}

TEST_CASE("quicr::Name Byte Array Tests")
{
  auto make_bytes = [](quicr::Name::uint_type v) {
    std::vector<uint8_t> result(sizeof(quicr::Name::uint_type));
    for (size_t i = 0; i < sizeof(quicr::Name::uint_type); ++i) {
      result[i] = static_cast<uint8_t>((v >> 8 * i));
    }
    return result;
  };

  auto byte_arr = make_bytes(0x1000000000000000);
  auto low_byte_arr = make_bytes(0x0000000000000000);
  byte_arr.insert(byte_arr.end(), low_byte_arr.begin(), low_byte_arr.end());

  CHECK_FALSE(byte_arr.empty());
  CHECK_EQ(byte_arr.size(), 16);
  
  quicr::Name name_to_bytes("0x10000000000000000000000000000000");
  quicr::Name name_from_bytes(byte_arr);
  CHECK_EQ(name_from_bytes, name_to_bytes);

  quicr::Name name_from_byte_ptr(byte_arr.data(), byte_arr.size());
  CHECK_EQ(name_from_byte_ptr, name_to_bytes);
}

TEST_CASE("quicr::Name Logical Arithmetic Tests")
{
  auto arith_and = quicr::Name("0x01010101010101010101010101010101") &
                   quicr::Name("0x10101010101010101010101010101010");
  CHECK_EQ(arith_and, quicr::Name("0x0"));

  auto arith_and2 = quicr::Name("0x0101010101010101") & 0x1010101010101010;
  CHECK_EQ(arith_and2, quicr::Name("0x0"));

  auto arith_or = quicr::Name("0x01010101010101010101010101010101") |
                  quicr::Name("0x10101010101010101010101010101010");
  CHECK_EQ(arith_or, quicr::Name("0x11111111111111111111111111111111"));

  auto arith_or2 = quicr::Name("0x0101010101010101") | 0x1010101010101010;
  CHECK_EQ(arith_or2, quicr::Name("0x1111111111111111"));
}

TEST_CASE("quicr::Namespace Contains Names Test")
{
  quicr::Namespace ns({ "0x11111111111111112222222222222200" }, 120);

  quicr::Name valid_name("0x111111111111111122222222222222FF");
  CHECK(ns.contains(valid_name));

  quicr::Name another_valid_name("0x11111111111111112222222222222211");
  CHECK(ns.contains(another_valid_name));

  quicr::Name invalid_name("0x11111111111111112222222222222300");
  CHECK_FALSE(ns.contains(invalid_name));

  quicr::Name invalid_sized_name("0x111111111111222222222300");
  CHECK_FALSE(ns.contains(invalid_sized_name));
}

TEST_CASE("quicr::Namespace Contains Namespaces Test")
{
  quicr::Namespace ns({ "0x11111111111111112222222222220000" }, 112);

  quicr::Namespace valid_namespace({ "0x11111111111111112222222222222200" },
                                   120);
  CHECK(ns.contains(valid_namespace));

  quicr::Namespace invalid_namespace({ "0x11111111111111112222222222000000" },
                                     104);
  CHECK_FALSE(ns.contains(invalid_namespace));
}
