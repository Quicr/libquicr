#pragma once

#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace quicr {
namespace messages {
class MessageBuffer;
}

class Name
{
  using uint_type = uint64_t;

public:
  constexpr Name() = default;
  constexpr Name(const Name& other) = default;
  Name(Name&& other) = default;
  Name(const std::string& hex_value);
  Name(uint8_t* data, size_t length);
  Name(const uint8_t* data, size_t length);
  Name(const std::vector<uint8_t>& data);
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

struct NameException : public std::runtime_error
{
  using std::runtime_error::runtime_error;
};
}
