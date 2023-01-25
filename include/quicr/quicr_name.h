#pragma once

#include <cstdint>
#include <vector>

namespace quicr {
class Name
{
public:
  using uint_type = uint64_t;

  Name() = delete;
  Name(uint_type value);
  Name(const std::string& hex_value);
  Name(uint8_t* data, size_t length);
  Name(const uint8_t* data, size_t length);
  Name(const std::vector<uint8_t>& data);
  Name(const Name& other);
  Name(Name&& other);
  ~Name() = default;

  std::vector<uint8_t> data() const;
  size_t size() const;
  std::string to_hex() const;

  Name operator>>(uint16_t value);
  Name operator<<(uint16_t value);
  Name operator+(uint_type value);
  void operator+=(uint_type value);
  Name operator-(uint_type value);
  void operator-=(uint_type value);
  Name operator&(uint_type value);
  void operator&=(uint_type value);
  Name operator|(uint_type value);
  void operator|=(uint_type value);
  Name operator&(const Name& other);
  void operator&=(const Name& other);
  Name operator|(const Name& other);
  void operator|=(const Name& other);

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
