#pragma once

#include "quicr_name.h"

namespace quicr {
namespace messages {
struct MessageBuffer;
}

class Namespace
{
public:
  Namespace() = default;
  Namespace(const Name& name, uint16_t sig_bits);
  Namespace(Name&& name, uint16_t sig_bits);
  Namespace(const Namespace& ns);
  Namespace(Namespace&& ns);

  Namespace& operator=(const Namespace& other);
  Namespace& operator=(Namespace&& other);

  bool contains(const Name& name) const;
  bool contains(const Namespace& name_space) const;
  Name name() const { return _mask_name; }
  uint16_t length() const { return _sig_bits; }
  std::string to_hex() const;

  friend bool operator==(const Namespace& a, const Namespace& b);
  friend bool operator!=(const Namespace& a, const Namespace& b);
  friend bool operator>(const Namespace& a, const Namespace& b);
  friend bool operator<(const Namespace& a, const Namespace& b);

  friend std::ostream& operator<<(std::ostream& os, const Namespace& ns);

  friend void operator<<(messages::MessageBuffer& msg, const Namespace& ns);
  friend bool operator>>(messages::MessageBuffer& msg, Namespace& ns);

private:
  Name _mask_name;
  uint16_t _sig_bits;
};
}
