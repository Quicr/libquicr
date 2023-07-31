#pragma once
#include <map>
#include <quicr/quicr_common.h>

// THIS COMES FROM MANIFEST SERVER

enum class SUBSCRIBE_OP_TYPE {
  KeyPackage = 0,
  Welcome,
  Commit
};
// todo: use numero-uno library to convert from uri to hex representation

std::map<quicr::Namespace, SUBSCRIBE_OP_TYPE> subscribe_op_map {
  {quicr::Namespace(quicr::Name("0x0011223344556600000100000000ABCD"), 96), SUBSCRIBE_OP_TYPE::KeyPackage},
};