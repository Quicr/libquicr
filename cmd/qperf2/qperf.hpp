#pragma once

#include <cstdint>
#include <quicr/client.h>

#include "inicpp.h"

namespace qperf {
    struct PerfConfig
    {
        std::string test_name;
        quicr::FullTrackName full_track_name;
        quicr::TrackMode track_mode;
        uint8_t priority;
        uint32_t ttl;
        double transmit_interval;
        uint32_t objects_per_group;
        uint32_t bytes_per_group_start;
        uint32_t bytes_per_group;
        uint64_t start_delay;
        uint64_t total_test_time;
        uint64_t total_transmit_time;
    };

    enum class TestMode : uint8_t
    {
        kNone,
        kWaitPreTest,
        kRunning,
        kComplete,
        kwaitPostTest,
        kError
    };

    struct TestMetrics
    {
        std::uint64_t start_transmit_time;
        std::uint64_t end_transmit_time;
        std::uint64_t total_published_objects;
        std::uint64_t total_objects_dropped_not_ok;
        std::uint64_t total_published_bytes;
        std::uint64_t max_publish_bitrate;
        std::uint64_t min_publish_bitrate;
        std::uint64_t avg_publish_bitrate;
        std::uint32_t metric_samples;
        std::uint64_t bitrate_total;
    };

    struct ObjectTestHeader
    {
        TestMode test_mode;
        std::uint64_t time;
    };

    struct ObjectTestComplete
    {
        TestMode test_mode;
        std::uint64_t time;
        TestMetrics test_metrics;
    };

    inline quicr::FullTrackName MakeFullTrackName(const std::string& track_namespace,
                                                  const std::string& track_name,
                                                  const std::optional<uint64_t> track_alias = std::nullopt) noexcept
    {
        return {
            quicr::TrackNamespace{ track_namespace },
            { track_name.begin(), track_name.end() },
            track_alias,
        };
    }

    static bool PopulateScenarioFields(const std::string section_name, ini::IniFile& inif, PerfConfig& perf_config)
    {
        bool parsed = false;
        std::string scenario_namespace = "";
        std::string scenario_name = "";

        perf_config.test_name = section_name;

        scenario_namespace = inif[section_name]["namespace"].as<std::string>();
        scenario_name = inif[section_name]["name"].as<std::string>();
        perf_config.full_track_name = MakeFullTrackName(scenario_namespace, scenario_name);

        std::string track_mode_ini_str = inif[section_name]["track_mode"].as<std::string>();
        if (track_mode_ini_str == "datagram") {
            perf_config.track_mode = quicr::TrackMode::kDatagram;
        } else if (track_mode_ini_str == "stream") {
            perf_config.track_mode = quicr::TrackMode::kStream;
        } else {
            perf_config.track_mode = quicr::TrackMode::kStream;
            SPDLOG_WARN("Invalid or missing track mode in scenario. Using default `stream`");
        }

        perf_config.priority = inif[section_name]["priority"].as<std::uint32_t>();
        perf_config.ttl = inif[section_name]["ttl"].as<std::uint32_t>();
        perf_config.transmit_interval = inif[section_name]["time_interval"].as<double>();
        perf_config.objects_per_group = inif[section_name]["objs_per_group"].as<std::uint32_t>();
        perf_config.bytes_per_group_start = inif[section_name]["bytes_per_group_start"].as<std::uint32_t>();
        perf_config.bytes_per_group = inif[section_name]["bytes_per_group"].as<std::uint32_t>();
        perf_config.start_delay = inif[section_name]["start_delay"].as<std::uint64_t>();
        perf_config.total_test_time = inif[section_name]["total_test_time"].as<std::uint64_t>();
        perf_config.total_transmit_time = perf_config.total_test_time - perf_config.start_delay;

        SPDLOG_INFO("--------------------------------------------");
        SPDLOG_INFO("Test config:");
        SPDLOG_INFO("                    ns  \"{}\"", scenario_namespace);
        SPDLOG_INFO("                     n  \"{}\"", scenario_name);
        SPDLOG_INFO("              track mode {} ({})", (int)perf_config.track_mode, track_mode_ini_str);
        SPDLOG_INFO("                     pri {}", perf_config.priority);
        SPDLOG_INFO("                     ttl {}", perf_config.ttl);
        SPDLOG_INFO("            objspergroup {}", perf_config.objects_per_group);
        SPDLOG_INFO("   bytes per group start {}", perf_config.bytes_per_group_start);
        SPDLOG_INFO("         bytes per group {}", perf_config.bytes_per_group);
        SPDLOG_INFO("       transmit interval {}", perf_config.transmit_interval);
        SPDLOG_INFO("             start_delay {}", perf_config.start_delay);
        SPDLOG_INFO("         total test time {}", perf_config.total_test_time);
        SPDLOG_INFO("           transmit time {}", perf_config.total_transmit_time);
        SPDLOG_INFO("--------------------------------------------");

        parsed = true;
        return parsed;
    }

    inline std::string FormatBitrate(const std::uint32_t& bitrate)
    {
        if (bitrate > 1e9) {
            return std::to_string(double(bitrate) / 1e9) + " Gbps";
        }
        if (bitrate > 1e6) {
            return std::to_string(double(bitrate) / 1e6) + " Mbps";
        }
        if (bitrate > 1e3) {
            return std::to_string(double(bitrate) / 1e3) + " Kbps";
        }

        return std::to_string(bitrate) + " bps";
    }

    /**
     * @brief Publish track handler
     * @details Publish track handler used for the publish command line option
     */

} // namespace qperf
