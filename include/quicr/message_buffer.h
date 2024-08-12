#pragma once

#include <quicr/name.h>
#include <transport/span.h>

#include <bit>
#include <vector>

namespace quicr::messages {

// clang-format off
namespace {
constexpr bool is_big_endian()
{
#if __cplusplus >= 202002L
  return std::endian::native == std::endian::big;
#else
  return (const uint8_t&)0x01020304 == 0x01;
#endif
}

constexpr uint16_t
swap_bytes(uint16_t value)
{
  if constexpr (is_big_endian())
    return value;

  return ((value >> 8) & 0x00ff) | ((value << 8) & 0xff00);
}

constexpr uint32_t
swap_bytes(uint32_t value)
{
  if constexpr (is_big_endian())
    return value;

  return ((value >> 24) & 0x000000ff) |
         ((value >>  8) & 0x0000ff00) |
         ((value <<  8) & 0x00ff0000) |
         ((value << 24) & 0xff000000);
}

constexpr uint64_t
swap_bytes(uint64_t value)
{
  if constexpr (is_big_endian())
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
  if constexpr (is_big_endian())
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
    using std::runtime_error::what;
  };

  struct EmptyException : public ReadException
  {
    EmptyException();
  };

  struct OutOfRangeException : public ReadException
  {
    OutOfRangeException(size_t length, size_t buffer_length);
  };

  struct LengthException : public ReadException
  {
    LengthException(size_t data_length, size_t expected_length);
  };

  class TypeReadException : public ReadException
  {
  protected:
    using ReadException::ReadException;
  };

private:
  template<typename T>
  struct TypeReadException_Internal : public TypeReadException
  {
    TypeReadException_Internal(size_t buffer_length)
      : TypeReadException("buffer size is smaller than type size: " +
                          std::to_string(buffer_length) + " < " +
                          std::to_string(sizeof(T)))
    {
    }

    using TypeReadException::what;
  };

public:
  using value_type = std::uint8_t;
  using buffer_type = std::vector<value_type>;
  using span_type = Span<const value_type>;

  using iterator = buffer_type::iterator;
  using const_iterator = buffer_type::const_iterator;
  using pointer = buffer_type::pointer;
  using const_pointer = buffer_type::const_pointer;

  MessageBuffer() = default;
  MessageBuffer(const MessageBuffer& other) = default;
  MessageBuffer(MessageBuffer&& other) = default;
  MessageBuffer(size_t reserve_size) { _buffer.reserve(reserve_size); }
  MessageBuffer(const buffer_type& buffer);
  MessageBuffer(buffer_type&& buffer);
  ~MessageBuffer() = default;

  MessageBuffer& operator=(const MessageBuffer& other) = default;
  MessageBuffer& operator=(MessageBuffer&& other) = default;

  bool empty() const { return _buffer.empty() || size() == 0; }
  size_t size() const { return _buffer.size() - _read_offset; }

  iterator begin() noexcept { return std::next(_buffer.begin(), _read_offset); }
  const_iterator begin() const noexcept
  {
    return std::next(_buffer.begin(), _read_offset);
  }

  iterator end() noexcept { return _buffer.end(); }
  const_iterator end() const noexcept { return _buffer.end(); }

  pointer data() noexcept { return _buffer.data() + _read_offset; }
  const_pointer data() const noexcept { return _buffer.data() + _read_offset; }

  void push(const value_type& value) { _buffer.push_back(value); }
  void push(span_type data) { _buffer.insert(_buffer.end(), data.begin(), data.end()); }
  void push(buffer_type&& data);

  void pop() { cleanup(); }
  void pop(size_t length);

  const value_type& front() const;
  span_type front(size_t length) const;

  value_type pop_front();
  buffer_type pop_front(size_t length);

  /**
   * @brief Moves the whole buffer, leaving MessageBuffer empty.
   * @returns The buffer as an rvalue-ref.
   */
  buffer_type&& take();

  /**
   * @brief Prints out the message buffer in hexadecimal bytes.
   * @returns The message buffer bytes as a hexadecimal string.
   */
  std::string to_hex() const;

public:
  /**
   * @brief A fancy operator for push, writes a byte on the end of the buffer.
   * @param value The byte to push.
   * @returns The MessageBuffer that was written to.
   */
  MessageBuffer& operator<<(const value_type& value);

  /**
   * @brief Writes QUICR integral types to the buffer in NBO.
   * @tparam T An unsigned integral type or a quicr::Name.
   * @param value The message to be written.
   * @returns The MessageBuffer that was written to.
   */
#if __cplusplus >= 202002L
  template<UnsignedOrName T>
#else
  template<typename T, typename std::enable_if_t<std::is_integral_v<T> || std::is_same_v<T, Name>, bool> = true>
#endif
  inline MessageBuffer& operator<<(T value)
  {
    value = swap_bytes(value);

    const auto length = _buffer.size();
    _buffer.resize(length + sizeof(T));
    std::memcpy(_buffer.data() + length, &value, sizeof(T));

    return *this;
  }

  /**
   * @brief A fancy operator for pop, reads a byte off the buffer.
   * @param value The value to read into.
   * @returns The MessageBuffer that was read from.
   */
  MessageBuffer& operator>>(value_type& value);

  /**
   * @brief Reads QUICR integral types in HBO.
   * @tparam T A type satisfying quicr::is_integral.
   * @param value The value to read into.
   * @returns The MessageBuffer that was read from.
   */
#if __cplusplus >= 202002L
  template<UnsignedOrName T>
#else
  template<typename T, typename std::enable_if_t<std::is_integral_v<T> || std::is_same_v<T, Name>, bool> = true>
#endif
  inline MessageBuffer& operator>>(T& value)
  {
    if (empty())
      throw EmptyException();

    if (size() < sizeof(T))
      throw TypeReadException_Internal<T>(size());

    std::memcpy(&value, data(), sizeof(T));
    cleanup(sizeof(T));

    value = swap_bytes(value);

    return *this;
  }

private:
  /**
   * @brief Adds to the read offset, and eventually clears the buffer.
   * @param length The amount to add to the read offset.
   */
  void cleanup(size_t length = 1);

private:
  buffer_type _buffer;
  size_t _read_offset = 0;
};
}
