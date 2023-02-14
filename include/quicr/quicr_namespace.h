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
  Namespace(const Name& name, uint8_t sig_bits);
  Namespace(Name&& name, uint8_t sig_bits);
  Namespace(const Namespace& ns);
  Namespace(Namespace&& ns);

  Namespace& operator=(const Namespace& other);
  Namespace& operator=(Namespace&& other);

  bool contains(const Name& name) const;
  bool contains(const Namespace& name_space) const;
  Name name() const { return _mask_name; }
  uint8_t length() const { return _sig_bits; }
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
