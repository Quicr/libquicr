// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

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

    /**
     * @brief Formats arguments using the configured `std::format` or {fmt} backend.
     * @tparam Args Argument types checked against the format string.
     * @param msg Compile-time-checked format string.
     * @param args Values to format.
     * @return The formatted string.
     * @throws std::format_error or the backend's equivalent when formatting fails; may also throw allocation errors.
     */
    template<typename... Args>
    std::string format(std_or_fmt::format_string<Args...> msg, Args&&... args)
    {
        return std_or_fmt::format(msg, std::forward<Args>(args)...);
    }

    /**
     * @brief Formats arguments using a runtime format string.
     * @tparam Args Argument types supplied to the backend.
     * @param msg Runtime format string; its view need only remain valid for this call.
     * @param args Values to format.
     * @return The formatted string.
     * @throws std::format_error or the backend's equivalent for an invalid format string or formatting failure; may
     * also throw allocation errors.
     */
    template<typename... Args>
    std::string vformat(std::string_view msg, Args&&... args)
    {
        const std_or_fmt::string_view format{ msg.data(), msg.size() };
        return std_or_fmt::vformat(format, std_or_fmt::make_format_args(args...));
    }

}
