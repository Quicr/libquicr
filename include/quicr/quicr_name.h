#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <cstdlib>
#include <stdexcept>

namespace quicr {
namespace messages {
class MessageBuffer;
}

class Name
{
public:
  using uint_type = uint64_t;

  Name();
  Name(const Name& other);
  Name(Name&& other);
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

  Name& operator=(const Name& other);
  Name& operator=(Name&& other);

  friend bool operator<(const Name& a, const Name& b);
  friend bool operator>(const Name& a, const Name& b);
  friend bool operator==(const Name& a, const Name& b);
  friend bool operator!=(const Name& a, const Name& b);

  friend std::ostream& operator<<(std::ostream& os, const Name& name);

private:
  uint_type _hi;
  uint_type _low;
};

struct NameException : public std::runtime_error
{
  using std::runtime_error::runtime_error;
};
}
