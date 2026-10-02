// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/track_name.h"
#include "quicr/utilities/format.h"

#include <chrono>
#include <string>
#include <vector>

namespace quicr::example {

    /**
     * @brief Get UTC timestamp as a string
     *
     * @return string value of UTC time
     */
    static std::string GetTimeStr() noexcept
    {
        const auto now = std::chrono::floor<std::chrono::microseconds>(std::chrono::system_clock::now());
        return quicr::format("{:%F %T}", now);
    }

    static const TrackNamespace MakeTrackNamespace(const std::string& ns)
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

        return split(ns, ",");
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
        return { MakeTrackNamespace(track_namespace), { track_name.begin(), track_name.end() } };
    }
}
