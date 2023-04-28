#include <quicr/message_buffer.h>

#include <algorithm>
#include <array>
#include <bit>
#include <iomanip>
#include <sstream>
#include <vector>

namespace quicr::messages {
MessageBuffer::MessageBuffer(const std::vector<uint8_t>& buffer)
  : _buffer{ buffer }
{
}

MessageBuffer::MessageBuffer(std::vector<uint8_t>&& buffer)
  : _buffer{ std::move(buffer) }
{
}

void
MessageBuffer::pop()
{
  if (empty()) {
    throw MessageBuffer::ReadException("Cannot pop from empty message buffer");
  }

  _buffer.erase(_buffer.begin());
}

void
MessageBuffer::push(const std::vector<uint8_t>& data)
{
  _buffer.insert(_buffer.end(), data.begin(), data.end());
}

void
MessageBuffer::push(std::vector<uint8_t>&& data)
{
  _buffer.insert(_buffer.end(),
                 std::make_move_iterator(data.begin()),
                 std::make_move_iterator(data.end()));
}

void
MessageBuffer::pop(uint16_t len)
{
  if (len == 0)
    return;

  if (len > _buffer.size())
    throw OutOfRangeException(
      "len cannot be longer than the size of the buffer");

  _buffer.erase(_buffer.begin(), std::next(_buffer.begin(), len));
};

std::vector<uint8_t>
MessageBuffer::front(uint16_t len)
{
  if (len == 0)
    return {};

  if (len > _buffer.size())
    throw OutOfRangeException(
      "len cannot be longer than the size of the buffer");

  return { _buffer.begin(), std::next(_buffer.begin(), len) };
}

std::vector<uint8_t>
MessageBuffer::pop_front(uint16_t len)
{
  if (len == 0)
    return {};

  if (len > _buffer.size())
    throw OutOfRangeException(
      "len cannot be longer than the size of the buffer");

  std::vector<uint8_t> front(len);
  std::copy_n(std::make_move_iterator(_buffer.begin()), len, front.begin());
  _buffer.erase(_buffer.begin(), std::next(_buffer.begin(), len));

  return front;
}

std::vector<uint8_t>
MessageBuffer::get()
{
  return std::move(_buffer);
}

std::string
MessageBuffer::to_hex() const
{
  std::ostringstream hex;
  hex << std::hex << std::setfill('0');
  for (const auto& byte : _buffer) {
    hex << std::setw(2) << int(byte);
  }
  return hex.str();
}

// clang-format off
constexpr uint16_t
swap_bytes(uint16_t value)
{
  return ((value >> 8) & 0x00ff) | ((value << 8) & 0xff00);
}

constexpr uint32_t
swap_bytes(uint32_t value)
{
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
// clang-format on

template<typename Uint_t>
MessageBuffer&
operator<<(MessageBuffer& msg, Uint_t val)
{
  val = swap_bytes(val);
  uint8_t* val_ptr = reinterpret_cast<uint8_t*>(&val);
  msg._buffer.insert(msg._buffer.end(), val_ptr, val_ptr + sizeof(Uint_t));
  return msg;
}
template MessageBuffer&
operator<<(MessageBuffer& msg, uint16_t val);
template MessageBuffer&
operator<<(MessageBuffer& msg, uint32_t val);
template MessageBuffer&
operator<<(MessageBuffer& msg, uint64_t val);

template<>
MessageBuffer&
operator<<(MessageBuffer& msg, uint8_t val)
{
  msg.push(val);
  return msg;
}

template<typename Uint_t>
MessageBuffer&
operator>>(MessageBuffer& msg, Uint_t& val)
{
  if (msg.empty()) {
    throw MessageBuffer::ReadException("Cannot read from empty message buffer");
  }

  constexpr size_t byte_length = sizeof(Uint_t);
  if (msg._buffer.size() < byte_length) {
    throw MessageBuffer::ReadException(
      "Cannot read mismatched size buffer into size of type: Wanted " +
      std::to_string(byte_length) + " but buffer only contains " +
      std::to_string(msg._buffer.size()));
  }

  auto buffer_front = msg._buffer.begin();
  auto buffer_byte_end =
    std::next(buffer_front, std::min(msg._buffer.size(), byte_length));

  std::copy_n(std::make_move_iterator(buffer_front),
              byte_length,
              reinterpret_cast<uint8_t*>(&val));
  msg._buffer.erase(buffer_front, buffer_byte_end);
  val = swap_bytes(val);

  return msg;
}
template MessageBuffer&
operator>>(MessageBuffer& msg, uint16_t& val);
template MessageBuffer&
operator>>(MessageBuffer& msg, uint32_t& val);
template MessageBuffer&
operator>>(MessageBuffer& msg, uint64_t& val);

template<>
MessageBuffer&
operator>>(MessageBuffer& msg, uint8_t& val)
{
  if (msg.empty()) {
    throw MessageBuffer::ReadException("Cannot read from empty message buffer");
  }

  val = msg.front();
  msg.pop();
  return msg;
}

MessageBuffer&
operator<<(MessageBuffer& msg, const std::vector<uint8_t>& val)
{
  msg << static_cast<uintVar_t>(val.size());
  msg.push(val);
  return msg;
}

MessageBuffer&
operator<<(MessageBuffer& msg, std::vector<uint8_t>&& val)
{
  msg << static_cast<uintVar_t>(val.size());
  msg.push(std::move(val));
  return msg;
}

MessageBuffer&
operator>>(MessageBuffer& msg, std::vector<uint8_t>& val)
{
  uintVar_t vec_size = 0;
  msg >> vec_size;

  val = msg.pop_front(vec_size);
  return msg;
}

MessageBuffer&
operator<<(MessageBuffer& msg, const uintVar_t& v)
{
  uint64_t val = v;

  if (val < ((uint64_t)1 << 7)) {
    msg.push(uint8_t(((val >> 0) & 0x7F)) | 0x00);
    return msg;
  }

  if (val < ((uint64_t)1 << 14)) {
    msg.push(uint8_t(((val >> 8) & 0x3F) | 0x80));
    msg.push(uint8_t((val >> 0) & 0xFF));
    return msg;
  }

  if (val < ((uint64_t)1 << 29)) {
    msg.push(uint8_t(((val >> 24) & 0x1F) | 0x80 | 0x40));
    msg.push(uint8_t((val >> 16) & 0xFF));
    msg.push(uint8_t((val >> 8) & 0xFF));
    msg.push(uint8_t((val >> 0) & 0xFF));
    return msg;
  }

  msg.push(uint8_t(((val >> 56) & 0x0F) | 0x80 | 0x40 | 0x20));
  msg.push(uint8_t((val >> 48) & 0xFF));
  msg.push(uint8_t((val >> 40) & 0xFF));
  msg.push(uint8_t((val >> 32) & 0xFF));
  msg.push(uint8_t((val >> 24) & 0xFF));
  msg.push(uint8_t((val >> 16) & 0xFF));
  msg.push(uint8_t((val >> 8) & 0xFF));
  msg.push(uint8_t((val >> 0) & 0xFF));

  return msg;
}

MessageBuffer&
operator>>(MessageBuffer& msg, uintVar_t& v)
{
  uint8_t byte[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
  uint8_t first = msg.front();

  if ((first & (0x80)) == 0) {
    msg >> byte[0];
    uint8_t val = ((byte[0] & 0x7F) << 0);
    v = val;
    return msg;
  }

  if ((first & (0x80 | 0x40)) == 0x80) {
    msg >> byte[1];
    msg >> byte[0];
    uint16_t val = (((uint16_t)byte[1] & 0x3F) << 8) + ((uint16_t)byte[0] << 0);
    v = val;
    return msg;
  }

  if ((first & (0x80 | 0x40 | 0x20)) == (0x80 | 0x40)) {
    msg >> byte[3];
    msg >> byte[2];
    msg >> byte[1];
    msg >> byte[0];
    uint32_t val = ((uint32_t)(byte[3] & 0x1F) << 24) +
                   ((uint32_t)byte[2] << 16) + ((uint32_t)byte[1] << 8) +
                   ((uint32_t)byte[0] << 0);
    v = val;
    return msg;
  }

  msg >> byte[7];
  msg >> byte[6];
  msg >> byte[5];
  msg >> byte[4];
  msg >> byte[3];
  msg >> byte[2];
  msg >> byte[1];
  msg >> byte[0];
  uint64_t val = ((uint64_t)(byte[7] & 0x0F) << 56) +
                 ((uint64_t)(byte[6]) << 48) + ((uint64_t)(byte[5]) << 40) +
                 ((uint64_t)(byte[4]) << 32) + ((uint64_t)(byte[3]) << 24) +
                 ((uint64_t)(byte[2]) << 16) + ((uint64_t)(byte[1]) << 8) +
                 ((uint64_t)(byte[0]) << 0);
  v = val;
  return msg;
}

} // namespace quicr::messages
