#pragma once

#include <quicr/name.h>

#include <bit>
#include <cassert>
#include <istream>
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
  constexpr uintVar_t(const uintVar_t&) = default;
  constexpr uintVar_t(uintVar_t&&) = default;
  constexpr uintVar_t(uint64_t value)
    : _value{ value }
  {
    if (value >= 0x1ull << 61)
      throw std::runtime_error("Max value cannot be exceeded: " +
                               std::to_string(0x1ull << 61));
  }

  constexpr operator uint64_t() const { return _value; }
  constexpr uintVar_t& operator=(const uintVar_t&) = default;
  constexpr uintVar_t& operator=(uintVar_t&&) = default;
  constexpr uintVar_t& operator=(uint64_t value)
  {
    if (value >= 0x1ull << 61)
      throw std::runtime_error("Max value cannot be exceeded: " +
                               std::to_string(0x1ull << 61));
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

  friend std::istream& operator>>(std::istream& is, uintVar_t v)
  {
    return is >> v._value;
  }

private:
  uint64_t _value;
};
}

namespace quicr::messages {

namespace {

template<typename T>
constexpr T
swap_bytes(T value)
{
  if constexpr (std::endian::native == std::endian::big)
    return value;

  uint8_t* value_ptr = reinterpret_cast<uint8_t*>(&value);
  for (size_t i = 0; i < sizeof(value) / 2; ++i) {
    std::swap(value_ptr[i], value_ptr[sizeof(value) - 1 - i]);
  }
  return value;
}

template<>
constexpr uint16_t
swap_bytes(uint16_t value)
{
  if constexpr (std::endian::native == std::endian::big)
    return value;

  return ((value >> 8) & 0x00ff) | ((value << 8) & 0xff00);
}

// clang-format off
template<>
constexpr uint32_t
swap_bytes(uint32_t value)
{
  if constexpr (std::endian::native == std::endian::big)
    return value;

  return ((value >> 24) & 0x000000ff) |
         ((value >>  8) & 0x0000ff00) |
         ((value <<  8) & 0x00ff0000) |
         ((value << 24) & 0xff000000);
}

template<>
constexpr uint64_t
swap_bytes(uint64_t value)
{
  if constexpr (std::endian::native == std::endian::big)
    return value;

  return ((value >> 56) & 0x00000000000000ff) |
         ((value >> 40) & 0x000000000000ff00) |
         ((value >> 24) & 0x0000000000ff0000) |
         ((value >>  8) & 0x00000000ff000000) |
         ((value <<  8) & 0x000000ff00000000) |
         ((value << 24) & 0x0000ff0000000000) |
         ((value << 40) & 0x00ff000000000000) |
         ((value << 56) & 0xff00000000000000);
}

template<>
constexpr quicr::Name
swap_bytes(quicr::Name value)
{
  if constexpr (std::endian::native == std::endian::big)
    return value;

  constexpr auto ones = ~0x0_name;
  return ((ones & swap_bytes(uint64_t(value))) << 64) | swap_bytes(uint64_t(value >> 64));
}
}
// clang-format on

/**
 * @brief Defines a buffer that can be sent over transport. Cannot be copied.
 */
class MessageBuffer
{
public:
  struct ReadException : public std::runtime_error
  {
    using std::runtime_error::runtime_error;
  };

  struct OutOfRangeException : public ReadException
  {
    using ReadException::ReadException;
  };

  struct MessageTypeException : public ReadException
  {
    using ReadException::ReadException;
  };

  struct LengthException : public ReadException
  {
    using ReadException::ReadException;
  };

public:
  MessageBuffer() = default;
  MessageBuffer(const MessageBuffer& other) = default;
  MessageBuffer(MessageBuffer&& other) = default;
  MessageBuffer(size_t reserve_size) { _buffer.reserve(reserve_size); }
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
  std::vector<uint8_t> front(uint16_t len) const;
  std::vector<uint8_t> pop_front(uint16_t len);

  [[deprecated("quicr::message::MessageBuffer::get is deprecated, use take")]]
  std::vector<uint8_t> get();
  std::vector<uint8_t>&& take();

  std::string to_hex() const;

  MessageBuffer& operator=(const MessageBuffer& other) = default;
  MessageBuffer& operator=(MessageBuffer&& other) = default;

  friend MessageBuffer& operator<<(MessageBuffer& msg, uint8_t val)
  {
    msg.push(val);
    return msg;
  }

  template<typename T>
  friend MessageBuffer& operator<<(MessageBuffer& msg, T val)
  {
    val = swap_bytes(val);
    uint8_t* val_ptr = reinterpret_cast<uint8_t*>(&val);

    const auto length = msg._buffer.size();
    msg._buffer.resize(length + sizeof(T));
    std::memcpy(msg._buffer.data() + length, val_ptr, sizeof(T));

    return msg;
  }

  template<typename T>
  friend MessageBuffer& operator>>(MessageBuffer& msg, T& val)
  {
    if (msg.empty()) {
      throw MessageBuffer::ReadException(
        "Cannot read from empty message buffer");
    }

    if (msg._buffer.size() < sizeof(T)) {
      throw MessageBuffer::ReadException(
        "Cannot read mismatched size buffer into size of type: Wanted " +
        std::to_string(sizeof(T)) + " but buffer only contains " +
        std::to_string(msg._buffer.size()));
    }

    auto val_ptr = reinterpret_cast<uint8_t*>(&val);
    std::memcpy(val_ptr, msg._buffer.data(), sizeof(T));

    msg._buffer.erase(msg._buffer.begin(),
                      std::next(msg._buffer.begin(), sizeof(T)));
    val = swap_bytes(val);

    return msg;
  }

  friend MessageBuffer& operator>>(MessageBuffer& msg, uint8_t& val)
  {
    if (msg.empty()) {
      throw MessageBuffer::ReadException(
        "Cannot read from empty message buffer");
    }

    val = msg.front();
    msg.pop();
    return msg;
  }

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
}
