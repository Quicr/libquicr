#pragma once

#include "ctrl_message_types.h"
#include "uintvar.h"
#include "utilities.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace quicr::messages {
    /**
     * @brief Generic message buffer formatting class.
     *
     * @details Generic message type that formats a message into a byte buffer in a declarative manner. The intent is
     *          to call it via the Builder Pattern methods in a way that lays out the format of message obviously.
     */
    class Message
    {
      public:
        Message(std::shared_ptr<std::vector<std::uint8_t>> buffer = std::make_shared<std::vector<std::uint8_t>>())
          : _bytes(std::move(buffer))
        {
            if (_bytes == nullptr) {
                throw std::invalid_argument("messages::Message buffer cannot be nullptr");
            }
        }

        /**
         * @brief Checks if the message was built with the payload length written near the front.
         * @returns True if the message has space reserved for the payload size near the front, false otherwise.
         */
        constexpr bool HasPrependedLength() const noexcept { return _length_bytes_offset.has_value(); }

        /**
         * @brief Prepends a message type at the front of the byte buffer.
         *
         * @tparam T The enumeration/integral type of the message type.
         *
         * @param type The type of message to be written at the front of the buffer.
         * @returns A reference to the message currently being built.
         *
         * @note When building a message, this should be the first method called.
         */
        template<typename T>
            requires std::is_enum_v<T> || std::is_integral_v<T>
        inline Message& PrependType(T type)
        {
            UintVar type_bytes(static_cast<std::uint64_t>(type));
            _bytes->insert(_bytes->begin(), type_bytes.begin(), type_bytes.end());
            return *this;
        }

        /**
         * @brief Inserts a std::uint16_t near the front of the byte buffer for the payload length.
         * @returns A reference to the message currently being built.
         */
        inline Message& ReserveLength()
        {
            if (_length_bytes_offset.has_value()) {
                return *this;
            }

            if (_bytes->empty()) {
                _length_bytes_offset = 0;
            } else {
                _length_bytes_offset = UintVar::Size(_bytes->front());
            }

            _bytes->insert(std::next(_bytes->begin(), _length_bytes_offset.value()), { 0, 0 });

            return *this;
        }

        inline Message& Append(std::uint8_t byte) noexcept
        {
            _bytes->push_back(byte);
            return *this;
        }

        inline Message& Append(std::span<const std::uint8_t> bytes) noexcept
        {
            _bytes->insert(_bytes->end(), bytes.begin(), bytes.end());
            return *this;
        }

        template<typename T>
            requires requires(std::vector<std::uint8_t>& buffer, const T& value) { buffer << value; }
        inline Message& Append(const T& value) noexcept(noexcept(*_bytes << value))
        {
            *_bytes << value;
            return *this;
        }

        inline const std::shared_ptr<std::vector<uint8_t>>& ToBytes() const noexcept
        {
            if (_length_bytes_offset.has_value()) {
                const std::uint16_t payload_size = SwapBytes(
                  static_cast<std::uint16_t>(_bytes->size() - _length_bytes_offset.value() - sizeof(std::uint16_t)));
                std::memcpy(_bytes->data() + _length_bytes_offset.value(), &payload_size, sizeof(std::uint16_t));
            }

            return _bytes;
        }

        inline std::span<const std::uint8_t> ToByteSpan() const noexcept { return *ToBytes(); }

        void Clear() noexcept
        {
            _bytes->clear();
            _length_bytes_offset = std::nullopt;
        }

        template<typename Field>
        [[nodiscard]] static inline Field ParseField(std::span<const std::uint8_t>& buffer)
        {
            Field field{};
            buffer = buffer >> field;
            return field;
        }

      private:
        std::optional<std::size_t> _length_bytes_offset;
        mutable std::shared_ptr<std::vector<std::uint8_t>> _bytes;
    };
}
