#pragma once

#include <cstdint>
#include <vector>

namespace quicr
{
class Name
{
public:
    Name() = delete;
    Name(uint64_t value);
    Name(const std::string& hex_value);
    Name(uint8_t* data, size_t length);
    Name(const std::vector<uint8_t>& data);
    Name(const Name& other);
    Name(Name&& other);
    ~Name() = default;

    std::vector<uint8_t> data() const;
    size_t size() const;
    std::string to_hex() const;

    Name operator>>(uint16_t value);
    Name operator<<(uint16_t value);
    Name operator+(uint64_t value);
    Name operator-(uint64_t value);
    Name operator&(uint64_t value);
    Name operator|(uint64_t value);
    Name operator&(const Name& other);
    Name operator|(const Name& other);

    Name& operator=(const Name& other);
    Name& operator=(Name&& other);

    friend bool operator<(const Name& a, const Name& b);
    friend bool operator>(const Name& a, const Name& b);
    friend bool operator==(const Name& a, const Name& b);
    friend bool operator!=(const Name& a, const Name& b);

private:
    uint64_t _hi;
    uint64_t _low;
};
}
