#pragma once

#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace quicr {
namespace messages {
class MessageBuffer;
}

/**
 * Compile-time hex string to unsigned integer conversion.
 */
constexpr uint64_t
hex_to_uint(const std::string_view& x)
{
  size_t start_pos = x.substr(0, 2) == "0x" ? 2 : 0;

  uint64_t y = 0;
  for (size_t i = start_pos; i < x.length(); ++i) {
    y *= 16ull;
    if ('0' <= x[i] && x[i] <= '9')
      y += x[i] - '0';
    else if ('A' <= x[i] && x[i] <= 'F')
      y += x[i] - 'A' + 10;
    else if ('a' <= x[i] && x[i] <= 'f')
      y += x[i] - 'a' + 10;
  }

  return y;
}

/**
 * Name specific exception, thrown only on creation of name where string
 * byte count is too long.
 */
struct NameException : public std::runtime_error
{
  using std::runtime_error::runtime_error;
};

/**
 * @brief Name class used for passing data in bits.
 */
class Name
{
  using uint_type = uint64_t;

public:
  constexpr Name() = default;
  constexpr Name(const Name& other) = default;
  Name(Name&& other) = default;
  Name(const std::string& x);
  Name(uint8_t* data, size_t length);
  Name(const uint8_t* data, size_t length);
  Name(const std::vector<uint8_t>& data);
  constexpr Name(const std::string_view& x)
  {
    size_t start_pos = (x.substr(0, 2) == "0x") * 2;
    auto hex_value = x.substr(start_pos, x.length() - start_pos);

    if (hex_value.length() > size() * 2)
      throw NameException("Hex string cannot be longer than " +
                          std::to_string(size() * 2) + " bytes");

    if (hex_value.length() > size()) {
      _hi = hex_to_uint(hex_value.substr(0, hex_value.length() - size()));
      _low = hex_to_uint(hex_value.substr(hex_value.length() - size(), size()));
    } else {
      _hi = 0;
      _low = hex_to_uint(x.substr(0, x.length()));
    }
  }

  ~Name() = default;

  std::string to_hex() const;
  std::uint8_t operator[](std::size_t offset) const;
  static constexpr size_t size() { return sizeof(uint_type) * 2; }

  Name operator>>(uint16_t value) const;
  Name operator>>=(uint16_t value);
  Name operator<<(uint16_t value) const;
  Name operator<<=(uint16_t value);
  Name operator+(uint_type value) const;
  void operator+=(uint_type value);
  Name operator++();
  Name operator++(int);
  Name operator--();
  Name operator--(int);
  Name operator-(uint_type value) const;
  void operator-=(uint_type value);
  Name operator&(uint_type value) const;
  void operator&=(uint_type value);
  Name operator|(uint_type value) const;
  void operator|=(uint_type value);
  Name operator&(const Name& other) const;
  void operator&=(const Name& other);
  Name operator|(const Name& other) const;
  void operator|=(const Name& other);
  Name operator^(const Name& other) const;
  void operator^=(const Name& other);
  Name operator~() const;

  constexpr Name& operator=(const Name& other) = default;
  constexpr Name& operator=(Name&& other) = default;

  friend bool operator<(const Name& a, const Name& b);
  friend bool operator>(const Name& a, const Name& b);
  friend bool operator==(const Name& a, const Name& b);
  friend bool operator!=(const Name& a, const Name& b);

  friend std::ostream& operator<<(std::ostream& os, const Name& name);

private:
  uint_type _hi{ 0 };
  uint_type _low{ 0 };
};

}

constexpr quicr::Name operator""_name(const char* x)
{
  return { std::string_view(x) };
}
