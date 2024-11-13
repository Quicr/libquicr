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

    struct TestMetrics {
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

    struct ObjectTestHeader {
        TestMode test_mode;
        std::uint64_t time;
    };

    struct ObjectTestComplete {
        TestMode test_mode;
        std::uint64_t time;        
        TestMetrics test_metrics;
    //    quicr::ConnectionMetrics quicr_metrics;
    //    quicr::PublishTrackMetrics  quicr_publish_track_metrics;
        /*
        std::uint64_t start_transmit_time;
        std::uint64_t end_transmit_time;         
        std::uint64_t total_published_objects;
        std::uint64_t total_published_bytes;
        std::uint64_t max_bitrate;
        std::uint64_t min_bitrate;
        std::uint64_t avg_bitrate;   
        */
        
    };    


    inline quicr::FullTrackName MakeFullTrackName(const std::string& track_namespace,
                                                  const std::string& track_name,
                                                  const std::optional<uint64_t> track_alias = std::nullopt) noexcept
    {
        return {
            quicr::TrackNamespace{ quicr::Bytes{ track_namespace.begin(), track_namespace.end() } },
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
        SPDLOG_INFO("\tns = \"{}\", n=\"{}\"", scenario_namespace, scenario_name);
        SPDLOG_INFO("\ttrack mode = {}", (int)perf_config.track_mode);
        SPDLOG_INFO("\tpri = {}, ttl = {}", perf_config.priority, perf_config.ttl);
        SPDLOG_INFO("\tobjspergroup = {}",perf_config.objects_per_group);
        SPDLOG_INFO("\tbytes per group start = {}, bytes per group = {}",perf_config.bytes_per_group_start, perf_config.bytes_per_group);
        SPDLOG_INFO("\ttransmit interval = {}",perf_config.transmit_interval);
        SPDLOG_INFO("\tstart_delay = {}",perf_config.start_delay);
        SPDLOG_INFO("\ttotal test time = {}",perf_config.total_test_time);
        SPDLOG_INFO("\ttransmit time = {}",perf_config.total_transmit_time);
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