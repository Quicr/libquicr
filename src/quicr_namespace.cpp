#include <quicr/quicr_namespace.h>

namespace quicr {

Namespace::Namespace(const Name& name, uint8_t sig_bits)
  : _name{ name & ~(~0x0_name >> sig_bits) }
  , _sig_bits{ sig_bits }
{
}

Namespace::Namespace(Name&& name, uint8_t sig_bits)
  : _name{ name & ~(~0x0_name >> sig_bits) }
  , _sig_bits{ sig_bits }
{
}

Namespace::Namespace(std::string_view str)
{
  auto delim_pos = str.find_first_of('/');
  _name = str.substr(0, delim_pos);
  str.remove_prefix(delim_pos+1);
  _sig_bits = std::atoi(str.data());
}

bool
Namespace::contains(const Name& name) const
{
  return (name & ~(~0x0_name >> _sig_bits)) == _name;
}

std::string
Namespace::to_hex() const
{
  return _name.to_hex() + "/" + std::to_string(_sig_bits);
}

bool
operator==(const Namespace& a, const Namespace& b)
{
  return a._name == b._name && a._sig_bits == b._sig_bits;
}

bool
operator!=(const Namespace& a, const Namespace& b)
{
  return !(a == b);
}

bool
operator>(const Namespace& a, const Namespace& b)
{
  return a._name > b._name;
}

bool
operator>(const Namespace& a, const Name& b)
{
  return a._name > b;
}

bool
operator>(const Name& a, const Namespace& b)
{
  return a > b._name;
}

bool
operator<(const Namespace& a, const Namespace& b)
{
  return a._name < b._name;
}

bool
operator<(const Namespace& a, const Name& b)
{
  return a._name < b;
}

bool
operator<(const Name& a, const Namespace& b)
{
  return a < b._name;
}

std::ostream&
operator<<(std::ostream& os, const Namespace& ns)
{
  os << ns.to_hex();
  return os;
}
}
