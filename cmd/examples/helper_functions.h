// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <quicr/track_name.h>

#include <chrono>
#include <iomanip>
#include <ostream>
#include <string>

namespace quicr::example {

    /**
     * @brief Get UTC timestamp as a string
     *
     * @return string value of UTC time
     */
    static std::string GetTimeStr() noexcept
    {
        std::ostringstream oss;

        auto now = std::chrono::system_clock::now();
        auto now_us = std::chrono::time_point_cast<std::chrono::microseconds>(now);
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        struct tm tm_result;
        localtime_r(&t, &tm_result);
        oss << std::put_time(&tm_result, "%F %T") << "." << std::setfill('0') << std::setw(6)
            << (now_us.time_since_epoch().count()) % 1'000'000;

        return oss.str();
    }

    /**
     * @brief Create a full track name using strings for namespace and name
     *
     * @param track_namespace           track namespace as a string
     * @param track_name                track name as a string
     * @param track_alias               track alias as optional
     * @return quicr::FullTrackName of the params
     */
    static FullTrackName const MakeFullTrackName(const std::string& track_namespace,
                                                 const std::string& track_name) noexcept
    {
        const auto split = [](std::string str, const std::string& delimiter) {
            std::vector<std::string> tokens;

            std::size_t pos = 0;
            while ((pos = str.find(delimiter)) != std::string::npos) {
                tokens.emplace_back(str.substr(0, pos));
                str.erase(0, pos + delimiter.length());
            }
            tokens.emplace_back(std::move(str));

            return tokens;
        };

        FullTrackName full_track_name{ TrackNamespace{ split(track_namespace, ",") },
                                       { track_name.begin(), track_name.end() } };
        return full_track_name;
    }
}
