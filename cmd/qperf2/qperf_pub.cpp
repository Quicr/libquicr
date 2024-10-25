// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <oss/cxxopts.hpp>
#include <quicr/client.h>
#include <quicr/detail/defer.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "qperf.hpp"

#include "inicpp.h"
#include "utils.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <functional>
#include <stack>
#include <thread>

#include <filesystem>

namespace qperf {
    PerfPublishTrackHandler::PerfPublishTrackHandler(const PerfConfig& perf_config)
      : PublishTrackHandler(perf_config.full_track_name, perf_config.track_mode, perf_config.priority, perf_config.ttl)
      , perf_config_(perf_config), terminate_(false)
    {
    }

    auto PerfPublishTrackHandler::Create(const std::string& section_name, ini::IniFile& inif)
    {
        PerfConfig perf_config;
        populate_scenario_fields(section_name, inif, perf_config);
        return std::shared_ptr<PerfPublishTrackHandler>(new PerfPublishTrackHandler(perf_config));
    }

    void PerfPublishTrackHandler::StatusChanged(Status status)
    {
        switch (status) {
            case Status::kOk: {
                SPDLOG_INFO("PerfPublishTrackeHandler - status kNotConnected");
                if (auto track_alias = GetTrackAlias(); track_alias.has_value()) {
                    SPDLOG_INFO("Track alias: {0} is ready to write", track_alias.value());
                    write_thread_ = SpawnWriter();
                }
            } break;
            case Status::kNotConnected:
                SPDLOG_INFO("PerfPublishTrackeHandler - status kNotConnected");
                break;
            case Status::kNotAnnounced:
                SPDLOG_INFO("PerfPublishTrackeHandler - status kNotAnnounced");
                break;
            case Status::kPendingAnnounceResponse:
                SPDLOG_INFO("PerfPublishTrackeHandler - status kPendingAnnounceResponse");
                break;
            case Status::kAnnounceNotAuthorized:
                SPDLOG_INFO("PerfPublishTrackeHandler - status kAnnounceNotAuthorized");
                break;
            case Status::kNoSubscribers:
                SPDLOG_INFO("PerfPublishTrackeHandler - status kNoSubscribers");
                break;
            case Status::kSendingUnannounce:
                SPDLOG_INFO("PerfPublishTrackeHandler - status kSendingUnannounce");
                break;
            default:
                SPDLOG_INFO("PerfPublishTrackeHandler - status UNKNOWN");
                break;
        }
    }

    void PerfPublishTrackHandler::MetricsSampled(const quicr::PublishTrackMetrics& metrics)
    {
        metrics_ = metrics;
        SPDLOG_INFO("Metrics...");
        SPDLOG_INFO("Total objects publics: {}", metrics_.objects_published);
        SPDLOG_INFO("Total bytes published {}", metrics_.bytes_published);
    }

    std::thread PerfPublishTrackHandler::SpawnWriter()
    {
        return std::thread([this] { WriteThread(); });
    }

    void PerfPublishTrackHandler::WriteThread()
    {
        std::vector<std::uint8_t> object_0_buffer(perf_config_.bytes_per_object_0);
        std::vector<std::uint8_t> object_not_0_buffer(perf_config_.bytes_per_object_not_0);

        group_id_ = -1;
        object_id_ = 0;

        if (perf_config_.transmit_time <= 0)
        {
            SPDLOG_WARN("Transmit time <= 0 - stopping test");
            return;
        }

        const std::chrono::time_point<std::chrono::system_clock> start_transmit_time = std::chrono::system_clock::now();            
        auto transmite_time_ms = std::chrono::milliseconds(perf_config_.transmit_time);
        auto end_transmit_time = start_transmit_time + transmite_time_ms;         

     
        // Delay befor trasnmitting
        if (perf_config_.start_delay > 0) {
            const std::chrono::time_point<std::chrono::system_clock> start = std::chrono::system_clock::now();            
            auto delay_ms = std::chrono::milliseconds(perf_config_.start_delay);
            auto end_time = start + delay_ms;

            auto start_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(start.time_since_epoch());
            auto delay_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(delay_ms);
            auto end_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time.time_since_epoch());

            while (!terminate_) {
                auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
                auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
                if (now > end_time) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(1000));
            }
        }

        // Transmit
        SPDLOG_INFO("Starting to transmit...");
        auto start_transmit = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());

        while (!terminate_) {
            auto start = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
            if (perf_config_.objects_per_group > 0)
            {
                if (!(object_id_ % perf_config_.objects_per_group)) {
                    auto m = object_id_ % perf_config_.objects_per_group;
                    object_id_ = 0;
                    group_id_ += 1;
                }
            }
            else
            {
                SPDLOG_WARN("Error - objects per groups <= 0");
            }

            quicr::ObjectHeaders object_headers{
                .group_id = group_id_,
                .object_id = object_id_,
                .payload_length = 0, // set later...
                .priority = perf_config_.priority,
                .ttl = perf_config_.ttl,
                .track_mode = perf_config_.track_mode,
                .extensions{},
            };
            //SetDefaultPriority(perf_config_.priority);
            perf_config_.track_mode = quicr::TrackMode::kStreamPerGroup;
            SetDefaultTrackMode(quicr::TrackMode::kStreamPerGroup);

            SPDLOG_WARN("Priorty = {}", (int)object_headers.priority.value());
            SPDLOG_WARN("Track mode = {}", (int)object_headers.track_mode.value());
            if (!object_headers.priority.has_value())
            {
                std::cerr << "priority has no value" << std::endl;
                abort();
            }
            if (object_headers.priority > 4)
            { 
                std::cerr << "priority  = " << (int)object_headers.priority.value() << std::endl;
            }

            if (object_id_ == 0) {
                object_headers.payload_length = perf_config_.bytes_per_object_0;
                PublishObject(object_headers, object_0_buffer);
            } else {
                object_headers.payload_length = perf_config_.bytes_per_object_not_0;
                PublishObject(object_headers, object_not_0_buffer);
            }           

            //auto p = object_headers.priority.value();
            //SPDLOG_INFO("Publish g:{} o:{} s:{} p:{}", group_id_, object_id_, object_headers.payload_length, p); // (int)object_headers.priority.value(), object_headers.ttl);

            // Check if we are done...
            auto current_transmit_time = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
            if (current_transmit_time >= end_transmit_time)
            {
                SPDLOG_INFO("Testing time completed...");
                return;
            }           

            // Wait interval
            if (perf_config_.transmit_interval >= 0) {
                std::uint64_t delay_us = static_cast<std::uint64_t>(1000.0 * perf_config_.transmit_interval);
                auto delay = std::chrono::microseconds(delay_us);
                auto end_time = start + delay;
                while (!terminate_) {
                    auto now =
                      std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
                    auto diff = now - end_time;
                    if (diff.count() > 0) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(1000));
                }
            }
            else
            {
                SPDLOG_WARN("Transmit interval is < 0");
            }
            object_id_ += 1;
        };
    }

    void PerfPublishTrackHandler::StopWriter()
    {
        terminate_ = true;
        if (write_thread_.joinable()) {
            write_thread_.join();
        }
    }

    PerfPubClient::PerfPubClient(const quicr::ClientConfig& cfg, const std::string& configfile)
      : quicr::Client(cfg)
      , configfile_(configfile)
    {
    }

    void PerfPubClient::StatusChanged(Status status)
    {
        std::vector<std::shared_ptr<PerfPublishTrackHandler>> track_handlers;
        switch (status) {
            case Status::kReady:
                std::cerr << "PerfPubClient - kReady" << std::endl;
                std::cerr << "path = " << std::filesystem::current_path() << std::endl;
                inif_.load(configfile_);
                for (const auto& sectionPair : inif_) {
                    const std::string& section_name = sectionPair.first;
                    const ini::IniSection& section = sectionPair.second;
                    std::cerr << "section: " << section_name << std::endl;
                    auto pub_handler =
                      track_handlers.emplace_back(PerfPublishTrackHandler::Create(section_name, inif_));
                    PublishTrack(pub_handler);
                }
                break;
            case Status::kConnecting:
                std::cerr << "PerfPubClient - kConnecting" << std::endl;
                break;
            case Status::kPendingSeverSetup:
                std::cerr << "PerfPubClient - kPendingSeverSetup" << std::endl;
                break;
            default:
                std::cerr << "PerfPubClient - UKNOWN status" << std::endl;
                SPDLOG_INFO("Connection failed {0}", static_cast<int>(status));
                terminate_ = true;
                break;
        }
    }

    void PerfPubClient::MetricsSampled(const quicr::ConnectionMetrics&) {}
}

bool terminate = false;

void
HandleTerminateSignal(int)
{
    terminate = true;
}

int
main(int argc, char** argv)
{
    // clang-format off
    cxxopts::Options options("QPerf");
    options.add_options()
        ("endpoint_id",     "Name of the client",                                    cxxopts::value<std::string>()->default_value("perf@cisco.com"))
        ("connect_uri",     "Relay to connect to",                                   cxxopts::value<std::string>()->default_value("moq://localhost:1234"))
        ("c,config",        "Scenario config file",                                  cxxopts::value<std::string>()->default_value("./config.ini"))
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

    const quicr::TransportConfig config{
        .tls_cert_filename = "",
        .tls_key_filename = "",
        .time_queue_max_duration = 5000,
        .use_reset_wait_strategy = false,
        .quic_qlog_path = "",
    };

    const quicr::ClientConfig client_config{
        {
          .endpoint_id = result["endpoint_id"].as<std::string>(),
          .transport_config = config,
          .metrics_sample_ms = 5000,
        },
        .connect_uri = result["connect_uri"].as<std::string>(),
    };

    const auto logger = spdlog::stderr_color_mt("PERF");

    auto client = std::make_shared<qperf::PerfPubClient>(client_config, result["config"].as<std::string>());

    std::signal(SIGINT, HandleTerminateSignal);

    try {
        client->Connect();
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_CRITICAL(
          logger, "Failed to connect to relay '{0}' with exception: {1}", client_config.connect_uri, e.what());
        return EXIT_FAILURE;
    } catch (...) {
        SPDLOG_LOGGER_CRITICAL(logger, "Unexpected error connecting to relay");
        return EXIT_FAILURE;
    }

    defer(client->Disconnect());

    while (!terminate) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    /*
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
            SPDLOG_LOGGER_INFO(logger, "| * Total Expected Objects: {0}", (expected_objects * tracks *
       duration.count()));
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
                        .priority = priority,
                        .ttl = expiry_age,
                        .track_mode = track_mode,
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

        for (const auto& handler : track_handlers) {
            total_objects_published += handler->GetMetrics().objects_published;
            total_bytes_published += handler->GetMetrics().bytes_published;
        }

        SPDLOG_LOGGER_INFO(logger, "+==========================================+");
        SPDLOG_LOGGER_INFO(logger, "| Results");
        SPDLOG_LOGGER_INFO(logger, "+-------------------------------------------");
        SPDLOG_LOGGER_INFO(logger, "| *          Duration: {0} seconds", elapsed.count());
        SPDLOG_LOGGER_INFO(logger, "| * Attempted Objects: {0}", total_attempted_published_objects.load());
        SPDLOG_LOGGER_INFO(logger, "| * Published Objects: {0}", total_objects_published);
        SPDLOG_LOGGER_INFO(logger, "| *   Published Bytes: {0}", total_bytes_published);
        SPDLOG_LOGGER_INFO(logger, "+==========================================+");\

        */

    return EXIT_SUCCESS;
}
