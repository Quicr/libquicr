#pragma once

#include "moq/common.h"
#include "span.h"

#include <cstdint>
#include <vector>

namespace moq::detail {
    class MessageFormatter
    {
      public:
        MessageFormatter() = default;
        MessageFormatter(std::size_t initial_size);

        void Push(Byte data);
        void Push(BytesSpan data);

      private:
        Bytes msg_bytes_;
    };
}
