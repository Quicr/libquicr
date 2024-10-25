#pragma once


#include <iostream>
#include <regex>
#include <string>
#include <unordered_map>

#include <quicr/client.h>
#include <quicr/detail/defer.h>

namespace qperf::utils {
using ScenarioVector = std::vector<std::string>;
using ScenarioMap = std::unordered_map<std::string, std::string>;
using ScenarioMapVector = std::vector<ScenarioMap> ;
ScenarioVector string_split(const std::string& in);
ScenarioMap parse_key_pairs(const std::string& input);
ScenarioMapVector parse_scenario_string(std::string& input);

/*

bool populate_scenario_fields(const ScenarioMap scenario_map,
                    quicr::FullTrackName& full_track_name,
                    quicr::TrackMode& track_mode,
                    uint8_t& default_priority,
                    uint32_t& default_ttl,
                    double& transmit_period,
                    uint32_t& group_0_size,
                    uint32_t& group_not_0_size
                    );
*/
inline quicr::FullTrackName MakeFullTrackName(const std::string& track_namespace,
                                                const std::string& track_name,
                                                const std::optional<uint64_t> track_alias = std::nullopt) noexcept
{
    return {
        { track_namespace.begin(), track_namespace.end() },
        { track_name.begin(), track_name.end() },
        track_alias,
    };
}                    
}