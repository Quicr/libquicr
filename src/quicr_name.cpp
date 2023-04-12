#include <quicr/message_buffer.h>
#include <quicr/quicr_name.h>

#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

namespace quicr {
Name::Name(uint8_t* data, size_t length)
{
  size_t size_of = std::min(length, size() / 2);
  std::memcpy(&_low, data, size_of);
  std::memcpy(&_hi, data + size_of, size_of);
}

Name::Name(const uint8_t* data, size_t length)
{
  if (length > size() * 2)
    throw NameException("");

  size_t size_of = std::min(length, size() / 2);
  std::memcpy(&_low, data, size_of);
  std::memcpy(&_hi, data + size_of, size_of);
}

Name::Name(const std::vector<uint8_t>& data)
{
  size_t size_of = std::min(data.size(), size() / 2);
  std::memcpy(&_low, data.data(), size_of);
  std::memcpy(&_hi, data.data() + size_of, size_of);
}

std::string
Name::to_hex() const
{
  std::string hex = "0x";
  hex += uint_to_hex(_hi);
  hex += uint_to_hex(_low);

  return hex;
}

std::uint8_t
Name::operator[](std::size_t index) const
{
  if (index >= size())
    throw std::out_of_range(
      "Cannot access index outside of max size of quicr::Name");

  if (index < sizeof(uint_type))
    return (_low >> (index * 8)) & 0xff;
  return (_hi >> ((index - sizeof(uint_type)) * 8)) & 0xff;
}

Name
Name::operator>>(uint16_t value) const
{
  Name name(*this);
  name >>= value;
  return name;
}

static constexpr size_t uint_type_bit_size = Name::size() * 4;

Name
Name::operator>>=(uint16_t value)
{
  if (value == 0)
    return *this;

  if (value < uint_type_bit_size) {
    _low = _low >> value;
    _low |= _hi << (uint_type_bit_size - value);
    _hi = _hi >> value;
  } else {
    _low = _hi >> (value - uint_type_bit_size);
    _hi = 0;
  }

  return *this;
}

Name
Name::operator<<(uint16_t value) const
{
  Name name(*this);
  name <<= value;
  return name;
}

Name
Name::operator<<=(uint16_t value)
{
  if (value == 0)
    return *this;

  if (value < uint_type_bit_size) {
    _hi = _hi << value;
    _hi |= _low >> (uint_type_bit_size - value);
    _low = _low << value;
  } else {
    _hi = _low << (value - uint_type_bit_size);
    _low = 0;
  }

  return *this;
}

Name
Name::operator+(uint_type value) const
{
  Name name(*this);
  name += value;
  return name;
}

void
Name::operator+=(uint_type value)
{
  if (_low + value < _low) {
    ++_hi;
  }
  _low += value;
}

Name
Name::operator-(uint_type value) const
{
  Name name(*this);
  name -= value;
  return name;
}

void
Name::operator-=(uint_type value)
{
  if (_low - value > _low) {
    --_hi;
  }
  _low -= value;
}

Name
Name::operator++()
{
  *this += 1;
  return *this;
}

Name
Name::operator++(int)
{
  Name name(*this);
  ++(*this);
  return name;
}

Name
Name::operator--()
{
  *this -= 1;
  return *this;
}

Name
Name::operator--(int)
{
  Name name(*this);
  --(*this);
  return name;
}

Name
Name::operator&(uint_type value) const
{
  Name name(*this);
  name &= value;
  return name;
}

void
Name::operator&=(uint_type value)
{
  _low &= value;
}

Name
Name::operator|(uint_type value) const
{
  Name name(*this);
  name |= value;
  return name;
}

void
Name::operator|=(uint_type value)
{
  _low |= value;
}

Name
Name::operator&(const Name& other) const
{
  Name name = *this;
  name &= other;
  return name;
}

void
Name::operator&=(const Name& other)
{
  _hi &= other._hi;
  _low &= other._low;
}

Name
Name::operator|(const Name& other) const
{
  Name name = *this;
  name |= other;
  return name;
}

void
Name::operator|=(const Name& other)
{
  _hi |= other._hi;
  _low |= other._low;
}

Name
Name::operator^(const Name& other) const
{
  Name name = *this;
  name ^= other;
  return name;
}

void
Name::operator^=(const Name& other)
{
  _hi ^= other._hi;
  _low ^= other._low;
}

bool
operator==(const Name& a, const Name& b)
{
  return a._hi == b._hi && a._low == b._low;
}

bool
operator!=(const Name& a, const Name& b)
{
  return !(a == b);
}

bool
operator>(const Name& a, const Name& b)
{
  if (a._hi > b._hi)
    return true;
  if (b._hi > a._hi)
    return false;
  return a._low > b._low;
}

bool
operator<(const Name& a, const Name& b)
{
  if (a._hi < b._hi)
    return true;
  if (b._hi < a._hi)
    return false;
  return a._low < b._low;
}

std::ostream&
operator<<(std::ostream& os, const Name& name)
{
  os << name.to_hex();
  return os;
}
}
