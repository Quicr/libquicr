// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/message.h"

#include <utility>
#include <vector>

namespace quicr::messages::control {

    class MessageReader
    {
      public:
        explicit MessageReader(BytesSpan payload) noexcept
          : payload_(payload)
        {
        }

        template<typename Field>
        [[nodiscard]] Field Read()
        {
            return Message::ParseField<Field>(payload_);
        }

        [[nodiscard]] std::vector<KeyValuePair<std::uint64_t>> ReadKeyValuePairs()
        {
            std::vector<KeyValuePair<std::uint64_t>> values;
            std::uint64_t previous_type = 0;

            while (!payload_.empty()) {
                KeyValuePair<std::uint64_t> value;
                ParseKvp(payload_, value, previous_type);
                previous_type = value.type;
                values.push_back(std::move(value));
            }

            return values;
        }

        [[nodiscard]] bool Done() const noexcept { return payload_.empty(); }

        void ExpectDone() const
        {
            if (!Done()) {
                throw ProtocolViolationException("Control message payload has trailing bytes");
            }
        }

      private:
        BytesSpan payload_;
    };

} // namespace quicr::messages::control
