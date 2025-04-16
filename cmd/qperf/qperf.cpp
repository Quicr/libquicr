// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <oss/cxxopts.hpp>
#include <quicr/client.h>
#include <quicr/detail/defer.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <functional>
#include <stack>
#include <thread>

namespace {
    std::condition_variable cv;
    std::mutex mutex;
    std::atomic_bool terminate = false;

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

    /**
     * @brief Publish track handler
     * @details Publish track handler used for the publish command line option
     */
    class PerfPublishTrackHandler : public quicr::PublishTrackHandler
    {
        PerfPublishTrackHandler(const quicr::FullTrackName& full_track_name,
                                quicr::TrackMode track_mode,
                                uint8_t default_priority,
                                uint32_t default_ttl)
          : quicr::PublishTrackHandler(full_track_name, track_mode, default_priority, default_ttl)
        {
        }

      public:
        static auto Create(const quicr::FullTrackName& full_track_name,
                           quicr::TrackMode track_mode,
                           uint8_t default_priority,
                           uint32_t default_ttl)
        {
            return std::shared_ptr<PerfPublishTrackHandler>(
              new PerfPublishTrackHandler(full_track_name, track_mode, default_priority, default_ttl));
        }

        void StatusChanged(Status status) override
        {
            switch (status) {
                case Status::kOk: {
                    if (auto track_alias = GetTrackAlias(); track_alias.has_value()) {
                        SPDLOG_INFO("Track alias: {0} is ready to read", track_alias.value());
                        cv.notify_one();
                    }
                } break;
                default:
                    break;
            }
        }

        void MetricsSampled(const quicr::PublishTrackMetrics& metrics) override { metrics_ = metrics; }

        const quicr::PublishTrackMetrics& GetMetrics() const noexcept { return metrics_; }

      private:
        quicr::PublishTrackMetrics metrics_;
    };

    /**
     * @brief  Subscribe track handler
     * @details Subscribe track handler used for the subscribe command line option.
     */
    class PerfSubscribeTrackHandler : public quicr::SubscribeTrackHandler
    {
        PerfSubscribeTrackHandler(const quicr::FullTrackName& full_track_name)
          : SubscribeTrackHandler(full_track_name,
                                  3,
                                  quicr::messages::GroupOrder::kAscending,
                                  quicr::messages::FilterType::kLatestObject)
        {
        }

      public:
        static auto Create(const quicr::FullTrackName& full_track_name)
        {
            return std::shared_ptr<PerfSubscribeTrackHandler>(new PerfSubscribeTrackHandler(full_track_name));
        }

        void ObjectReceived(const quicr::ObjectHeaders&, quicr::BytesSpan) override {}

        void StatusChanged(Status status) override
        {
            switch (status) {
                case Status::kOk: {
                    if (auto track_alias = GetTrackAlias(); track_alias.has_value()) {
                        SPDLOG_INFO("Track alias: {0} is ready to read", track_alias.value());
                        cv.notify_one();
                    }
                    break;
                }
                default:
                    break;
            }
        }

        void MetricsSampled(const quicr::SubscribeTrackMetrics& metrics) override { metrics_ = metrics; }

        const quicr::SubscribeTrackMetrics& GetMetrics() const noexcept { return metrics_; }

      private:
        quicr::SubscribeTrackMetrics metrics_;
    };

    /**
     * @brief MoQ client
     * @details Implementation of the MoQ Client
     */
    class PerfClient : public quicr::Client
    {
      public:
        PerfClient(const quicr::ClientConfig& cfg)
          : quicr::Client(cfg)
        {
        }

        void StatusChanged(Status status) override
        {
            switch (status) {
                case Status::kReady:
                    SPDLOG_INFO("Connection ready");
                    cv.notify_all();
                    break;
                case Status::kConnecting:
                    [[fallthrough]];
                case Status::kPendingSeverSetup:
                    break;
                default:
                    SPDLOG_INFO("Connection failed {0}", static_cast<int>(status));
                    terminate = true;
                    cv.notify_all();
                    break;
            }
        }

        void MetricsSampled(const quicr::ConnectionMetrics&) override {}
    };
}

void
HandleTerminateSignal(int)
{
    terminate = true;
    cv.notify_all();
}

int
main(int argc, char** argv)
{
    // clang-format off
    cxxopts::Options options("QPerf");
    options.add_options()
        ("endpoint_id",     "Name of the client",                                    cxxopts::value<std::string>()->default_value("perf@cisco.com"))
        ("tracks",          "Number of tracks per client",                           cxxopts::value<std::size_t>()->default_value("1"))
        ("s,msg_size",      "Byte size of message",                                  cxxopts::value<std::uint16_t>()->default_value("1024"))
        ("connect_uri",     "Relay to connect to",                                   cxxopts::value<std::string>()->default_value("moq://localhost:1234"))
        ("d,duration",      "The duration of the test in seconds",                   cxxopts::value<std::uint32_t>()->default_value("120"))
        ("i,interval",      "The interval in microseconds to send publish messages", cxxopts::value<std::uint32_t>()->default_value("1000"))
        ("p,priority",      "Priority for sending publish messages",                 cxxopts::value<std::uint8_t>()->default_value("1"))
        ("e,expiry_age",    "Expiry age of objects in ms",                           cxxopts::value<std::uint16_t>()->default_value("5000"))
        ("reliable",        "Should use reliable per group",                         cxxopts::value<bool>())
        ("g,group_size",    "Size before group index changes",                       cxxopts::value<std::uint16_t>()->default_value("0"))
        ("h,help",          "Print usage");
    // clang-format on

    cxxopts::ParseResult result;

    try {
        result = options.parse(argc, argv);
    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "Caught exception while parsing arguments: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    if (result.count("help")) {
        std::cerr << options.help() << std::endl;
        return EXIT_SUCCESS;
    }

    const std::size_t tracks = result["tracks"].as<std::size_t>();
    const std::uint16_t msg_size = result["msg_size"].as<std::uint16_t>();
    const std::uint8_t priority = result["priority"].as<std::uint8_t>();
    const std::uint16_t expiry_age = result["expiry_age"].as<std::uint16_t>();
    const std::chrono::microseconds interval(result["interval"].as<std::uint32_t>());
    const std::chrono::seconds duration(result["duration"].as<std::uint32_t>());
    const bool reliable = result["reliable"].as<bool>();
    const std::uint16_t group_size = result["group_size"].as<std::uint16_t>();
    const quicr::TrackMode track_mode = reliable ? quicr::TrackMode::kStream : quicr::TrackMode::kDatagram;

    const quicr::TransportConfig config{
        .tls_cert_filename = "",
        .tls_key_filename = "",
        .time_queue_max_duration = expiry_age,
        .use_reset_wait_strategy = false,
        .quic_qlog_path = "",
    };

    const quicr::ClientConfig client_config{
        {
          .endpoint_id = result["endpoint_id"].as<std::string>(),
          .transport_config = config,
          .metrics_sample_ms = 5000,
        },
        result["connect_uri"].as<std::string>(),
    };

    const auto logger = spdlog::stderr_color_mt("PERF");

    auto client = std::make_shared<PerfClient>(client_config);

    std::signal(SIGINT, HandleTerminateSignal);

    try {
        std::unique_lock lock(mutex);
        client->Connect();

        cv.wait_for(lock, std::chrono::seconds(30));

        if (client->GetStatus() != quicr::Client::Status::kReady) {
            SPDLOG_LOGGER_CRITICAL(logger, "Failed to connect to relay '{0}'", client_config.connect_uri);
            return EXIT_FAILURE;
        }
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_CRITICAL(
          logger, "Failed to connect to relay '{0}' with exception: {1}", client_config.connect_uri, e.what());
        return EXIT_FAILURE;
    } catch (...) {
        SPDLOG_LOGGER_CRITICAL(logger, "Unexpected error connecting to relay");
        return EXIT_FAILURE;
    }

    defer(client->Disconnect());

    std::vector<std::shared_ptr<PerfPublishTrackHandler>> track_handlers;
    std::vector<std::shared_ptr<PerfSubscribeTrackHandler>> sub_track_handlers;
    for (std::uint16_t i = 0; i < tracks; ++i) {
        auto full_track_name = MakeFullTrackName("perf/" + std::to_string(i), "0");
        auto pub_handler = track_handlers.emplace_back(
          PerfPublishTrackHandler::Create(full_track_name, track_mode, priority, expiry_age));
        client->PublishTrack(pub_handler);

        auto sub_handler = sub_track_handlers.emplace_back(PerfSubscribeTrackHandler::Create(full_track_name));
        client->SubscribeTrack(sub_handler);
    }

    defer({
        for (const auto& handler : track_handlers)
            client->UnpublishTrack(handler);
        for (const auto& handler : sub_track_handlers)
            client->UnsubscribeTrack(handler);
    });

    std::unique_lock lock(mutex);

    cv.wait(lock, [&] {
        return terminate.load() || std::all_of(sub_track_handlers.begin(), sub_track_handlers.end(), [](auto&& h) {
                   return h->GetStatus() == quicr::SubscribeTrackHandler::Status::kOk;
               });
    });

    cv.wait(lock, [&] {
        return terminate.load() || std::all_of(track_handlers.begin(), track_handlers.end(), [](auto&& h) {
                   return h->GetStatus() == quicr::PublishTrackHandler::Status::kOk;
               });
    });

    if (terminate) {
        SPDLOG_LOGGER_INFO(logger, "Received interrupt, exiting early");
        return EXIT_SUCCESS;
    }

    SPDLOG_LOGGER_INFO(logger, "+==========================================+");
    SPDLOG_LOGGER_INFO(logger, "| Starting test of duration {0} seconds", duration.count());
    SPDLOG_LOGGER_INFO(logger, "+-------------------------------------------");
    SPDLOG_LOGGER_INFO(logger, "| *                 Tracks: {0}", tracks);

    if (interval != std::chrono::microseconds::zero()) {
        const std::uint64_t bitrate = (msg_size * 8) / (interval.count() / 1e6);
        const std::uint64_t expected_objects = 1e6 / interval.count();
        SPDLOG_LOGGER_INFO(logger, "| *         Approx bitrate: {0}", FormatBitrate(bitrate));
        SPDLOG_LOGGER_INFO(logger, "| *          Total bitrate: {0}", FormatBitrate(bitrate * tracks));
        SPDLOG_LOGGER_INFO(logger, "| *     Expected Objects/s: {0}", expected_objects);
        SPDLOG_LOGGER_INFO(logger, "| *        Total Objects/s: {0}", (expected_objects * tracks));
        SPDLOG_LOGGER_INFO(logger, "| * Total Expected Objects: {0}", (expected_objects * tracks * duration.count()));
    }

    SPDLOG_LOGGER_INFO(logger, "+==========================================+");

    std::atomic_size_t finished_publishers = 0;
    std::atomic_size_t total_attempted_published_objects = 0;
    quicr::Bytes data(msg_size, 0);

    std::vector<std::thread> threads;
    defer(for (auto& thread : threads) { thread.join(); });

    const auto start = std::chrono::high_resolution_clock::now();

    for (const auto& handler : track_handlers) {
        threads.emplace_back([&] {
            std::size_t group = 0;
            std::size_t objects = 0;

            ::LoopFor(duration, interval, [&] {
                if (objects++ == group_size) {
                    ++group;
                    objects = 0;
                }

                quicr::ObjectHeaders header = {
                    .group_id = group,
                    .object_id = objects,
                    .payload_length = data.size(),
                    .status = quicr::ObjectStatus::kAvailable,
                    .priority = priority,
                    .ttl = expiry_age,
                    .track_mode = track_mode,
                    .extensions = std::nullopt,
                };

                handler->PublishObject(header, data);
                ++total_attempted_published_objects;
            });

            ++finished_publishers;
            cv.notify_one();
        });
    }

    cv.wait(lock, [&] { return terminate.load() || finished_publishers.load() == tracks; });

    const auto end = std::chrono::high_resolution_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - start);

    SPDLOG_LOGGER_INFO(logger, "| Test complete, collecting metrics...");
    cv.wait_for(lock, std::chrono::seconds(10));

    std::size_t total_objects_published = 0;
    std::size_t total_bytes_published = 0;
    std::size_t total_objects_received = 0;
    std::size_t total_bytes_received = 0;

    for (const auto& handler : track_handlers) {
        total_objects_published += handler->GetMetrics().objects_published;
        total_bytes_published += handler->GetMetrics().bytes_published;
    }

    for (const auto& handler : sub_track_handlers) {
        total_objects_received += handler->GetMetrics().objects_received;
        total_bytes_received += handler->GetMetrics().bytes_received;
    }

    SPDLOG_LOGGER_INFO(logger, "+==========================================+");
    SPDLOG_LOGGER_INFO(logger, "| Results");
    SPDLOG_LOGGER_INFO(logger, "+-------------------------------------------");
    SPDLOG_LOGGER_INFO(logger, "| *          Duration: {0} seconds", elapsed.count());
    SPDLOG_LOGGER_INFO(logger, "| * Attempted Objects: {0}", total_attempted_published_objects.load());
    SPDLOG_LOGGER_INFO(logger, "| * Published Objects: {0}", total_objects_published);
    SPDLOG_LOGGER_INFO(logger, "| *  Received Objects: {0}", total_objects_received);
    SPDLOG_LOGGER_INFO(logger, "| *   Published Bytes: {0}", total_bytes_published);
    SPDLOG_LOGGER_INFO(logger, "| *    Received Bytes: {0}", total_bytes_received);
    SPDLOG_LOGGER_INFO(logger, "+==========================================+");

    return EXIT_SUCCESS;
}
