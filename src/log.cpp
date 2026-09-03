#include "quicr/log.h"

#include <memory>
#include <string_view>

namespace quicr {
    bool Logger::ShouldLog(Level level) const noexcept
    {
#ifdef QUICR_ACTIVE_LOG_LEVEL
        return static_cast<int>(level) >= QUICR_ACTIVE_LOG_LEVEL;
#else
        return true;
#endif
    }
}
