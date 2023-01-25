#pragma once
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

TEST_CASE("QUICR::Name Basic Test ")
{
  quicr::Name val42( 0x42 );

  quicr::Name str42( std::string( "0x42" ) );
  CHECK( val42 == str42 );

  quicr::Name hex42( "0x42" );
  CHECK( val42 == hex42 );
  
  quicr::Name val41( 0x41 );
  quicr::Name val43( 0x43 );
  CHECK( val42 + 1 == val43 );
  CHECK( val42 - 1 == val41 );

  //CHECK( (quicr::Name("0x1234") >> 4) ==  quicr::Name("0x123") );
  //CHECK( (quicr::Name("0x1234") << 4) ==  quicr::Name("0x1230") );
  
  CHECK( quicr::Name("0x123") < quicr::Name("0x124") );
  CHECK( quicr::Name("0x123") > quicr::Name("0x122") );

  CHECK( quicr::Name("0x123") != quicr::Name("0x122") );

  //CHECK( quicr::Name("0x123456789abcd0123456789abcd0") >> 64 ==  quicr::Name( 0x123456789abcd0 ) );
  CHECK( quicr::Name("0x123456789abcd0FFFFFFFFFFFFFF") +1 ==  quicr::Name("0x123456789abcd10000000000000000" ) );
}
