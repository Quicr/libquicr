#pragma once

#include <exception>
#include <istream>
#include <ostream>
#include <stdint.h>

namespace quicr {
/**
 * @brief Variable length integer
 */
class uintVar_t
{
  static constexpr uint64_t _max_value = 0x1ull << 61;

public:
  uintVar_t() = default;
  constexpr uintVar_t(const uintVar_t&) = default;
  constexpr uintVar_t(uintVar_t&&) = default;
  constexpr uintVar_t(uint64_t value)
    : _value{ value }
  {
    if (value >= _max_value)
      throw std::domain_error("Max value cannot be exceeded: " +
                              std::to_string(_max_value));
  }

  constexpr operator uint64_t() const { return _value; }
  constexpr uintVar_t& operator=(const uintVar_t&) = default;
  constexpr uintVar_t& operator=(uintVar_t&&) = default;
  constexpr uintVar_t& operator=(uint64_t value)
  {
    if (value >= _max_value)
      throw std::domain_error("Max value cannot be exceeded: " +
                              std::to_string(_max_value));
    _value = value;
    return *this;
  }

  constexpr bool operator==(uintVar_t other) { return _value == other._value; }
  constexpr bool operator!=(uintVar_t other) { return !(*this == other); }
  constexpr bool operator>(uintVar_t other) { return _value > other._value; }
  constexpr bool operator>=(uintVar_t other) { return _value >= other._value; }
  constexpr bool operator<(uintVar_t other) { return _value < other._value; }
  constexpr bool operator<=(uintVar_t other) { return _value <= other._value; }

  friend std::ostream& operator<<(std::ostream& os, uintVar_t v)
  {
    return os << v._value;
  }

  friend std::istream& operator>>(std::istream& is, uintVar_t v)
  {
    return is >> v._value;
  }

private:
  uint64_t _value;
};

constexpr uintVar_t
operator""_uV(uint64_t value)
{
  return { value };
}
}
