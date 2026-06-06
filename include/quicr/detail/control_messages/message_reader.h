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
