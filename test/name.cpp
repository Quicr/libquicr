#include <doctest/doctest.h>
#include <map>
#include <quicr/quicr_common.h>

#include <quicr/quicr_name.h>

// Tests for QuicrName and QuicrNamespace

TEST_CASE("QUICRNamespace Equality")
{
  quicr::QUICRNamespace qns1{ 0x1000, 0x2000, 3 };
  quicr::QUICRNamespace qns2{ 0x1000, 0x2000, 3 };
  quicr::QUICRNamespace qns3{ 0x1000, 0x2000, 4 };
  quicr::QUICRNamespace qns4{ 0x2000, 0x2000, 3 };

  CHECK_EQ(qns1, qns2);
  CHECK_NE(qns1, qns3);
  CHECK_NE(qns1, qns4);
}

TEST_CASE("QUICRNamespace Map Lookup")
{
  quicr::QUICRNamespace qns1{ 0x1000, 0x2000, 3 };
  quicr::QUICRNamespace qns2{ 0x1000, 0x3000, 5 };
  quicr::QUICRNamespace qns3{ 0x1000, 0x4000, 4 };

  std::map<quicr::QUICRNamespace, int> qns_map;
  qns_map[qns1] = 1;
  qns_map[qns2] = 2;

  CHECK_EQ(qns_map.size(), 2);
  CHECK_EQ(qns_map.count(qns1), 1);
  CHECK_EQ(qns_map.count(qns2), 1);
  CHECK_EQ(qns_map.count(qns3), 0);
  CHECK_EQ(qns_map[qns1], 1);
  CHECK_EQ(qns_map[qns2], 2);
  CHECK_THROWS(qns_map.at(qns3));
}

TEST_CASE("QUICRName Map Lookup with QuicRNamespace ")
{
  quicr::QUICRNamespace qns1{ 0x11111111, 0x22222200, 8 };
  quicr::QUICRName qn1{ 0x11111111, 0x222222FF };
  CHECK(quicr::is_quicr_name_in_namespace(qns1, qn1));
}

TEST_CASE("quicr::Name Constructor Tests")
{
  quicr::Name val42(0x42);
  quicr::Name str42("0x42");
  CHECK_EQ(val42, str42);

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
           quicr::Name(0x123456789abcdef));
}

TEST_CASE("quicr::Name Arithmetic Tests")
{
  quicr::Name val42(0x42);
  quicr::Name val41(0x41);
  quicr::Name val43(0x43);
  CHECK_EQ(val42 + 1, val43);
  CHECK_EQ(val42 - 1, val41);
  CHECK_EQ(quicr::Name("0x0FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF") + 1,
           quicr::Name("0x10000000000000000000000000000000"));
}

TEST_CASE("quicr::Name Byte Array Tests")
{
  quicr::Name name_to_bytes("0x10000000000000000000000000000000");
  auto byte_arr = name_to_bytes.data();
  CHECK_FALSE(byte_arr.empty());
  CHECK_EQ(byte_arr.size(), 16);

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
