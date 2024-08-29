// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "moq/message_buffer.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <vector>

namespace moq::messages {

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

MessageBuffer::MessageBuffer(const buffer_type& buffer)
  : _buffer{ buffer }
{
}

MessageBuffer::MessageBuffer(buffer_type&& buffer)
  : _buffer{ std::move(buffer) }
{
}

void
MessageBuffer::push(buffer_type&& data)
{
  _buffer.insert(_buffer.end(),
                 std::make_move_iterator(data.begin()),
                 std::make_move_iterator(data.end()));
}

void
MessageBuffer::pop(size_t length)
{
  if (length == 0) {
    return;
  }

  cleanup(length);
};

const uint8_t&
MessageBuffer::front() const
{
  if (empty()) {
    throw EmptyException();
  }

  return *begin();
}

MessageBuffer::span_type
MessageBuffer::front(size_t length) const
{
  if (length == 0) {
    return {};
  }

  if (empty()) {
    throw EmptyException();
  }

  if (length > size()) {
    throw OutOfRangeException(length, size());
  }

  if (length == size()) {
    return _buffer;
  }

  return { data(), length };
}

uint8_t
MessageBuffer::pop_front()
{
  if (empty()) {
    throw EmptyException();
  }

  const auto value = *std::make_move_iterator(begin());
  cleanup();

  return value;
}

MessageBuffer::buffer_type
MessageBuffer::pop_front(size_t length)
{
  if (length == 0) {
    return {};
  }

  if (empty()) {
    throw EmptyException();
  }

  if (length > size()) {
    throw OutOfRangeException(length, size());
  }

  if (length == size()) {
    return take();
  }

  buffer_type result(length);
  std::copy_n(std::make_move_iterator(begin()), length, result.begin());
  cleanup(length);

  return result;
}

MessageBuffer::buffer_type&&
MessageBuffer::take()
{
  _buffer.erase(_buffer.begin(), this->begin());
  _read_offset = 0;
  return std::move(_buffer);
}

std::string
MessageBuffer::to_hex() const
{
  std::ostringstream hex;
  hex << std::hex << std::setfill('0') << std::uppercase;
  for (const auto& byte : *this) {
    hex << std::setw(2) << int(byte);
  }
  return hex.str();
}

/*===========================================================================*/
// Stream operators
/*===========================================================================*/

MessageBuffer&
MessageBuffer::operator<<(const uint8_t& value)
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

void
MessageBuffer::cleanup(size_t length)
{
  if (empty()) {
    throw EmptyException();
  }

  if (length > size()) {
    throw OutOfRangeException(length, size());
  }

  _read_offset += length;
  if (!empty()) {
    return;
  }

  _read_offset = 0;
  _buffer.clear();
}
} // namespace moq::messages
