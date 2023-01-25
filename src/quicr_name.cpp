#include <quicr/quicr_name.h>

#include <bitset>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <tuple>

#include <iostream>

namespace quicr
{
Name::Name(uint64_t value) : _hi{0}, _low{value} {}

// Note: This assumes string has 0x prefix!
Name::Name(const std::string& hex_value)
{
    const auto size_of = sizeof(uint64_t);
    if (hex_value.length() - 2 > size_of * 2)
    {
        std::string hi_bits = hex_value.substr(0, size_of + 2);
        std::string low_bits = hex_value.substr(size_of, size_of);
        
        _hi = std::stoull(hi_bits, nullptr, 16);
        _low = std::stoull("0x" + low_bits, nullptr, 16);
    }
    else
    {
        _hi = 0;
        _low = std::stoull(hex_value, nullptr, 16);
    }
}

Name::Name(uint8_t* data, size_t length)
{
    const size_t size_of = sizeof(uint64_t);

    for (size_t i = 0; i < std::min(length, size_of); ++i)
    {
        _low |= (data[i] << 8 * i);
    }

    for (size_t i = size_of; i < length; ++i)
    {
        _hi |= (data[i] << 8 * i);
    }
}

Name::Name(const std::vector<uint8_t>& data)
{
    const size_t size_of = sizeof(uint64_t);

    for (size_t i = 0; i < std::min(data.size(), size_of); ++i)
    {
        _low |= (data[i] << 8 * i);
    }

    for (size_t i = size_of; i < data.size(); ++i)
    {
        _hi |= (data[i] << 8 * i);
    }
}

Name::Name(const Name& other) : _hi{other._hi}, _low{other._low} {}

Name::Name(Name&& other) : _hi{std::move(other._hi)}, _low{std::move(other._low)} {}

std::vector<uint8_t> Name::data() const
{
    auto make_bytes = [](uint64_t v) {
        std::vector<uint8_t> result(sizeof(uint64_t));
        for (size_t i = 0; i < sizeof(uint64_t); ++i) {
            result[i] = static_cast<uint8_t>((v >> 8 * i));
        }
        return result;
    };

    std::vector<uint8_t> bytes = make_bytes(_hi);
    std::vector<uint8_t> low_bytes = make_bytes(_low);
    bytes.insert(bytes.end(), low_bytes.begin(), low_bytes.end());

    return bytes;
}

size_t Name::size() const
{
    return data().size();
}

std::string Name::to_hex() const
{
  std::stringstream stream;
  stream << "0x"
         << std::setfill ('0') 
         << std::setw(sizeof(uint64_t)*2)
         << std::hex 
         << _hi
         << std::setw(sizeof(uint64_t)*2)
         << _low;
  return stream.str();
}

using bitset_t = std::bitset<128>;
static const bitset_t bitset_divider(std::numeric_limits<uint64_t>::max());
static bitset_t make_bitset(uint64_t low, uint64_t hi)
{
    bitset_t bits;
    bits |= bitset_t(low) | (bitset_t(hi) << 64);
    return bits;
}

static std::pair<uint64_t, uint64_t> split_bitset(const bitset_t& bits)
{
    uint64_t a = (bits & bitset_divider).to_ullong();
    uint64_t b = (bits >> 64 & bitset_divider).to_ullong();
    return {a, b};
}

Name Name::operator>>(uint16_t value)
{
    auto bits = make_bitset(_low, _hi);
    bits >> value;
    std::tie(_low, _hi) = split_bitset(bits);

    return *this;
}

Name Name::operator<<(uint16_t value)
{
    auto bits = make_bitset(_low, _hi);
    bits << value;
    std::tie(_low, _hi) = split_bitset(bits);

    return *this;
}

static bitset_t add_bitset(const bitset_t& x, const bitset_t& y)
{
    auto full_adder = [](bool a, bool b, bool& carry) {
        bool sum = a ^ b ^ carry;
        carry = (a && b) || (a && carry) || (b && carry);
        return sum;
    };

    bool carry = false;
    bitset_t result;
    for (size_t i = 0; i < x.size(); ++i)
    {
        result[i] = full_adder(x[i], y[i], carry);
    }

    return result;
}

Name Name::operator+(uint64_t value)
{
    auto bits = make_bitset(_low, _hi);
    bitset_t value_bits(value);

    auto result_bits = add_bitset(bits, value_bits);
    std::tie(_low, _hi) = split_bitset(result_bits);

    return *this;
}

static bitset_t sub_bitset(const bitset_t& x, const bitset_t& y)
{
    auto full_subtractor = [](bool a, bool b, bool& borrow) {
        bool diff = a ^ b ^ borrow;
        borrow = (!a && borrow) || (!a && b) || (b && borrow);
        return diff;
    };

    bool carry = false;
    bitset_t result;
    for (size_t i = 0; i < x.size(); ++i)
    {
        result[i] = full_subtractor(x[i], y[i], carry);
    }

    return result;
}
Name Name::operator-(uint64_t value)
{
    auto bits = make_bitset(_low, _hi);
    bitset_t value_bits(value);

    auto result_bits = sub_bitset(bits, value_bits);
    std::tie(_low, _hi) = split_bitset(result_bits);

    return *this;   
}

Name Name::operator&(uint64_t value)
{
    // TODO: No way that's enough, figure out what more to do.
    _low &= value;
    return *this;
}

Name Name::operator|(uint64_t value)
{
    // TODO: No way that's enough, figure out what more to do.
    _low |= value;
    return *this;
}

Name Name::operator&(const Name& other)
{
    _hi &= other._hi;
    _low &= other._low;
    return *this;
}

Name Name::operator|(const Name& other)
{
    _hi |= other._hi;
    _low |= other._low;
    return *this;
}

Name& Name::operator=(const Name& other)
{
    _hi = other._hi;
    _low = other._low;
    return *this;
}

Name& Name::operator=(Name&& other)
{
    _hi = std::move(other._hi);
    _low = std::move(other._low);
    return *this;
}

bool operator==(const Name& a, const Name& b)
{
    return std::tie(a._hi, a._low) == std::tie(b._hi, b._low);
}

bool operator!=(const Name& a, const Name& b)
{
    return !(a == b);
}

bool operator>(const Name& a, const Name& b)
{
    return std::tie(a._hi, a._low) > std::tie(b._hi, b._low);
}

bool operator<(const Name& a, const Name& b)
{
    return std::tie(a._hi, b._low) < std::tie(b._hi, b._low);
}
}
