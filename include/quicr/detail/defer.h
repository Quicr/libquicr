#pragma once

#include <functional>
#include <iostream>

#define DEFER_CONCAT(a, b) DEFER_CONCAT_INNER(a, b)
#define DEFER_CONCAT_INNER(a, b) a##b
#define defer(n)                                                                                                       \
    quicr::DeferType DEFER_CONCAT(defer_, __COUNTER__)                                                                 \
    {                                                                                                                  \
        [&] { n; }                                                                                                     \
    }

namespace quicr {

    class DeferType
    {
      public:
        explicit DeferType(std::function<void()> f)
          : func{ f }
        {
        }
        ~DeferType() { func(); }

      private:
        std::function<void()> func;
    };
}