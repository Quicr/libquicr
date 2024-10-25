#include "utils.hpp"

#include <spdlog/spdlog.h>

namespace qperf::utils {

ScenarioVector string_split(const std::string& in) {
    const std::regex re("\\|");
    return {
               std::sregex_token_iterator( in.begin(), in.end(), re, -1 ),
               std::sregex_token_iterator()
           };
}

ScenarioMap parse_key_pairs(std::string& input) {
    std::regex pattern(R"((\w+)=([^;]+)[;])");
    std::smatch match;
    ScenarioMap kvps;
    
    while(std::regex_search(input, match, pattern)) {
        kvps[match[1]] = match[2];
        input = match.suffix();
    }
    return kvps;
}

ScenarioMapVector parse_scenario_string(std::string& input) {
    ScenarioMapVector scenario_vector;
    for(auto& s : string_split(input))
    {
        scenario_vector.push_back(parse_key_pairs(s));
    }
    return scenario_vector;
}

}