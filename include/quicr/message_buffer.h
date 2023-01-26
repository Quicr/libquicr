#pragma once

#include <quicr/quicr_name.h>
#include <quicr/quicr_namespace.h>

#include <vector>

namespace quicr::messages {

// TODO: This is a very crude implementation. Optimize???
struct MessageBuffer
{

  void push_back(const std::vector<uint8_t>& data)
  {
    buffer.insert(buffer.end(), data.begin(), data.end());
  }

  void push_back(uint8_t t) { buffer.push_back(t); };

  void pop_back() { buffer.pop_back(); };

  uint8_t back() const { return buffer.back(); };

  std::vector<uint8_t> back(uint16_t len)
  {
    assert(len <= buffer.size());
    auto vec = std::vector<uint8_t>(len);
    auto delta = buffer.size() - len;
    std::copy(buffer.begin() + delta, buffer.end(), vec.begin());
    buffer.erase(buffer.begin() + delta, buffer.end());
    return vec;
  }

  std::string to_hex();

  std::vector<uint8_t> buffer;
};

// Operations for encoding types

void
operator<<(MessageBuffer& msg, const uint64_t& val);
void
operator<<(MessageBuffer& msg, uint8_t val);
void
operator<<(MessageBuffer& msg, const std::vector<uint8_t>& val);

bool
operator>>(MessageBuffer& msg, uint64_t& val);
bool
operator>>(MessageBuffer& msg, uint8_t& val);
bool
operator>>(MessageBuffer& msg, std::vector<uint8_t>& val);

enum class uintVar_t : uint64_t
{
};

void
operator<<(MessageBuffer& msg, const uintVar_t& val);
bool
operator>>(MessageBuffer& msg, uintVar_t& val);
uintVar_t toVarInt(uint64_t);
uint64_t fromVarInt(uintVar_t);

void
operator<<(MessageBuffer& msg, const quicr::Name& val);
bool
operator>>(MessageBuffer& msg, quicr::Name& val);

}