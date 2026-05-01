#pragma once

#include "uintvar.h"
#include "utilities.h"

#include <cstdint>
#include <span>
#include <vector>

namespace quicr::messages {
    class Message
    {
      public:
        template<typename T>
            requires std::is_enum_v<T>
        Message(T type)
        {
            auto type_bytes = UintVar(static_cast<std::uint64_t>(type));
            _bytes.insert(_bytes.end(), type_bytes.begin(), type_bytes.end());
        }

        Message& ReserveLength()
        {
            if (_length_reserved) {
                return *this;
            }

            const std::size_t type_size = UintVar(_bytes.front()).size();
            _bytes.insert(std::next(_bytes.begin(), type_size), { 0, 0 });

            _length_reserved = true;
            return *this;
        }

        Message& Add(std::uint8_t byte) noexcept
        {
            _bytes.push_back(byte);
            return *this;
        }

        Message& Add(std::span<const std::uint8_t> bytes) noexcept
        {
            _bytes.insert(_bytes.end(), bytes.begin(), bytes.end());
            return *this;
        }

        template<typename T>
            requires requires(std::vector<std::uint8_t>& buffer, const T& value) { buffer << value; }
        Message& Add(const T& value) noexcept(noexcept(_bytes << value))
        {
            _bytes << value;
            return *this;
        }

        const std::vector<std::uint8_t>& ToBytes() const noexcept
        {
            if (_length_reserved) {
                const std::size_t type_size = UintVar(_bytes.front()).size();
                auto* size_ptr = reinterpret_cast<std::uint16_t*>(_bytes.data() + type_size);
                *size_ptr = SwapBytes(static_cast<std::uint16_t>(_bytes.size() - type_size - sizeof(std::uint16_t)));
            }

            return _bytes;
        }

      private:
        bool _length_reserved = false;
        mutable std::vector<std::uint8_t> _bytes;
    };
}
