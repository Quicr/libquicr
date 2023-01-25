#include "quicr/message_buffer.h"
#include <iomanip>
#include <sstream>

namespace quicr::messages {

std::string
MessageBuffer::to_hex()
{
  std::stringstream hex(std::ios_base::out);
  hex.flags(std::ios::hex);
  for (const auto& byte : buffer) {
    hex << std::setw(2) << std::setfill('0') << int(byte);
  }
  return hex.str();
}

///
/// Atomic Types
///

void
operator<<(MessageBuffer& msg, const uint64_t& val)
{
  // TODO - std::copy version for little endian machines optimization

  // buffer on wire is little endian (that is *not* network byte order)
  msg.push_back(uint8_t((val >> 0) & 0xFF));
  msg.push_back(uint8_t((val >> 8) & 0xFF));
  msg.push_back(uint8_t((val >> 16) & 0xFF));
  msg.push_back(uint8_t((val >> 24) & 0xFF));
  msg.push_back(uint8_t((val >> 32) & 0xFF));
  msg.push_back(uint8_t((val >> 40) & 0xFF));
  msg.push_back(uint8_t((val >> 48) & 0xFF));
  msg.push_back(uint8_t((val >> 56) & 0xFF));
}

void
operator<<(MessageBuffer& msg, const uint8_t val)
{
  msg.push_back(val);
}

void
operator<<(MessageBuffer& msg, const std::vector<uint8_t>& val)
{
  msg.push_back(val);
  msg.push_back(val.size());
}

bool
operator>>(MessageBuffer& msg, uint64_t& val)
{
  uint8_t byte[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

  bool ok = true;
  ok &= msg >> byte[7];
  ok &= msg >> byte[6];
  ok &= msg >> byte[5];
  ok &= msg >> byte[4];
  ok &= msg >> byte[3];
  ok &= msg >> byte[2];
  ok &= msg >> byte[1];
  ok &= msg >> byte[0];

  val = (uint64_t(byte[0]) << 0) + (uint64_t(byte[1]) << 8) +
        (uint64_t(byte[2]) << 16) + (uint64_t(byte[3]) << 24) +
        (uint64_t(byte[4]) << 32) + (uint64_t(byte[5]) << 40) +
        (uint64_t(byte[6]) << 48) + (uint64_t(byte[7]) << 56);

  return ok;
}

bool
operator>>(MessageBuffer& msg, uint8_t& val)
{
  val = msg.back();
  msg.pop_back();
  return true;
}

bool
operator>>(MessageBuffer& msg, std::vector<uint8_t>& val)
{
  uint8_t vecSize = 0;
  msg >> vecSize;
  if (vecSize == 0) {
    return false;
  }

  val.resize(vecSize);
  val = msg.back(vecSize);
  return true;
}

///
/// Varints
///

void
operator<<(MessageBuffer& msg, const uintVar_t& v)
{
  uint64_t val = fromVarInt(v);

  assert(val < ((uint64_t)1 << 61));

  if (val <= ((uint64_t)1 << 7)) {
    msg.push_back(uint8_t(((val >> 0) & 0x7F)) | 0x00);
    return;
  }

  if (val <= ((uint64_t)1 << 14)) {
    msg.push_back(uint8_t((val >> 0) & 0xFF));
    msg.push_back(uint8_t(((val >> 8) & 0x3F) | 0x80));
    return;
  }

  if (val <= ((uint64_t)1 << 29)) {
    msg.push_back(uint8_t((val >> 0) & 0xFF));
    msg.push_back(uint8_t((val >> 8) & 0xFF));
    msg.push_back(uint8_t((val >> 16) & 0xFF));
    msg.push_back(uint8_t(((val >> 24) & 0x1F) | 0x80 | 0x40));
    return;
  }

  msg.push_back(uint8_t((val >> 0) & 0xFF));
  msg.push_back(uint8_t((val >> 8) & 0xFF));
  msg.push_back(uint8_t((val >> 16) & 0xFF));
  msg.push_back(uint8_t((val >> 24) & 0xFF));
  msg.push_back(uint8_t((val >> 32) & 0xFF));
  msg.push_back(uint8_t((val >> 40) & 0xFF));
  msg.push_back(uint8_t((val >> 48) & 0xFF));
  msg.push_back(uint8_t(((val >> 56) & 0x0F) | 0x80 | 0x40 | 0x20));
}

bool
operator>>(MessageBuffer& msg, uintVar_t& v)
{
  uint8_t byte[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
  bool ok = true;

  uint8_t first = msg.back();

  if ((first & (0x80)) == 0) {
    ok &= msg >> byte[0];
    uint8_t val = ((byte[0] & 0x7F) << 0);
    v = toVarInt(val);
    return ok;
  }

  if ((first & (0x80 | 0x40)) == 0x80) {
    ok &= msg >> byte[1];
    ok &= msg >> byte[0];
    uint16_t val = (((uint16_t)byte[1] & 0x3F) << 8) + ((uint16_t)byte[0] << 0);
    v = toVarInt(val);
    return ok;
  }

  if ((first & (0x80 | 0x40 | 0x20)) == (0x80 | 0x40)) {
    ok &= msg >> byte[3];
    ok &= msg >> byte[2];
    ok &= msg >> byte[1];
    ok &= msg >> byte[0];
    uint32_t val = ((uint32_t)(byte[3] & 0x1F) << 24) +
                   ((uint32_t)byte[2] << 16) + ((uint32_t)byte[1] << 8) +
                   ((uint32_t)byte[0] << 0);
    v = toVarInt(val);
    return ok;
  }

  ok &= msg >> byte[7];
  ok &= msg >> byte[6];
  ok &= msg >> byte[5];
  ok &= msg >> byte[4];
  ok &= msg >> byte[3];
  ok &= msg >> byte[2];
  ok &= msg >> byte[1];
  ok &= msg >> byte[0];
  uint64_t val = ((uint64_t)(byte[3] & 0x0F) << 56) +
                 ((uint64_t)(byte[2]) << 48) + ((uint64_t)(byte[1]) << 40) +
                 ((uint64_t)(byte[0]) << 32) + ((uint64_t)(byte[2]) << 24) +
                 ((uint64_t)(byte[2]) << 16) + ((uint64_t)(byte[1]) << 8) +
                 ((uint64_t)(byte[0]) << 0);
  v = toVarInt(val);
  return ok;
}

uintVar_t
toVarInt(uint64_t v)
{
  assert(v < ((uint64_t)0x1 << 61));
  return static_cast<uintVar_t>(v);
}

uint64_t
fromVarInt(uintVar_t v)
{
  return static_cast<uint64_t>(v);
}

}