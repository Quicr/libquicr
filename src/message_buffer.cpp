#include <quicr/message_buffer.h>
#include <quicr/uvarint.h>

#include <algorithm>
#include <array>
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
  const auto length = _buffer.size();
  _buffer.resize(length + data.size());
  std::memcpy(_buffer.data() + length, data.data(), data.size());
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
MessageBuffer::front(uint16_t len) const
{
  if (len == 0)
    return {};

  if (len > _buffer.size())
    throw OutOfRangeException(
      "len cannot be longer than the size of the buffer");

  if (len == _buffer.size())
    return _buffer;

  return { std::begin(_buffer), std::next(std::begin(_buffer), len) };
}

std::vector<uint8_t>
MessageBuffer::pop_front(uint16_t len)
{
  if (len == 0)
    return {};

  if (len > _buffer.size())
    throw OutOfRangeException(
      "len cannot be longer than the size of the buffer");

  if (len == _buffer.size())
    return std::move(_buffer);

  std::vector<uint8_t> front(len);
  std::copy_n(
    std::make_move_iterator(std::begin(_buffer)), len, std::begin(front));
  _buffer.erase(std::begin(_buffer), std::next(std::begin(_buffer), len));

  return front;
}

std::vector<uint8_t>
MessageBuffer::get()
{
  return std::move(_buffer);
}

std::vector<uint8_t>&&
MessageBuffer::take()
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

} // namespace quicr::messages
