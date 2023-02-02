#include <quicr/quicr_name.h>
#include <quicr/message_buffer.h>

#include <bitset>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <tuple>
#include <cstring>

namespace quicr {
Name::Name()
  : _hi{ 0 }
  , _low{ 0 }
{
}

Name::Name(const std::string& hex_value)
{
  std::string clean_hex = hex_value;
  auto found = clean_hex.find("0x");
  if (found != std::string::npos)
    clean_hex.erase(found, 2);

  const auto size_of = sizeof(Name::uint_type) * 2;
  if (clean_hex.length() > size_of * 2)
    throw NameException("Hex string cannot be longer than " +
                        std::to_string(size_of * 2) + " bytes");

  if (clean_hex.length() > size_of) {
    size_t midpoint = clean_hex.length() - size_of;

    std::string low_bits = "0x" + clean_hex.substr(midpoint, size_of);
    clean_hex.erase(midpoint, size_of);
    std::string hi_bits = "0x" + clean_hex;

    _hi = std::stoull(hi_bits, nullptr, 16);
    _low = std::stoull(low_bits, nullptr, 16);
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
  const size_t size_of = sizeof(Name::uint_type);
  auto midpoint = std::prev(data.end(), size_of);

  std::vector<uint8_t> hi_bits{ data.begin(), midpoint };
  std::memcpy(&_hi, hi_bits.data(), hi_bits.size());

  std::vector<uint8_t> low_bits{ midpoint, data.end() };
  std::memcpy(&_low, low_bits.data(), low_bits.size());
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

std::vector<uint8_t>
Name::data() const
{
  auto make_bytes = [](Name::uint_type v) {
    std::vector<uint8_t> result(sizeof(Name::uint_type));
    for (size_t i = 0; i < sizeof(Name::uint_type); ++i) {
      result[i] = static_cast<uint8_t>((v >> 8 * i));
    }
    return result;
  };

  std::vector<uint8_t> bytes = make_bytes(_hi);
  std::vector<uint8_t> low_bytes = make_bytes(_low);
  bytes.insert(bytes.end(), low_bytes.begin(), low_bytes.end());

  return bytes;
}

size_t
Name::size() const
{
  return data().size();
}

std::string
Name::to_hex() const
{
  std::stringstream stream;
  stream << "0x" << std::hex;

  stream << std::setw(sizeof(uint_type) * 2) << std::setfill('0');
  stream << _hi;
  stream << std::setw(sizeof(uint_type) * 2) << std::setfill('0');
  stream << _low;
  
  return stream.str();
}

static const size_t uint_type_bit_size = sizeof(Name::uint_type) * 8;
static const size_t max_uint_type_bit_size = uint_type_bit_size * 2;
using bitset_t = std::bitset<max_uint_type_bit_size>;
static const bitset_t bitset_divider(
  std::numeric_limits<Name::uint_type>::max());

static bitset_t
make_bitset(Name::uint_type low, Name::uint_type hi)
{
  bitset_t bits;
  bits |= bitset_t(low) | (bitset_t(hi) << uint_type_bit_size);
  return bits;
}

static std::pair<Name::uint_type, Name::uint_type>
split_bitset(const bitset_t& bits)
{
  Name::uint_type a = (bits & bitset_divider).to_ullong();
  Name::uint_type b = (bits >> uint_type_bit_size & bitset_divider).to_ullong();
  return { a, b };
}

Name
Name::operator>>(uint16_t value) const
{
  auto bits = make_bitset(_low, _hi);
  bits >>= value;
  Name name(*this);
  std::tie(name._low, name._hi) = split_bitset(bits);

  return name;
}

Name
Name::operator<<(uint16_t value) const
{
  auto bits = make_bitset(_low, _hi);
  bits <<= value;
  Name name(*this);
  std::tie(name._low, name._hi) = split_bitset(bits);

  return name;
}

static bitset_t
add_bitset(const bitset_t& x, const bitset_t& y)
{
  auto full_adder = [](bool a, bool b, bool& carry) {
    bool sum = a ^ b ^ carry;
    carry = (a && b) || (a && carry) || (b && carry);
    return sum;
  };

  bool carry = false;
  bitset_t result;
  for (size_t i = 0; i < x.size(); ++i) {
    result[i] = full_adder(x[i], y[i], carry);
  }

  return result;
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
  auto bits = make_bitset(_low, _hi);
  bitset_t value_bits(value);

  auto result_bits = add_bitset(bits, value_bits);
  std::tie(_low, _hi) = split_bitset(result_bits);
}

static bitset_t
sub_bitset(const bitset_t& x, const bitset_t& y)
{
  auto full_subtractor = [](bool a, bool b, bool& borrow) {
    bool diff = a ^ b ^ borrow;
    borrow = (!a && borrow) || (!a && b) || (b && borrow);
    return diff;
  };

  bool borrow = false;
  bitset_t result;
  for (size_t i = 0; i < x.size(); ++i) {
    result[i] = full_subtractor(x[i], y[i], borrow);
  }

  return result;
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
  auto bits = make_bitset(_low, _hi);
  bitset_t value_bits(value);

  auto result_bits = sub_bitset(bits, value_bits);
  std::tie(_low, _hi) = split_bitset(result_bits);
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
  return std::tie(a._hi, a._low) == std::tie(b._hi, b._low);
}

bool
operator!=(const Name& a, const Name& b)
{
  return !(a == b);
}

bool
operator>(const Name& a, const Name& b)
{
  return std::tie(a._hi, a._low) > std::tie(b._hi, b._low);
}

bool
operator<(const Name& a, const Name& b)
{
  return std::tie(a._hi, a._low) < std::tie(b._hi, b._low);
}

std::ostream&
operator<<(std::ostream& os, const Name& name)
{
  os << name.to_hex();
  return os;
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
