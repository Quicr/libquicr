#pragma once

#include <quicr/name.h>
#include <quicr/namespace.h>

#include <bit>
#include <vector>

namespace quicr::messages {

// clang-format off
namespace {
constexpr uint16_t
swap_bytes(uint16_t value)
{
  if constexpr (std::endian::native == std::endian::big)
    return value;

  return ((value >> 8) & 0x00ff) | ((value << 8) & 0xff00);
}

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

constexpr quicr::Name
swap_bytes(quicr::Name value)
{
  if constexpr (std::endian::native == std::endian::big)
    return value;

  constexpr auto ones = ~0x0_name;
  return ((ones & swap_bytes(uint64_t(value))) << 64) |
                  swap_bytes(uint64_t(value >> 64));
}
}
// clang-format on

/**
 * @brief Defines a buffer that can be sent over transport.
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

  /**
   * @brief Moves the whole buffer, leaving MessageBuffer empty.
   * @returns The buffer as an rvalue-ref.
   */
  [[deprecated("quicr::message::MessageBuffer::get is deprecated, use take")]]
  std::vector<uint8_t> get();

  /**
   * @brief Moves the whole buffer, leaving MessageBuffer empty.
   * @returns The buffer as an rvalue-ref.
   */
  std::vector<uint8_t>&& take();

  std::string to_hex() const;

  MessageBuffer& operator=(const MessageBuffer& other) = default;
  MessageBuffer& operator=(MessageBuffer&& other) = default;

  /**
   * @brief A fancy operator for push, writes a byte on the end of the buffer.
   * @param value The byte to push.
   * @returns The MessageBuffer that was written to.
   */
  inline MessageBuffer& operator<<(uint8_t value)
  {
    _buffer.push_back(value);
    return *this;
  }

  /**
   * @brief Writes QUICR integral types to the buffer in NBO.
   * @tparam T A type satisfying quicr::is_integral.
   * @param value The message to be written.
   * @returns The MessageBuffer that was written to.
   */
  template<typename T, typename = std::enable_if_t<quicr::is_integral_v<T>, T>>
  inline MessageBuffer& operator<<(T value)
  {
    value = swap_bytes(value);
    uint8_t* val_ptr = reinterpret_cast<uint8_t*>(&value);

    const auto length = _buffer.size();
    _buffer.resize(length + sizeof(T));
    std::memcpy(_buffer.data() + length, val_ptr, sizeof(T));

    return *this;
  }

  /**
   * @brief A fancy operator for pop, reads a byte off the buffer.
   * @param value The value to read into.
   * @returns The MessageBuffer that was read from.
   */
  inline MessageBuffer& operator>>(uint8_t& value)
  {
    if (empty()) {
      throw MessageBuffer::ReadException(
        "Cannot read from empty message buffer");
    }

    value = front();
    pop();
    return *this;
  }

  /**
   * @brief Reads QUICR integral types in HBO.
   * @tparam T A type satisfying quicr::is_integral.
   * @param value The value to read into.
   * @returns The MessageBuffer that was read from.
   */
  template<typename T, typename = std::enable_if_t<quicr::is_integral_v<T>, T>>
  inline MessageBuffer& operator>>(T& value)
  {
    if (empty()) {
      throw MessageBuffer::ReadException(
        "Cannot read from empty message buffer");
    }

    if (_buffer.size() < sizeof(T)) {
      throw MessageBuffer::ReadException(
        "Cannot read mismatched size buffer into size of type: Wanted " +
        std::to_string(sizeof(T)) + " but buffer only contains " +
        std::to_string(_buffer.size()));
    }

    auto val_ptr = reinterpret_cast<uint8_t*>(&value);
    std::memcpy(val_ptr, _buffer.data(), sizeof(T));

    _buffer.erase(_buffer.begin(), std::next(_buffer.begin(), sizeof(T)));

    value = swap_bytes(value);

    return *this;
  }

private:
  std::vector<uint8_t> _buffer;
};
}
