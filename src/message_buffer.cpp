#include <quicr/message_buffer.h>

#include <quicr/uvarint.h>

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <vector>

namespace quicr::messages {

/*===========================================================================*/
// Exception definitions
/*===========================================================================*/

MessageBuffer::EmptyException::EmptyException()
  : MessageBuffer::ReadException("buffer cannot be empty when reading")
{
}

MessageBuffer::OutOfRangeException::OutOfRangeException(size_t length,
                                                        size_t buffer_length)
  : MessageBuffer::ReadException(
      "length is longer than the size of the buffer: " +
      std::to_string(length) + " > " + std::to_string(buffer_length))
{
}

MessageBuffer::LengthException::LengthException(size_t data_length,
                                                size_t expected_length)
  : MessageBuffer::ReadException(
      "length of decoded data must match separately decoded length: " +
      std::to_string(data_length) + " != " + std::to_string(expected_length))
{
}

/*===========================================================================*/
// MessageBuffer definitions
/*===========================================================================*/

MessageBuffer::MessageBuffer(const std::vector<uint8_t>& buffer)
  : _buffer{ buffer }
{
}

MessageBuffer::MessageBuffer(std::vector<uint8_t>&& buffer)
  : _buffer{ std::move(buffer) }
{
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
MessageBuffer::pop(uint16_t length)
{
  if (length == 0)
    return;

  pop_or_clear(length);
};

std::vector<uint8_t>
MessageBuffer::front(uint16_t length) const
{
  if (length == 0)
    return {};

  if (length > size())
    throw OutOfRangeException(length, size());

  if (length == size())
    return _buffer;

  const auto& it = front_it();
  return { it, it + length };
}

uint8_t
MessageBuffer::pop_front()
{
  uint8_t value = *front_move_it();
  pop_or_clear();
  return value;
}

std::vector<uint8_t>
MessageBuffer::pop_front(uint16_t length)
{
  if (length == 0)
    return {};

  if (length > size())
    throw OutOfRangeException(length, size());

  if (length == size())
    return take();

  std::vector<uint8_t> result(length);
  std::copy_n(front_move_it(), length, result.begin());
  pop_or_clear(length);

  return result;
}

std::vector<uint8_t>&&
MessageBuffer::take()
{
  _buffer.erase(_buffer.begin(), std::next(_buffer.begin(), _read_offset));
  _read_offset = 0;
  return std::move(_buffer);
}

std::string
MessageBuffer::to_hex() const
{
  std::ostringstream hex;
  hex << std::hex << std::setfill('0') << std::uppercase;
  for (auto it = front_it(); it != _buffer.end(); ++it)
    hex << std::setw(2) << int(*it);
  return hex.str();
}

/*===========================================================================*/
// Stream operators
/*===========================================================================*/

MessageBuffer&
MessageBuffer::operator<<(uint8_t value)
{
  _buffer.push_back(value);
  return *this;
}

MessageBuffer&
MessageBuffer::operator>>(uint8_t& value)
{
  value = pop_front();
  return *this;
}

/*===========================================================================*/
// Helper Methods
/*===========================================================================*/

MessageBuffer::buffer_t::iterator
MessageBuffer::front_it()
{
  if (empty())
    throw EmptyException();

  return std::next(_buffer.begin(), _read_offset);
}

MessageBuffer::buffer_t::const_iterator
MessageBuffer::front_it() const
{
  if (empty())
    throw EmptyException();

  return std::next(_buffer.begin(), _read_offset);
}

std::move_iterator<MessageBuffer::buffer_t::iterator>
MessageBuffer::front_move_it()
{
  return std::make_move_iterator(front_it());
}

void
MessageBuffer::pop_or_clear(size_t length)
{
  if (empty())
    throw EmptyException();

  if (length > size())
    throw OutOfRangeException(length, size());

  _read_offset += length;
  if (!empty())
    return;

  _read_offset = 0;
  _buffer.clear();
}
} // namespace quicr::messages
