#pragma once

#include "quicr_name.h"

namespace quicr
{
class Namespace
{
public:
    Namespace() = delete;
    Namespace(const Name& name, uint16_t sig_bits);
    Namespace(Name&& name, uint16_t sig_bits);

    bool contains(const Name& name) const;
    bool contains(const Namespace& name_space) const;

    friend bool operator==(const Namespace& a, const Namespace& b);
    friend bool operator!=(const Namespace& a, const Namespace& b);
    friend bool operator>(const Namespace& a, const Namespace& b);
    friend bool operator<(const Namespace& a, const Namespace& b);

private:
    Name _name;
    uint16_t _sig_bits;
};
}
