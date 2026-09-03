#pragma once

#ifdef QUICR_HAVE_STD_FORMAT
#include <format>
namespace std_or_fmt = std;
#else
#include <fmt/format.h>
namespace std_or_fmt = fmt;
#endif

namespace quicr {

    template<typename... Args>
    std::string format(std_or_fmt::format_string<Args...> msg, Args&&... args)
    {
        return std_or_fmt::format(msg, std::forward<Args>(args)...);
    }

    template<typename... Args>
    std::string vformat(
      std::conditional_t<sizeof...(Args) == 0, std::string_view, std_or_fmt::format_string<Args...>> msg,
      Args&&... args)
    {
        return std_or_fmt::vformat(msg, std_or_fmt::make_format_args(args...));
    }

}
