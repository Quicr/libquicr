#include <quicr/quicr_namespace.h>


namespace quicr
{
Namespace::Namespace(const Name& name, uint16_t sig_bits)
    : _name{name}, _sig_bits{sig_bits}
{
    
}

Namespace::Namespace(Name&& name, uint16_t sig_bits)
    : _name{std::move(name)}, _sig_bits{sig_bits}
{
    
}

bool Namespace::contains(const Name& name) const
{
    
}

bool Namespace::contains(const Namespace& name_space) const
{
    
}

bool operator==(const Namespace& a, const Namespace& b)
{
    return a._name == b._name && a._sig_bits == b._sig_bits;
}

bool operator!=(const Namespace& a, const Namespace& b)
{
    return !(a == b);
}

bool operator>(const Namespace& a, const Namespace& b)
{
    return a._name > b._name;
}

bool operator<(const Namespace& a, const Namespace& b)
{
    return a._name < b._name;
}
}