#pragma once

#include <cassert>
#include <ostream>
#include <vector>

namespace quicr {
/**
 * @brief Variable length integer
 */
class uintVar_t
{
public:
  uintVar_t() = default;
  uintVar_t(const uintVar_t&) = default;
  constexpr uintVar_t(uint64_t v)
    : _value{ v }
  {
    assert(v < 0x1ull << 61);
  }

  constexpr operator uint64_t() const { return _value; }
  constexpr uintVar_t& operator=(uintVar_t other)
  {
    _value = other._value;
    return *this;
  }
  constexpr uintVar_t& operator=(uint64_t value)
  {
    assert(value < 0x1ull << 61);
    _value = value;
    return *this;
  }

  constexpr bool operator==(uintVar_t other) { return _value == other._value; }
  constexpr bool operator!=(uintVar_t other) { return !(*this == other); }
  constexpr bool operator>(uintVar_t other) { return _value > other._value; }
  constexpr bool operator>=(uintVar_t other) { return _value >= other._value; }
  constexpr bool operator<(uintVar_t other) { return _value < other._value; }
  constexpr bool operator<=(uintVar_t other) { return _value <= other._value; }

  friend std::ostream& operator<<(std::ostream& os, uintVar_t v)
  {
    return os << v._value;
  }

private:
  uint64_t _value;
};
}

namespace quicr::messages {

/**
 * @brief Defines a buffer that can be sent over transport. Cannot be copied.
 */
class MessageBuffer
{
public:
  MessageBuffer() = default;
  MessageBuffer(size_t reserve_size) { _buffer.reserve(reserve_size); }
  MessageBuffer(const MessageBuffer& other) = default;
  MessageBuffer(MessageBuffer&& other);
  MessageBuffer(const std::vector<uint8_t>& buffer);
  MessageBuffer(std::vector<uint8_t>&& buffer);
  ~MessageBuffer() = default;

  bool empty() const { return _buffer.empty(); }

  void push(uint8_t t) { _buffer.push_back(t); }
  void pop();
  const uint8_t& front() const { return _buffer.front(); }

  void push(const std::vector<uint8_t>& data);
  void push(std::vector<uint8_t>&& data);
  void pop(uint16_t len);
  std::vector<uint8_t> front(uint16_t len);

  std::vector<uint8_t> get();

  std::string to_hex() const;

  void operator=(const MessageBuffer& other) = delete;
  void operator=(MessageBuffer&& other);

  template<typename Uint_t>
  friend MessageBuffer& operator<<(MessageBuffer& msg, Uint_t val);
  template<typename Uint_t>
  friend MessageBuffer& operator>>(MessageBuffer& msg, Uint_t& val);

private:
  std::vector<uint8_t> _buffer;
};

MessageBuffer&
operator<<(MessageBuffer& msg, const uintVar_t& val);
MessageBuffer&
operator>>(MessageBuffer& msg, uintVar_t& val);

MessageBuffer&
operator<<(MessageBuffer& msg, const std::vector<uint8_t>& val);
MessageBuffer&
operator<<(MessageBuffer& msg, std::vector<uint8_t>&& val);
MessageBuffer&
operator>>(MessageBuffer& msg, std::vector<uint8_t>& val);

/**
 * @brief General exceptions for errors coming from MessageBuffer
 */
struct MessageBufferException : public std::runtime_error
{
  using std::runtime_error::runtime_error;
};

}
