#include <quicr/quicr_namespace.h>
#include <quicr/message_buffer.h>

#include <sstream>

namespace quicr {

const size_t max_uint_type_bit_size = sizeof(Name::uint_type) * 8 * 2;

Namespace::Namespace(const Name& name, uint16_t sig_bits)
  : _mask_name{ name }
  , _sig_bits{ sig_bits }
{
}

Namespace::Namespace(Name&& name, uint16_t sig_bits)
  : _mask_name{ std::move(name) }
  , _sig_bits{ sig_bits }
{
}
  
Namespace::Namespace(const Namespace& ns)
  : _mask_name{ns._mask_name}, _sig_bits{ns._sig_bits}
{
}
  
Namespace::Namespace(Namespace&& ns)
  : _mask_name{std::move(ns._mask_name)}, _sig_bits{std::move(ns._sig_bits)}
{
}

bool
Namespace::contains(const Name& name) const
{
  if (name.size() != _mask_name.size())
    return false;

  auto insig_bits = max_uint_type_bit_size - _sig_bits;
  return (name >> insig_bits) == (_mask_name >> insig_bits);
}

bool
Namespace::contains(const Namespace& name_space) const
{
  return contains(name_space._mask_name);
}

std::string Namespace::to_hex() const
{
  return _mask_name.to_hex();
}

Namespace&
Namespace::operator=(const Namespace& other)
{
  _mask_name = other._mask_name;
  _sig_bits = other._sig_bits;
  return *this;
}

Namespace&
Namespace::operator=(Namespace&& other)
{
  _mask_name = std::move(other._mask_name);
  _sig_bits = std::move(other._sig_bits);
  return *this;
}

bool
operator==(const Namespace& a, const Namespace& b)
{
  return a._mask_name == b._mask_name && a._sig_bits == b._sig_bits;
}

bool
operator!=(const Namespace& a, const Namespace& b)
{
  return !(a == b);
}

bool
operator>(const Namespace& a, const Namespace& b)
{
  return a._mask_name > b._mask_name;
}

bool
operator<(const Namespace& a, const Namespace& b)
{
  return a._mask_name < b._mask_name;
}

std::ostream&
operator<<(std::ostream& os, const Namespace& ns)
{
  os << ns.to_hex();
  return os;
}

void
operator<<(messages::MessageBuffer& msg, const quicr::Namespace& val)
{
  msg.push_back(val._sig_bits);
  msg << val._mask_name;
}

bool
operator>>(messages::MessageBuffer& msg, quicr::Namespace& val)
{
  msg >> val._mask_name;
  val._sig_bits = msg.back();
  msg.pop_back();

  return true;
}
}