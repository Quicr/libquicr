#pragma once

#include <cstdint>
#include <quicr/client.h>

#include "inicpp.h"
#include "qperf.hpp"
#include <chrono>

namespace qperf {
    class PerfPublishTrackHandler : public quicr::PublishTrackHandler
    {
      private:
        PerfPublishTrackHandler(const PerfConfig&);

      public:
        static auto Create(const std::string& section_name, ini::IniFile& inif);
        void StatusChanged(Status status) override;
        void MetricsSampled(const quicr::PublishTrackMetrics& metrics) override;

        qperf::TestMode TestMode() { return test_mode_; }

        std::chrono::time_point<std::chrono::system_clock> PublishObjectWithMetrics(quicr::BytesSpan object_span);
        std::uint64_t PublishTestComplete();

        std::thread SpawnWriter();
        void WriteThread();
        void StopWriter();

        bool IsComplete() { return (test_mode_ == qperf::TestMode::kComplete); }

      private:
        PerfConfig perf_config_;
        std::atomic_bool terminate_;
        uint64_t last_bytes_;
        qperf::TestMode test_mode_;
        uint64_t group_id_;
        uint64_t object_id_;

        std::thread write_thread_;
        std::chrono::time_point<std::chrono::system_clock> last_metric_time_;

        qperf::TestMetrics test_metrics_;
        std::mutex mutex_;
    };

    class PerfPubClient : public quicr::Client
    {
      public:
        PerfPubClient(const quicr::ClientConfig& cfg, const std::string& configfile);
        void StatusChanged(Status status) override;
        void MetricsSampled(const quicr::ConnectionMetrics&) override;
        bool GetTerminateStatus();
        bool HandlersComplete();
        void Terminate();

      private:
        bool terminate_;
        std::string configfile_;
        ini::IniFile inif_;
        std::vector<std::shared_ptr<PerfPublishTrackHandler>> track_handlers_;
        std::mutex track_handlers_mutex_;
    };

} // namespace qperf
