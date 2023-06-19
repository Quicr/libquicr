#pragma once

#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace quicr {

constexpr uint64_t
hexchar_to_uint(char x)
{
  uint64_t y = 0;
  if ('0' <= x && x <= '9')
    y += x - '0';
  else if ('A' <= x && x <= 'F')
    y += x - 'A' + 10;
  else if ('a' <= x && x <= 'f')
    y += x - 'a' + 10;

  return y;
}

template<typename T,
         typename = typename std::enable_if<std::is_unsigned_v<T>, T>::type>
constexpr char
uint_to_hexchar(T b)
{
  char x = ' ';
  if (b > 9)
    x = b + 'A' - 10;
  else
    x = b + '0';

  return x;
}

template<typename T,
         typename = typename std::enable_if<std::is_unsigned_v<T>, T>::type>
constexpr T
hex_to_uint(std::string_view x)
{
  if (x.starts_with("0x"))
    x.remove_prefix(2);

  T y = 0;
  for (size_t i = 0; i < x.length(); ++i) {
    y *= 16ull;
    y += hexchar_to_uint(x[i]);
  }

  return y;
}

template<typename T,
         typename = typename std::enable_if<std::is_unsigned_v<T>, T>::type>
std::string
uint_to_hex(T y)
{
  char x[sizeof(T) * 2 + 1] = "";
  for (int i = sizeof(T) * 2 - 1; i >= 0; --i) {
    T b = y & 0x0F;
    x[i] = uint_to_hexchar(b);
    y -= b;
    y /= 16;
  }
  x[sizeof(T) * 2] = '\0';

  return x;
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
public:
  Name() = default;
  constexpr Name(const Name& other) = default;
  Name(Name&& other) = default;
  Name(uint8_t* data, size_t length);
  Name(const uint8_t* data, size_t length);
  Name(const std::vector<uint8_t>& data);

  constexpr Name(std::string_view hex_value)
  {
    if (hex_value.starts_with("0x"))
      hex_value.remove_prefix(2);

    if (hex_value.length() > sizeof(Name) * 2)
      throw NameException("Hex string cannot be longer than " +
                          std::to_string(sizeof(Name) * 2) + " bytes");

    if (hex_value.length() > sizeof(Name)) {
      _hi = hex_to_uint<uint64_t>(
        hex_value.substr(0, hex_value.length() - sizeof(Name)));
      _low = hex_to_uint<uint64_t>(
        hex_value.substr(hex_value.length() - sizeof(Name), sizeof(Name)));
    } else {
      _hi = 0;
      _low = hex_to_uint<uint64_t>(hex_value.substr(0, hex_value.length()));
    }
  }

  ~Name() = default;

  std::string to_hex() const;
  std::uint8_t operator[](std::size_t offset) const;

  Name operator>>(uint16_t value) const;
  Name operator>>=(uint16_t value);
  Name operator<<(uint16_t value) const;
  Name operator<<=(uint16_t value);
  Name operator+(uint64_t value) const;
  void operator+=(uint64_t value);
  Name operator+(Name value) const;
  void operator+=(Name value);
  Name operator++();
  Name operator++(int);
  Name operator--();
  Name operator--(int);
  Name operator-(uint64_t value) const;
  void operator-=(uint64_t value);
  Name operator-(Name value) const;
  void operator-=(Name value);
  Name operator&(uint64_t value) const;
  void operator&=(uint64_t value);
  Name operator|(uint64_t value) const;
  void operator|=(uint64_t value);
  Name operator&(const Name& other) const;
  void operator&=(const Name& other);
  Name operator|(const Name& other) const;
  void operator|=(const Name& other);
  Name operator^(const Name& other) const;
  void operator^=(const Name& other);

  constexpr Name operator~() const
  {
    Name name(*this);
    name._hi = ~_hi;
    name._low = ~_low;
    return name;
  }

  constexpr Name& operator=(const Name& other) = default;
  constexpr Name& operator=(Name&& other) = default;

  friend bool operator<(const Name& a, const Name& b);
  friend bool operator>(const Name& a, const Name& b);
  friend bool operator==(const Name& a, const Name& b);
  friend bool operator!=(const Name& a, const Name& b);

  friend std::ostream& operator<<(std::ostream& os, const Name& name);

private:
  uint64_t _hi;
  uint64_t _low;
};
}

constexpr quicr::Name
operator""_name(const char* x)
{
  return { std::string_view(x) };
}
