#pragma once

#include <quicr/quicr_name.h>

namespace quicr {
namespace messages {
class MessageBuffer;
}

class Namespace
{
public:
  Namespace() = default;
  constexpr Namespace(const Namespace& ns) = default;
  Namespace(Namespace&& ns) = default;
  Namespace(const Name& name, uint8_t sig_bits);
  Namespace(Name&& name, uint8_t sig_bits);

  Namespace& operator=(const Namespace& other) = default;
  Namespace& operator=(Namespace&& other) = default;

  bool contains(const Name& name) const;
  bool contains(const Namespace& name_space) const;
  constexpr Name name() const { return _mask_name; }
  constexpr uint8_t length() const { return _sig_bits; }
  std::string to_hex() const;

  friend bool operator==(const Namespace& a, const Namespace& b);
  friend bool operator!=(const Namespace& a, const Namespace& b);
  friend bool operator>(const Namespace& a, const Namespace& b);
  friend bool operator<(const Namespace& a, const Namespace& b);

  friend std::ostream& operator<<(std::ostream& os, const Namespace& ns);

private:
  Name _mask_name;
  uint8_t _sig_bits;
};
}
