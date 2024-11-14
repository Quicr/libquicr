#pragma once

#include <cstdint>
#include <quicr/client.h>

#include "inicpp.h"
#include "qperf.hpp"

namespace qperf {
    class PerfSubscribeTrackHandler : public quicr::SubscribeTrackHandler
    {
      private:
        PerfSubscribeTrackHandler(const PerfConfig& perf_config, std::uint32_t test_identifier);

      public:
        static auto Create(const std::string& section_name, ini::IniFile& inif, std::uint32_t test_identifier);
        void ObjectReceived(const quicr::ObjectHeaders&, quicr::BytesSpan) override;
        void StatusChanged(Status status) override;
        void MetricsSampled(const quicr::SubscribeTrackMetrics& metrics) override;
        const quicr::SubscribeTrackMetrics& GetMetrics() const noexcept { return metrics_; }

        bool IsComplete() { return (terminate_ || (test_mode_ == qperf::TestMode::kComplete)); }

        std::string TestName() { return perf_config_.test_name; }

      private:
        std::atomic_bool terminate_;
        PerfConfig perf_config_;
        quicr::SubscribeTrackMetrics metrics_;

        std::chrono::time_point<std::chrono::system_clock> last_metric_time_;
        uint64_t last_bytes_;
        std::uint64_t local_now_;
        std::uint64_t last_local_now_;
        std::uint64_t start_data_time_;
        std::uint64_t total_objects_;
        std::uint64_t total_bytes_;
        std::uint32_t test_identifier_;
        qperf::TestMode test_mode_;

        std::uint64_t max_bitrate_;
        std::uint64_t min_bitrate_;
        double avg_bitrate_;

        std::uint32_t metric_samples_;
        std::uint64_t bitrate_total_;

        std::uint64_t max_object_time_delta_;
        std::uint64_t min_object_time_delta_;
        double avg_object_time_delta_;
        std::uint64_t total_time_delta_;

        std::uint64_t max_object_arrival_delta_;
        std::uint64_t min_object_arrival_delta_;
        double avg_object_arrival_delta_;
        std::uint64_t total_arrival_delta_;
    };

    class PerfSubClient : public quicr::Client
    {
      public:
        PerfSubClient(const quicr::ClientConfig& cfg, const std::string& configfile, std::uint32_t test_identifier);
        void StatusChanged(Status status) override;
        void MetricsSampled(const quicr::ConnectionMetrics&) override {}

        bool HandlersComplete();
        void Terminate();

      private:
        bool terminate_;
        std::string configfile_;
        ini::IniFile inif_;
        std::uint32_t test_identifier_;

        std::vector<std::shared_ptr<PerfSubscribeTrackHandler>> track_handlers_;

        std::mutex track_handlers_mutex_;
    };

} // namespace
