#include <quicr/message_buffer.h>
#include <quicr/quicr_namespace.h>

#include <sstream>

namespace quicr {

constexpr size_t max_uint_type_bit_size = sizeof(Name::uint_type) * 8 * 2;

Namespace::Namespace(const Name& name, uint8_t sig_bits)
  : _mask_name{ (name >> (max_uint_type_bit_size - sig_bits))
                << (max_uint_type_bit_size - sig_bits) }
  , _sig_bits{ sig_bits }
{
}

Namespace::Namespace(Name&& name, uint8_t sig_bits)
  : _mask_name{ std::move(name >> (max_uint_type_bit_size - sig_bits))
                << (max_uint_type_bit_size - sig_bits) }
  , _sig_bits{ sig_bits }
{
}

Namespace::Namespace(const Namespace& ns)
  : _mask_name{ ns._mask_name }
  , _sig_bits{ ns._sig_bits }
{
}

Namespace::Namespace(Namespace&& ns)
  : _mask_name{ std::move(ns._mask_name) }
  , _sig_bits{ std::move(ns._sig_bits) }
{
}

bool
Namespace::contains(const Name& name) const
{
  const uint8_t insig_bits = max_uint_type_bit_size - _sig_bits;
  return (name >> insig_bits) == (_mask_name >> insig_bits);
}

bool
Namespace::contains(const Namespace& name_space) const
{
  return contains(name_space._mask_name);
}

std::string
Namespace::to_hex() const
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
}