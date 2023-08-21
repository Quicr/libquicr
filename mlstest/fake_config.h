#pragma once
#include <map>
#include <quicr/quicr_common.h>

// THIS SHOULD COME FROM MANIFEST SERVER. ADDING SOME CONSTANTS FOR
// TESTING PURPOSES ONLY.

enum class SUBSCRIBE_OP_TYPE {
  KeyPackage = 0,
  Welcome,
  Commit,
  Invalid
};

// todo: use numero-uno library to convert from uri to hex representation

/*
 * URI Sample: "quicr://webex.cisco.com<pen=1><sub_pen=1>/conferences/<int24>/secGroupId/<int16>/datatype/<int8>/endpoint/<int24>
 * webex.cisco.com, 32 bits = 0xAABBCCDD
 * conference,      24 bits = 0x112233
 * secGroupId,      16 bits = 0xEEEE
 * datatype,         8 bits = one-of {KeyPackage(0x01), Welcome(0x02), Commit(0x03}
 * endpointId       24 bits = 0x000001 - creator, 0x000002 onwards for participants
 * messageId        24 bits for each message
 *
 */

 // Names are hardcoded to work for 2 participant flow
 struct namespaceConfig
{
  std::map<SUBSCRIBE_OP_TYPE, quicr::Namespace> subscribe_op_map {
    { SUBSCRIBE_OP_TYPE::KeyPackage, quicr::Namespace(quicr::Name("0xAABBCCDD112233EEEE01000002FFFF01"), 80)},
    { SUBSCRIBE_OP_TYPE::Welcome, quicr::Namespace(quicr::Name("0xAABBCCDD112233EEEE02000002FFFF01"), 80)},
    { SUBSCRIBE_OP_TYPE::Commit, quicr::Namespace(quicr::Name("0xAABBCCDD112233EEEE03000001FFFF01"), 80)},
  };
};
