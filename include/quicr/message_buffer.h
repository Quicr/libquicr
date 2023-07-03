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
   * @param buffer The MessageBuffer to push to.
   * @param value The byte to push.
   * @returns The MessageBuffer that was written to.
   */
  friend inline MessageBuffer& operator<<(MessageBuffer& buffer, uint8_t value)
  {
    buffer._buffer.push_back(value);
    return buffer;
  }

  /**
   * @brief Writes QUICR integral types to the buffer in NBO.
   * @tparam T A type satisfying quicr::is_integral.
   * @param msg The MessageBuffer to write to.
   * @param value The message to be written.
   * @returns The MessageBuffer that was written to.
   */
  template<typename T, typename = std::enable_if_t<quicr::is_integral_v<T>, T>>
  friend inline MessageBuffer& operator<<(MessageBuffer& msg, T value)
  {
    value = swap_bytes(value);
    uint8_t* val_ptr = reinterpret_cast<uint8_t*>(&value);

    const auto length = msg._buffer.size();
    msg._buffer.resize(length + sizeof(T));
    std::memcpy(msg._buffer.data() + length, val_ptr, sizeof(T));

    return msg;
  }

  /**
   * @brief Overload for Namespace integral type to write in NBO.
   * @param msg The MessageBuffer to write to.
   * @param value The message to be written.
   * @returns The MessageBuffer that was written to.
   */
  friend MessageBuffer& operator<<(MessageBuffer& msg, quicr::Namespace value)
  {
    if constexpr (std::endian::native == std::endian::big)
      return msg << value.name() << value.length();

    return msg << value.length() << value.name();
  }

  /**
   * @brief A fancy operator for pop, reads a byte off the buffer.
   * @param buffer The MessageBuffer to read from.
   * @param value The value to read into.
   * @returns The MessageBuffer that was read from.
   */
  friend inline MessageBuffer& operator>>(MessageBuffer& msg, uint8_t& value)
  {
    if (msg.empty()) {
      throw MessageBuffer::ReadException(
        "Cannot read from empty message buffer");
    }

    value = msg.front();
    msg.pop();
    return msg;
  }

  /**
   * @brief Reads QUICR integral types in HBO.
   * @tparam T A type satisfying quicr::is_integral.
   * @param buffer The MessageBuffer to read from.
   * @param value The value to read into.
   * @returns The MessageBuffer that was read from.
   */
  template<typename T, typename = std::enable_if_t<quicr::is_integral_v<T>, T>>
  friend inline MessageBuffer& operator>>(MessageBuffer& msg, T& value)
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

    auto val_ptr = reinterpret_cast<uint8_t*>(&value);
    std::memcpy(val_ptr, msg._buffer.data(), sizeof(T));

    msg._buffer.erase(msg._buffer.begin(),
                      std::next(msg._buffer.begin(), sizeof(T)));

    value = swap_bytes(value);

    return msg;
  }

  /**
   * @brief Overload for Namespace integral type to read into HBO.
   * @param buffer The MessageBuffer to read from.
   * @param value The value to read into.
   * @returns The MessageBuffer that was read from.
   */
  friend MessageBuffer& operator>>(MessageBuffer& msg, quicr::Namespace& value)
  {
    quicr::Name name;
    uint8_t length;

    if constexpr (std::endian::native == std::endian::big)
      msg >> name >> length;
    else
      msg >> length >> name;

    value = { name, length };
    return msg;
  }

private:
  std::vector<uint8_t> _buffer;
};
}
