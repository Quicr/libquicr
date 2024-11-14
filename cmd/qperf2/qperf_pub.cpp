// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <oss/cxxopts.hpp>
#include <quicr/client.h>
#include <quicr/detail/defer.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "qperf_pub.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <thread>

namespace qperf {
    PerfPublishTrackHandler::PerfPublishTrackHandler(const PerfConfig& perf_config)
      : PublishTrackHandler(perf_config.full_track_name, perf_config.track_mode, perf_config.priority, perf_config.ttl)
      , perf_config_(perf_config)
      , terminate_(false)
      , last_bytes_(0)
      , test_mode_(qperf::TestMode::kNone)
      , group_id_(0)
      , object_id_(0)

    {
        memset(&test_metrics_, '\0', sizeof(test_metrics_));
    }

    auto PerfPublishTrackHandler::Create(const std::string& section_name, ini::IniFile& inif)
    {
        PerfConfig perf_config;
        PopulateScenarioFields(section_name, inif, perf_config);
        return std::shared_ptr<PerfPublishTrackHandler>(new PerfPublishTrackHandler(perf_config));
    }

    void PerfPublishTrackHandler::StatusChanged(Status status)
    {
        switch (status) {
            case Status::kOk: {
                SPDLOG_INFO("PerfPublishTrackeHandler - status kOk");
                auto track_alias = GetTrackAlias();
                if (track_alias.has_value()) {
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
        std::lock_guard<std::mutex> _(mutex_);
        auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
        if (test_mode_ == qperf::TestMode::kRunning && last_bytes_ != 0) { // skip first metric reporting...
            // calculate bitrate metrics
            auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - last_metric_time_);
            std::uint64_t delta_bytes = metrics.bytes_published - last_bytes_;
            std::uint64_t bitrate = ((delta_bytes) * 8) / diff.count();
            test_metrics_.bitrate_total += bitrate;
            test_metrics_.max_publish_bitrate =
              bitrate > test_metrics_.max_publish_bitrate ? bitrate : test_metrics_.max_publish_bitrate;
            test_metrics_.min_publish_bitrate =
              bitrate < test_metrics_.min_publish_bitrate ? bitrate : test_metrics_.min_publish_bitrate;
            test_metrics_.metric_samples += 1;
            test_metrics_.avg_publish_bitrate = test_metrics_.bitrate_total / test_metrics_.metric_samples;
            SPDLOG_INFO("{}: Bitrate: {} {} delta bytes {}, delta time {}, {}, {}, {}",
                        perf_config_.test_name,
                        bitrate,
                        FormatBitrate(bitrate),
                        delta_bytes,
                        diff.count(),
                        test_metrics_.max_publish_bitrate,
                        test_metrics_.min_publish_bitrate,
                        test_metrics_.avg_publish_bitrate);
        }

        last_metric_time_ = now;
        last_bytes_ = metrics.bytes_published;
    }

    std::chrono::time_point<std::chrono::system_clock> PerfPublishTrackHandler::PublishObjectWithMetrics(
      quicr::BytesSpan object_span)
    {
        std::lock_guard<std::mutex> _(mutex_);
        ObjectTestHeader test_header;
        memset(&test_header, '\0', sizeof(test_header));
        if (perf_config_.objects_per_group > 0) {
            if (!(object_id_ % perf_config_.objects_per_group)) {
                object_id_ = 0;
                group_id_ += 1;
            }
        } else {
            SPDLOG_WARN("{} Error - objects per groups <= 0", perf_config_.test_name);
        }

        quicr::ObjectHeaders object_headers;
        object_headers.group_id = group_id_;
        object_headers.object_id = object_id_;
        object_headers.payload_length = 0; // set later
        object_headers.priority = perf_config_.priority;
        object_headers.ttl = perf_config_.ttl;

        // get current time..
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();

        // update metrics
        if (test_metrics_.start_transmit_time == 0) {
            test_metrics_.start_transmit_time = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
        }

        // fill out test_header
        test_header.test_mode = qperf::TestMode::kRunning;
        test_header.time = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();

        // check how much we can write in the header
        auto header_bytes_to_copy =
          object_span.size() < sizeof(test_header) ? sizeof(test_header.test_mode) : sizeof(test_header);
        memcpy((void*)object_span.data(), (void*)&test_header, header_bytes_to_copy);
        object_headers.payload_length = object_span.size();

        // publish
        PublishObject(object_headers, object_span);

        SPDLOG_INFO("PO, RUNNING, {}, {}, {}, {}, {}",
                    perf_config_.test_name,
                    group_id_,
                    object_id_,
                    publish_track_metrics_.objects_published,
                    publish_track_metrics_.bytes_published);

        // return current time in ms - publish time
        return now;
    }

    std::uint64_t PerfPublishTrackHandler::PublishTestComplete()
    {
        std::lock_guard<std::mutex> _(mutex_);
        test_mode_ = qperf::TestMode::kComplete;
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();

        ObjectTestComplete test_complete;
        memset(&test_complete, '\0', sizeof(test_complete));

        // start_transmit_time is set when fist object is published
        test_metrics_.end_transmit_time = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();

        // test_metrics_.end_transmit_time;
        test_metrics_.total_published_objects = publish_track_metrics_.objects_published + 1;
        test_metrics_.total_published_bytes = publish_track_metrics_.bytes_published + sizeof(test_complete);
        test_metrics_.total_objects_dropped_not_ok = publish_track_metrics_.objects_dropped_not_ok;
        // following are calculated on metrics callback...
        // test_metrics_.max_bitrate
        // test_metrics_.min_bitrate
        // test_metrics_.avg_bitrate

        test_complete.test_mode = test_mode_;
        test_complete.time = test_metrics_.end_transmit_time;
        memcpy(&test_complete.test_metrics, &test_metrics_, sizeof(test_metrics_));

        quicr::Bytes object_data(sizeof(test_complete));
        memcpy((void*)&object_data[0], (void*)&test_complete, sizeof(test_complete));

        // SAH group_id_ += 1;
        object_id_ += 1;

        quicr::ObjectHeaders object_headers;
        object_headers.group_id = group_id_;
        object_headers.object_id = object_id_;
        object_headers.payload_length = 0;
        object_headers.priority = perf_config_.priority;
        object_headers.ttl = perf_config_.ttl;

        object_headers.payload_length = sizeof(test_complete);
        PublishObject(object_headers, object_data);

        auto total_transmit_time = test_metrics_.end_transmit_time - test_metrics_.start_transmit_time;
        SPDLOG_INFO("PO, COMPLETE, {}, {}, {}, {}, {}, {}",
                    perf_config_.test_name,
                    group_id_,
                    object_id_,
                    test_metrics_.total_published_objects,
                    test_metrics_.total_published_bytes,
                    total_transmit_time);
        SPDLOG_INFO("--------------------------------------------");
        SPDLOG_INFO("{}", perf_config_.test_name);
        SPDLOG_INFO("Publish Object - Complete");
        SPDLOG_INFO("\tTotal transmit time in {} ms", total_transmit_time);
        SPDLOG_INFO("\tTotal pubished objects {}, bytes {}",
                    test_metrics_.total_published_objects,
                    test_metrics_.total_published_bytes);
        SPDLOG_INFO("\tBitrate max {}, min {}, avg {}, {}",
                    test_metrics_.max_publish_bitrate,
                    test_metrics_.min_publish_bitrate,
                    test_metrics_.avg_publish_bitrate,
                    FormatBitrate(static_cast<std::uint32_t>(test_metrics_.avg_publish_bitrate)));

        SPDLOG_INFO("--------------------------------------------");

        return test_complete.time;
    }

    std::thread PerfPublishTrackHandler::SpawnWriter()
    {
        return std::thread([this] { WriteThread(); });
    }

    void PerfPublishTrackHandler::WriteThread()
    {
        quicr::Bytes object_0_buffer(perf_config_.bytes_per_group_start);
        quicr::Bytes object_not_0_buffer(perf_config_.bytes_per_group);

        for (std::size_t i = 0; i < object_0_buffer.size(); i++) {
            object_0_buffer[i] = i % 255;
        }

        for (std::size_t i = 0; i < object_not_0_buffer.size(); i++) {
            object_not_0_buffer[i] = i % 255;
        }

        group_id_ = 0;
        object_id_ = 0;

        if (perf_config_.total_test_time <= 0) {
            SPDLOG_WARN("Transmit time <= 0 - stopping test");
            return;
        }

        const std::chrono::time_point<std::chrono::system_clock> start_transmit_time = std::chrono::system_clock::now();
        auto transmit_time_ms = std::chrono::milliseconds(perf_config_.total_test_time);
        auto end_transmit_time = start_transmit_time + transmit_time_ms;

        // Delay befor trasnmitting
        if (perf_config_.start_delay > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
            test_mode_ = qperf::TestMode::kWaitPreTest;
            SPDLOG_INFO("{} Waiting start delay {} ms", perf_config_.test_name, perf_config_.start_delay);
            const std::chrono::time_point<std::chrono::system_clock> start = std::chrono::system_clock::now();
            auto delay_ms = std::chrono::milliseconds(perf_config_.start_delay);
            auto end_time = start + delay_ms;
            while (!terminate_) {
                auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
                if (now >= end_time) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
        }

        // Transmit
        SPDLOG_INFO("{} Start transmitting for {} ms", perf_config_.test_name, perf_config_.total_transmit_time);
        std::chrono::time_point<std::chrono::system_clock> last_publish_time;

        test_mode_ = qperf::TestMode::kRunning;
        while (!terminate_) {
            std::chrono::time_point<std::chrono::system_clock> last_publish_time;
            if (object_id_ == 0) {
                quicr::BytesSpan object_span(object_0_buffer);
                last_publish_time = PublishObjectWithMetrics(object_span);
            } else {
                quicr::BytesSpan object_span(object_not_0_buffer);
                last_publish_time = PublishObjectWithMetrics(object_span);
            }

            // Check if we are done...
            if (last_publish_time >= end_transmit_time) {
                // publish COMPLETE object  - end of test
                PublishTestComplete();
                terminate_ = true;
                return;
            }

            // Wait interval
            if (perf_config_.transmit_interval >= 0) {
                std::uint64_t interval_us = (perf_config_.transmit_interval * 1000.0f);
                std::this_thread::sleep_for(std::chrono::microseconds(interval_us));
            } else {
                SPDLOG_WARN("{} Transmit interval is < 0", perf_config_.test_name);
            }
            object_id_ += 1;
        };
        SPDLOG_WARN("{} Exiting writer thread.", perf_config_.test_name);
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
        std::lock_guard<std::mutex> _(track_handlers_mutex_);
        switch (status) {
            case Status::kReady:
                SPDLOG_INFO("PerfPubClient - kReady");
                inif_.load(configfile_);
                for (const auto& section_pair : inif_) {
                    const std::string& section_name = section_pair.first;
                    auto pub_handler =
                      track_handlers_.emplace_back(PerfPublishTrackHandler::Create(section_name, inif_));
                    PublishTrack(pub_handler);
                }
                break;

            case Status::kNotReady:
                SPDLOG_INFO("PerfPubClient - kNotReady");
                break;
            case Status::kConnecting:
                SPDLOG_INFO("PerfPubClient - kConnecting");
                break;
            case Status::kDisconnecting:
                SPDLOG_INFO("PerfPubClient - kDisconnecting");
                break;
            case Status::kPendingSeverSetup:
                SPDLOG_INFO("PerfPubClient - kPendingSeverSetup");
                break;

            // All of the rest of these are 'errors' and will set terminate_.
            case Status::kInternalError:
                SPDLOG_INFO("PerfPubClient - kInternalError - terminate");
                terminate_ = true;
                break;
            case Status::kInvalidParams:
                SPDLOG_INFO("PerfPubClient - kInvalidParams - terminate");
                terminate_ = true;
                break;
            case Status::kNotConnected:
                SPDLOG_INFO("PerfPubClient - kNotConnected - terminate");
                terminate_ = true;
                break;
            case Status::kFailedToConnect:
                SPDLOG_INFO("PerfPubClient - kFailedToConnect - terminate");
                terminate_ = true;
                break;
            default:
                SPDLOG_INFO("PerfPubClient - UNKNOWN - Connection failed {0}", static_cast<int>(status));
                terminate_ = true;
                break;
        }
    }

    void PerfPubClient::MetricsSampled(const quicr::ConnectionMetrics&) {}

    bool PerfPubClient::GetTerminateStatus()
    {
        return terminate_;
    }

    bool PerfPubClient::HandlersComplete()
    {
        std::lock_guard<std::mutex> _(track_handlers_mutex_);
        bool ret = true;
        // Don't like this - should be dependent on a 'state'
        if (track_handlers_.size() > 0) {
            for (auto handler : track_handlers_) {
                if (!handler->IsComplete()) {
                    ret = false;
                    break;
                }
            }
        } else {
            ret = false;
        }
        return ret;
    }

    void PerfPubClient::Terminate()
    {
        std::lock_guard<std::mutex> _(track_handlers_mutex_);
        for (auto handler : track_handlers_) {
            // Stop the handler writer thread...
            handler->StopWriter();
            // Unpublish the track
            UnpublishTrack(handler);
        }
        // we are done
        terminate_ = true;
    }
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

    quicr::TransportConfig config;
    config.tls_cert_filename = "";
    config.tls_key_filename = "";
    config.time_queue_max_duration = 5000;
    config.use_reset_wait_strategy = false;
    config.quic_qlog_path = "";

    quicr::ClientConfig client_config;
    client_config.endpoint_id = result["endpoint_id"].as<std::string>();
    client_config.metrics_sample_ms = 5000;
    client_config.transport_config = config;
    client_config.connect_uri = result["connect_uri"].as<std::string>();

    const auto logger = spdlog::stderr_color_mt("PERF");

    auto config_file = result["config"].as<std::string>();
    SPDLOG_INFO("--------------------------------------------");
    SPDLOG_INFO("Starting...pub");
    SPDLOG_INFO("\tconfig file {}", config_file);
    SPDLOG_INFO("\tclient config:");
    SPDLOG_INFO("\t\tconnect_uri = {}", client_config.connect_uri);
    SPDLOG_INFO("\t\tendpoint = {}", client_config.endpoint_id);
    SPDLOG_INFO("--------------------------------------------");

    std::signal(SIGINT, HandleTerminateSignal);

    auto client = std::make_shared<qperf::PerfPubClient>(client_config, config_file);

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

    while (!terminate && !client->HandlersComplete()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    client->Terminate();
    client->Disconnect();
    return EXIT_SUCCESS;
}