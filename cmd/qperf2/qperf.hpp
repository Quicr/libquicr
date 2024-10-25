#pragma once

#include <cstdint>
#include <quicr/client.h>

#include "inicpp.h"

namespace qperf {
    struct PerfConfig
    {
        quicr::FullTrackName full_track_name;
        quicr::TrackMode track_mode;
        uint8_t priority;
        uint32_t ttl;
        double transmit_interval;
        uint32_t objects_per_group;
        uint32_t bytes_per_object_0;
        uint32_t bytes_per_object_not_0;
        uint64_t start_delay;
        uint64_t transmit_time;
    };

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

    static bool populate_scenario_fields(const std::string& section_name, ini::IniFile& inif, PerfConfig& perf_config)
    {
        bool parsed = false;

        std::string scenario_namespace = "";
        std::string scenario_name = "";

        scenario_namespace = inif[section_name]["namespace"].as<std::string>();
        scenario_name = inif[section_name]["name"].as<std::string>();
        perf_config.full_track_name = MakeFullTrackName(scenario_namespace, scenario_name);

        std::string track_mode_ini_str = inif[section_name]["trackmode"].as<std::string>();
        if (track_mode_ini_str == "datagram") {
            perf_config.track_mode = quicr::TrackMode::kDatagram;
        } else if (track_mode_ini_str == "perobject") {
            perf_config.track_mode = quicr::TrackMode::kStreamPerObject;
        } else if (track_mode_ini_str == "pergroup") {
            perf_config.track_mode = quicr::TrackMode::kStreamPerGroup;
        } else if (track_mode_ini_str == "pertrack") {
            perf_config.track_mode = quicr::TrackMode::kStreamPerTrack;
        } else {
            perf_config.track_mode = quicr::TrackMode::kStreamPerGroup;
            SPDLOG_WARN("Invalid or missing track mode in scenario. Using default `pergroup`");
        }

        perf_config.priority = inif[section_name]["priority"].as<std::uint32_t>();
        perf_config.ttl = inif[section_name]["ttl"].as<std::uint32_t>();
        perf_config.transmit_interval = inif[section_name]["timeinterval"].as<double>();
        perf_config.objects_per_group = inif[section_name]["objspergroup"].as<std::uint32_t>();
        perf_config.bytes_per_object_0 = inif[section_name]["bytesobject0"].as<std::uint32_t>();
        perf_config.bytes_per_object_not_0 = inif[section_name]["bytesnotobject0"].as<std::uint32_t>();
        perf_config.start_delay = inif[section_name]["startdelay"].as<std::uint64_t>();
        perf_config.transmit_time = inif[section_name]["transmittime"].as<std::uint64_t>();

        SPDLOG_INFO("Test config:");
        SPDLOG_INFO("\tns = \"{}\", n=\"{}\"", scenario_namespace, scenario_name);
        SPDLOG_INFO("\ttrack mode = {}", (int)perf_config.track_mode);
        SPDLOG_INFO("\tpri = {}, ttl = {}", perf_config.priority, perf_config.ttl);
        SPDLOG_INFO("\tobjspergroup = {}",perf_config.objects_per_group);
        SPDLOG_INFO("\tbytesobject0 = {}, bytes_per_object_not_0 = {}",perf_config.bytes_per_object_0, perf_config.bytes_per_object_not_0);
        SPDLOG_INFO("\ttransmit interval = {}",perf_config.transmit_interval);
        SPDLOG_INFO("\tstart_delay = {}",perf_config.start_delay);
        SPDLOG_INFO("\ttransmit time = {}",perf_config.transmit_time);

        parsed = true;
        return parsed;
    }

    /*
        template<typename D, typename I, typename F, typename... Args>
        inline void LoopFor(const D& duration, const I& interval, const F& func, Args&&... args)
        {
            auto run_time = I::zero();
            while (!terminate && run_time < duration) {
                const auto start = std::chrono::high_resolution_clock::now();
                func(std::forward<Args>(args)...);
                const auto end = std::chrono::high_resolution_clock::now();

                const auto execution_time = std::chrono::duration_cast<I>(end - start);
                if (interval != I::zero()) {
                    std::this_thread::sleep_for(interval - execution_time);
                    run_time += interval;
                } else {
                    run_time += execution_time;
                }
            }
        }
    */
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
    class PerfPublishTrackHandler : public quicr::PublishTrackHandler
    {
      private:
        PerfPublishTrackHandler(const PerfConfig&);

      public:
        static auto Create(const std::string& section_name, ini::IniFile& inif);
        void StatusChanged(Status status) override;
        void MetricsSampled(const quicr::PublishTrackMetrics& metrics) override;

        const quicr::PublishTrackMetrics& GetMetrics() const noexcept { return metrics_; }

        std::thread SpawnWriter();
        void WriteThread();
        void StopWriter();

      private:
        quicr::PublishTrackMetrics metrics_;
        std::thread write_thread_;
        std::atomic_bool terminate_;

        uint64_t group_id_;
        uint64_t object_id_;
        PerfConfig perf_config_;
    };

    class PerfPubClient : public quicr::Client
    {
      public:
        PerfPubClient(const quicr::ClientConfig& cfg, const std::string& configfile);
        void StatusChanged(Status status) override;
        void MetricsSampled(const quicr::ConnectionMetrics&) override;

      private:
        bool terminate_;
        std::string configfile_;
        ini::IniFile inif_;
    };

    class PerfSubscribeTrackHandler : public quicr::SubscribeTrackHandler
    {
      private:
        PerfSubscribeTrackHandler(const PerfConfig& perf_config);

      public:
        static auto Create(const std::string& section_name, ini::IniFile& inif);
        void ObjectReceived(const quicr::ObjectHeaders&, quicr::BytesSpan) override { SPDLOG_INFO("ObjectReceived"); }
        void StatusChanged(Status status) override;
        void MetricsSampled(const quicr::SubscribeTrackMetrics& metrics) override { metrics_ = metrics; }
        const quicr::SubscribeTrackMetrics& GetMetrics() const noexcept { return metrics_; }

      private:
        PerfConfig perf_config_;
        quicr::SubscribeTrackMetrics metrics_;
    };

    class PerfSubClient : public quicr::Client
    {
      public:
        PerfSubClient(const quicr::ClientConfig& cfg, const std::string& configfile);
        void StatusChanged(Status status) override;
        void MetricsSampled(const quicr::ConnectionMetrics&) override {}

      private:
        bool terminate_;
        std::string configfile_;
        ini::IniFile inif_;
    };

} // namespace qperf