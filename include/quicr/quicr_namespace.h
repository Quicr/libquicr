#pragma once

#include <quicr/quicr_name.h>

#include <map>
#include <ostream>

namespace quicr {

/**
 * @brief A prefix for a quicr::Name
 */
class Namespace
{
public:
  Namespace() = default;
  constexpr Namespace(const Namespace& ns) = default;
  Namespace(Namespace&& ns) = default;
  Namespace(const Name& name, uint8_t sig_bits);
  Namespace(Name&& name, uint8_t sig_bits);
  Namespace(std::string_view str);

  Namespace& operator=(const Namespace& other) = default;
  Namespace& operator=(Namespace&& other) = default;

  bool contains(const Name& name) const;
  bool contains(const Namespace& prefix) const
  {
    return contains(prefix._name);
  }

  constexpr Name name() const { return _name; }
  constexpr uint8_t length() const { return _sig_bits; }
  std::string to_hex() const;

  friend bool operator==(const Namespace& a, const Namespace& b);
  friend bool operator!=(const Namespace& a, const Namespace& b);

  friend bool operator>(const Namespace& a, const Namespace& b);
  friend bool operator>(const Namespace& a, const Name& b);
  friend bool operator>(const Name& a, const Namespace& b);

  friend bool operator<(const Namespace& a, const Namespace& b);
  friend bool operator<(const Namespace& a, const Name& b);
  friend bool operator<(const Name& a, const Namespace& b);

  friend std::ostream& operator<<(std::ostream& os, const Namespace& ns);

private:
  Name _name;
  uint8_t _sig_bits;
};

struct NamespaceComparator
{
  using is_transparent = std::true_type;

  bool operator()(const Namespace& ns1, const Namespace& ns2) const
  {
    return ns1 < ns2;
  }

  bool operator()(const Namespace& ns, const Name& name) const
  {
    return ns.contains(name);
  }

  bool operator()(const Name& name, const Namespace& ns) const
  {
    return ns.contains(name);
  }
};

template<class T,
         class Allocator = std::allocator<std::pair<const quicr::Namespace, T>>>
using namespace_map =
  std::map<quicr::Namespace, T, NamespaceComparator, Allocator>;
}
