#pragma once

#include <quicr/quicr_name.h>
#include <quicr/quicr_namespace.h>

#include <vector>
#include <cassert>

namespace quicr {
/**
 * @brief Variable length integer
 */
enum class uintVar_t : uint64_t {};

uintVar_t to_varint(uint64_t);
uint64_t from_varint(uintVar_t);

namespace messages {
/**
 * @brief Defines a buffer that can be sent over transport. Cannot be copied.
 */
class MessageBuffer
{
public:
  MessageBuffer() = default;
  MessageBuffer(MessageBuffer&& other);
  MessageBuffer(const std::vector<uint8_t>& buffer);
  MessageBuffer(std::vector<uint8_t>&& buffer);
  MessageBuffer(const MessageBuffer& other) = delete;
  ~MessageBuffer() = default;

  bool empty() const { return _buffer.empty(); }

  void push_back(uint8_t t) { _buffer.push_back(t); }
  void pop_back() { _buffer.pop_back(); }
  uint8_t back() const { return _buffer.back(); }

  void push_back(const std::vector<uint8_t>& data);
  void pop_back(uint16_t len);
  std::vector<uint8_t> back(uint16_t len);

  /**
   * @brief Returns an rvalue reference to the buffer (moving it).
   */
  std::vector<uint8_t>&& get() { return std::move(_buffer); }

  std::string to_hex() const;

  void operator=(const MessageBuffer& other) = delete;
  void operator=(MessageBuffer&& other);

private:
  std::vector<uint8_t> _buffer;
};
  
MessageBuffer& operator<<(MessageBuffer& msg, uint8_t val);
bool operator>>(MessageBuffer& msg, uint8_t& val);

MessageBuffer& operator<<(MessageBuffer& msg, const uint64_t& val);
bool operator>>(MessageBuffer& msg, uint64_t& val);

MessageBuffer& operator<<(MessageBuffer& msg, const uintVar_t& val);
bool operator>>(MessageBuffer& msg, uintVar_t& val);

MessageBuffer& operator<<(MessageBuffer& msg, const std::vector<uint8_t>& val);
bool operator>>(MessageBuffer& msg, std::vector<uint8_t>& val);
}
}
