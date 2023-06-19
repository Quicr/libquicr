#include <quicr/message_buffer.h>
#include <quicr/quicr_name.h>

#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

namespace quicr {
Name::Name(uint8_t* data, size_t length)
{
  if (length > sizeof(Name) * 2)
    throw NameException(
      "Byte array length cannot be longer than length of Name: " +
      std::to_string(sizeof(Name) * 2));

  constexpr size_t size_of = sizeof(Name) / 2;
  std::memcpy(&_low, data, size_of);
  std::memcpy(&_hi, data + size_of, size_of);
}

Name::Name(const uint8_t* data, size_t length)
{
  if (length > sizeof(Name) * 2)
    throw NameException(
      "Byte array length cannot be longer than length of Name: " +
      std::to_string(sizeof(Name) * 2));

  constexpr size_t size_of = sizeof(Name) / 2;
  std::memcpy(&_low, data, size_of);
  std::memcpy(&_hi, data + size_of, size_of);
}

Name::Name(const std::vector<uint8_t>& data)
{
  if (data.size() > sizeof(Name))
    throw NameException(
      "Byte array length cannot be longer than length of Name: " +
      std::to_string(sizeof(Name)));

  constexpr size_t size_of = sizeof(Name) / 2;
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
  if (index >= sizeof(Name))
    throw std::out_of_range(
      "Cannot access index outside of max size of quicr::Name");

  if (index < sizeof(uint64_t))
    return (_low >> (index * 8)) & 0xff;
  return (_hi >> ((index - sizeof(uint64_t)) * 8)) & 0xff;
}

Name
Name::operator>>(uint16_t value) const
{
  Name name(*this);
  name >>= value;
  return name;
}

static constexpr size_t uint64_t_bit_size = sizeof(Name) * 4;

Name
Name::operator>>=(uint16_t value)
{
  if (value == 0)
    return *this;

  if (value < uint64_t_bit_size) {
    _low = _low >> value;
    _low |= _hi << (uint64_t_bit_size - value);
    _hi = _hi >> value;
  } else {
    _low = _hi >> (value - uint64_t_bit_size);
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

  if (value < uint64_t_bit_size) {
    _hi = _hi << value;
    _hi |= _low >> (uint64_t_bit_size - value);
    _low = _low << value;
  } else {
    _hi = _low << (value - uint64_t_bit_size);
    _low = 0;
  }

  return *this;
}

Name
Name::operator+(uint64_t value) const
{
  Name name(*this);
  name += value;
  return name;
}

void
Name::operator+=(uint64_t value)
{
  if (_low + value < _low) {
    ++_hi;
  }
  _low += value;
}

Name
Name::operator+(Name value) const
{
  Name name(*this);
  name += value;
  return name;
}

void
Name::operator+=(Name value)
{
  if (_low + value._low < _low) {
    ++_hi;
  }
  _low += value._low;
  _hi += value._hi;
}

Name
Name::operator-(uint64_t value) const
{
  Name name(*this);
  name -= value;
  return name;
}

void
Name::operator-=(uint64_t value)
{
  if (_low - value > _low) {
    --_hi;
  }
  _low -= value;
}

Name
Name::operator-(Name value) const
{
  Name name(*this);
  name -= value;
  return name;
}

void
Name::operator-=(Name value)
{
  if (_hi - value._hi > _hi) {
    _hi = 0;
    --_low;
  } else {
    _hi -= value._hi;
  }
  *this -= value._low;
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
Name::operator&(uint64_t value) const
{
  Name name(*this);
  name &= value;
  return name;
}

void
Name::operator&=(uint64_t value)
{
  _low &= value;
}

Name
Name::operator|(uint64_t value) const
{
  Name name(*this);
  name |= value;
  return name;
}

void
Name::operator|=(uint64_t value)
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
