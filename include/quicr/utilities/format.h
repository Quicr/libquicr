#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <utility>

#ifdef QUICR_HAS_STD_FORMAT
#include <format>
namespace std_or_fmt = std;
#else
#include <fmt/chrono.h>
namespace std_or_fmt = fmt;
#endif

namespace quicr {

    template<typename... Args>
    std::string format(std_or_fmt::format_string<Args...> msg, Args&&... args)
    {
        return std_or_fmt::format(msg, std::forward<Args>(args)...);
    }

    template<typename... Args>
    std::string vformat(std::string_view msg, Args&&... args)
    {
        const std_or_fmt::string_view format{ msg.data(), msg.size() };
        return std_or_fmt::vformat(format, std_or_fmt::make_format_args(args...));
    }

}
