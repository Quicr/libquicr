#include <quicr/quicr_name.h>
#include <quicr/message_buffer.h>

#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

namespace quicr {
static constexpr size_t uint_type_bit_size = sizeof(Name::uint_type) * 8;

Name::Name()
  : _hi{ 0 }
  , _low{ 0 }
{
}

Name::Name(const Name& other)
  : _hi{ other._hi }
  , _low{ other._low }
{
}

Name::Name(Name&& other)
  : _hi{ std::move(other._hi) }
  , _low{ std::move(other._low) }
{
}

Name::Name(const std::string& hex_value)
{
  std::string clean_hex = hex_value;
  auto found = clean_hex.substr(0, 2).find("0x");
  if (found != std::string::npos)
    clean_hex.erase(found, 2);

  const auto size_of = sizeof(Name::uint_type) * 2;
  if (clean_hex.length() > size_of * 2)
    throw NameException("Hex string cannot be longer than " +
                        std::to_string(size_of * 2) + " bytes");

  if (clean_hex.length() > size_of) {
    size_t midpoint = clean_hex.length() - size_of;
    _hi = std::stoull(clean_hex.substr(0, midpoint), nullptr, 16);
    _low = std::stoull(clean_hex.substr(midpoint, size_of), nullptr, 16);
  } else {
    _hi = 0;
    _low = std::stoull(clean_hex, nullptr, 16);
  }
}

Name::Name(uint8_t* data, size_t length)
  : Name(std::vector<uint8_t>{ data, data + length })
{
}

Name::Name(const uint8_t* data, size_t length)
  : Name(std::vector<uint8_t>{ data, data + length })
{
}

Name::Name(const std::vector<uint8_t>& data)
{
  auto midpoint = std::prev(data.end(), sizeof(uint_type));

  std::vector<uint8_t> hi_bits{ data.begin(), midpoint };
  std::memcpy(&_hi, hi_bits.data(), hi_bits.size());

  std::vector<uint8_t> low_bits{ midpoint, data.end() };
  std::memcpy(&_low, low_bits.data(), low_bits.size());
}

std::string
Name::to_hex() const
{
  constexpr uint8_t size_of = sizeof(uint_type) * 2;

  std::ostringstream stream;
  stream << "0x" << std::hex << std::setfill('0');
  stream << std::setw(size_of) << _hi;
  stream << std::setw(size_of) << _low;
  
  return stream.str();
}

Name
Name::operator>>(uint16_t value) const
{
  Name name(*this);
  name >>= value;
  return name;
}

Name
Name::operator>>=(uint16_t value)
{
  if (value == 0)
    return *this;

  if (value < uint_type_bit_size)
  {
    _low = _low >> value;
    _low |= _hi << (uint_type_bit_size - value);
    _hi = _hi >> value;
  }
  else
  {
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

  if (value < uint_type_bit_size)
  {
    _hi = _hi << value;
    _hi |= _low >> (uint_type_bit_size - value);
    _low = _low << value;
  }
  else
  {
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
  if (_low + value < _low)
  {
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
  if (_low - value > _low)
  {
    --_hi;
  }
  _low -= value;
}

Name Name::operator++()
{
  *this += 1;
  return *this;
}

Name Name::operator++(int)
{
  Name name(*this);
  ++(*this);
  return name;
}

Name Name::operator--()
{
  *this -= 1;
  return *this;
}

Name Name::operator--(int)
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

Name&
Name::operator=(const Name& other)
{
  _hi = other._hi;
  _low = other._low;
  return *this;
}

Name&
Name::operator=(Name&& other)
{
  _hi = std::move(other._hi);
  _low = std::move(other._low);
  return *this;
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
  return a._hi >= b._hi && a._low > b._low;
}

bool
operator<(const Name& a, const Name& b)
{
  return a._hi <= b._hi && a._low < b._low;
}

std::ostream&
operator<<(std::ostream& os, const Name& name)
{
  os << name.to_hex();
  return os;
}

std::vector<uint8_t>
Name::data() const
{
  std::vector<uint8_t> result(sizeof(uint_type) * 2);

  for (uint8_t i = 0; i < sizeof(uint_type); ++i)
  {
    result[i] = static_cast<uint8_t>(( _hi >> 8 * i));
    result[i + sizeof(uint_type)] = static_cast<uint8_t>((_low >> 8 * i));
  }

  return result;
}

void
operator<<(messages::MessageBuffer& msg, const Name& val)
{
  msg << val.data();
}

bool
operator>>(messages::MessageBuffer& msg, Name& val)
{
  std::vector<uint8_t> bytes{};
  if (!(msg >> bytes))
    return false;

  val = Name{bytes};

  return true;
}
}
