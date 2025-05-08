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
    PerfPublishTrackHandler::PerfPublishTrackHandler(const std::shared_ptr<spdlog::logger> logger,
                                                     const PerfConfig& perf_config)
      : PublishTrackHandler(perf_config.full_track_name, perf_config.track_mode, perf_config.priority, perf_config.ttl)
      , logger_(logger)
      , perf_config_(perf_config)
      , terminate_(false)
      , last_bytes_(0)
      , test_mode_(qperf::TestMode::kNone)
      , group_id_(0)
      , object_id_(0)
      , first_write_(true)
      , first_metrics_write_(true)
    {
        memset(&test_metrics_, '\0', sizeof(test_metrics_));
    }

    auto PerfPublishTrackHandler::Create(const std::shared_ptr<spdlog::logger> logger,
                                         const std::string& section_name,
                                         ini::IniFile& inif)
    {
        PerfConfig perf_config;
        PopulateScenarioFields(logger, section_name, inif, perf_config);
        return std::shared_ptr<PerfPublishTrackHandler>(new PerfPublishTrackHandler(logger, perf_config));
    }

    void PerfPublishTrackHandler::StatusChanged(Status status)
    {
        switch (status) {
            case Status::kOk: {
                SPDLOG_LOGGER_INFO(logger_, "PerfPublishTrackeHandler - status kOk");
                auto track_alias = GetTrackAlias();
                if (track_alias.has_value()) {
                    SPDLOG_LOGGER_INFO(logger_, "Track alias: {0} is ready to write", track_alias.value());
                    write_thread_ = SpawnWriter();
                }
            } break;
            case Status::kNotConnected:
                SPDLOG_LOGGER_INFO(logger_, "PerfPublishTrackeHandler - status kNotConnected");
                break;
            case Status::kNotAnnounced:
                SPDLOG_LOGGER_INFO(logger_, "PerfPublishTrackeHandler - status kNotAnnounced");
                break;
            case Status::kPendingAnnounceResponse:
                SPDLOG_LOGGER_INFO(logger_, "PerfPublishTrackeHandler - status kPendingAnnounceResponse");
                break;
            case Status::kAnnounceNotAuthorized:
                SPDLOG_LOGGER_INFO(logger_, "PerfPublishTrackeHandler - status kAnnounceNotAuthorized");
                break;
            case Status::kNoSubscribers:
                SPDLOG_LOGGER_INFO(logger_, "PerfPublishTrackeHandler - status kNoSubscribers");
                break;
            case Status::kSendingUnannounce:
                SPDLOG_LOGGER_INFO(logger_, "PerfPublishTrackeHandler - status kSendingUnannounce");
                break;
            default:
                SPDLOG_LOGGER_INFO(logger_, "PerfPublishTrackeHandler - status UNKNOWN");
                break;
        }
    }

    void PerfPublishTrackHandler::MetricsSampled(const quicr::PublishTrackMetrics& metrics)
    {
        std::lock_guard<std::mutex> _(mutex_);
        auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());

        std::cerr << "HELLO" << std::endl;

        if (first_metrics_write_) {
            std::cout << "time, id, log_type, BITRATE, test_name, bitrate, bitrate_str, delta_bytes, time_delta, "
                         "min_bitrate, "
                         "max_bitrate, avg_bitrate, "
                      << perf_config_.test_name << std::endl;

            std::cout << "time, id, log_type, TRACK_METRICS, test_name, bytes_published, "
                      << "last_sample_time,  "
                      << "objects_dropped_not_ok, "
                      << "objects_published, "
                      << "tx_buffer_drops, "
                      << "tx_callback_ms, "
                      << "tx_delayed_callback, "
                      << "tx_object_duration_us, "
                      << "tx_queue_discards, "
                      << "tx_queue_expired, "
                      << "tx_queue_size, "
                      << "tx_reset_wait, " << perf_config_.test_name << std::endl;
        }

        first_metrics_write_ = false;

        if (test_mode_ == qperf::TestMode::kRunning && last_bytes_ != 0) { // skip first metric reporting...
            // calculate bitrate metrics
            auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - last_metric_time_);
            // Check if more than 4 seconds have elapsed
            if (diff > std::chrono::seconds(4)) {

                std::uint64_t delta_bytes = metrics.bytes_published - last_bytes_;
                std::uint64_t bitrate = ((delta_bytes) * 8) / diff.count();
                test_metrics_.bitrate_total += bitrate;
                test_metrics_.max_publish_bitrate =
                  bitrate > test_metrics_.max_publish_bitrate ? bitrate : test_metrics_.max_publish_bitrate;
                test_metrics_.min_publish_bitrate =
                  bitrate < test_metrics_.min_publish_bitrate ? bitrate : test_metrics_.min_publish_bitrate;
                test_metrics_.metric_samples += 1;
                test_metrics_.avg_publish_bitrate = test_metrics_.bitrate_total / test_metrics_.metric_samples;
                SPDLOG_LOGGER_INFO(logger_,
                                   "BITRATE, {}, {} {}, {}, {}, {}, {}, {}, TRACK_BITRATE",
                                   perf_config_.test_name,
                                   bitrate,
                                   FormatBitrate(bitrate),
                                   delta_bytes,
                                   diff.count(),
                                   test_metrics_.min_publish_bitrate,
                                   test_metrics_.max_publish_bitrate,
                                   test_metrics_.avg_publish_bitrate);
            }
        }

        SPDLOG_LOGGER_INFO(logger_,
                           "{}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}",
                           "TRACK_METRICS",
                           perf_config_.test_name,
                           metrics.bytes_published,
                           metrics.last_sample_time,
                           metrics.objects_dropped_not_ok,
                           metrics.objects_published,
                           metrics.quic.tx_buffer_drops,
                           metrics.quic.tx_callback_ms,
                           metrics.quic.tx_delayed_callback,
                           metrics.quic.tx_object_duration_us,
                           metrics.quic.tx_queue_discards,
                           metrics.quic.tx_queue_expired,
                           metrics.quic.tx_queue_size,
                           metrics.quic.tx_reset_wait,
                           "TRACK_METRICS");

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

        if (first_write_) {
            std::cout << "time, id, log_type, PO, state, test_name, priority, ttl, group, object, "
                         "objects_published, bytes_published, PO "
                      << perf_config_.test_name << std::endl;
        }

        first_write_ = false;
        SPDLOG_LOGGER_INFO(logger_,
                           "PO, RUNNING, {}, {}, {}, {}, {}, {}, {}",
                           perf_config_.test_name,
                           perf_config_.priority,
                           perf_config_.ttl,
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

        test_complete.test_mode = test_mode_;
        test_complete.time = test_metrics_.end_transmit_time;
        memcpy(&test_complete.test_metrics, &test_metrics_, sizeof(test_metrics_));

        quicr::Bytes object_data(sizeof(test_complete));
        memcpy((void*)&object_data[0], (void*)&test_complete, sizeof(test_complete));

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
        SPDLOG_LOGGER_INFO(logger_,
                           "PO, COMPLETE, {}, {}, {}, {}, {}, {}",
                           perf_config_.test_name,
                           group_id_,
                           object_id_,
                           test_metrics_.total_published_objects,
                           test_metrics_.total_published_bytes,
                           total_transmit_time);
        SPDLOG_LOGGER_INFO(logger_, "--------------------------------------------");
        SPDLOG_LOGGER_INFO(logger_, "{}", perf_config_.test_name);
        SPDLOG_LOGGER_INFO(logger_, "Publish Object - Complete");
        SPDLOG_LOGGER_INFO(logger_, "      Total transmit time (ms) {}", total_transmit_time);
        SPDLOG_LOGGER_INFO(logger_,
                           "        Total pubished objects {}, bytes {}",
                           test_metrics_.total_published_objects,
                           test_metrics_.total_published_bytes);
        SPDLOG_LOGGER_INFO(logger_, "                 Bitrate (bps)");
        SPDLOG_LOGGER_INFO(logger_, "                           min {}", test_metrics_.min_publish_bitrate);
        SPDLOG_LOGGER_INFO(logger_, "                           max {}", test_metrics_.max_publish_bitrate);
        SPDLOG_LOGGER_INFO(logger_, "                           avg {}", test_metrics_.avg_publish_bitrate);
        SPDLOG_LOGGER_INFO(logger_,
                           "                               {}",
                           FormatBitrate(static_cast<std::uint32_t>(test_metrics_.avg_publish_bitrate)));
        SPDLOG_LOGGER_INFO(logger_, "--------------------------------------------");

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
            SPDLOG_LOGGER_INFO(
              logger_, "{} Waiting start delay {} ms", perf_config_.test_name, perf_config_.start_delay);
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
        SPDLOG_LOGGER_INFO(
          logger_, "{} Start transmitting for {} ms", perf_config_.test_name, perf_config_.total_transmit_time);

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

    PerfPubClient::PerfPubClient(const std::shared_ptr<spdlog::logger> logger,
                                 const quicr::ClientConfig& cfg,
                                 const std::string& configfile)
      : logger_(logger)
      , quicr::Client(cfg)
      , configfile_(configfile)
      , first_write_(true)
    {
    }

    void PerfPubClient::StatusChanged(Status status)
    {
        std::lock_guard<std::mutex> _(track_handlers_mutex_);
        switch (status) {
            case Status::kReady:
                SPDLOG_LOGGER_INFO(logger_, "PerfPubClient - kReady");
                inif_.load(configfile_);
                for (const auto& section_pair : inif_) {
                    const std::string& section_name = section_pair.first;
                    auto pub_handler =
                      track_handlers_.emplace_back(PerfPublishTrackHandler::Create(logger_, section_name, inif_));
                    PublishTrack(pub_handler);
                }
                break;

            case Status::kNotReady:
                SPDLOG_LOGGER_INFO(logger_, "PerfPubClient - kNotReady");
                break;
            case Status::kConnecting:
                SPDLOG_LOGGER_INFO(logger_, "PerfPubClient - kConnecting");
                break;
            case Status::kDisconnecting:
                SPDLOG_LOGGER_INFO(logger_, "PerfPubClient - kDisconnecting");
                break;
            case Status::kPendingSeverSetup:
                SPDLOG_LOGGER_INFO(logger_, "PerfPubClient - kPendingSeverSetup");
                break;

            // All of the rest of these are 'errors' and will set terminate_.
            case Status::kInternalError:
                SPDLOG_LOGGER_INFO(logger_, "PerfPubClient - kInternalError - terminate");
                terminate_ = true;
                break;
            case Status::kInvalidParams:
                SPDLOG_LOGGER_INFO(logger_, "PerfPubClient - kInvalidParams - terminate");
                terminate_ = true;
                break;
            case Status::kNotConnected:
                SPDLOG_LOGGER_INFO(logger_, "PerfPubClient - kNotConnected - terminate");
                terminate_ = true;
                break;
            case Status::kFailedToConnect:
                SPDLOG_LOGGER_INFO(logger_, "PerfPubClient - kFailedToConnect - terminate");
                terminate_ = true;
                break;
            default:
                SPDLOG_LOGGER_INFO(
                  logger_, "PerfPubClient - UNKNOWN - Connection failed {0}", static_cast<int>(status));
                terminate_ = true;
                break;
        }
    }

    void PerfPubClient::MetricsSampled(const quicr::ConnectionMetrics& connection_metrics)
    {
        if (first_write_) {
            std::cout << "time, id, log_type, invalid_ctrl_stream_msg, last_sample_time,"
                      << "rx_dgram_decode_failed, rx_dgram_invalid_type, connecrx_stream_buffer_error,"
                      << "connecrx_stream_invalid_type, connecrx_stream_unknown_track_alias, cwin_congested,"
                      << "prev_cwin_congested,"
                      << "rtt_us.min, rtt_us.max, rtt_us.avg,"
                      << "rx_dgrams,  rx_dgrams_bytes,"
                      << "rx_rate_bps.min, rx_rate_bps.max, rx_rate_bps.avg,"
                      << "srtt_us.min, srtt_us.max, srtt_us.avg,"
                      << " tx_congested,"
                      << "tx_cwin_bytes.min, tx_cwin_bytes.max, tx_cwin_bytes.avg,"
                      << "tx_dgram_ack, tx_dgram_cb, tx_dgram_drops,  tx_dgram_spurious,"
                      << "tx_in_transit_bytes.min, _tx_in_transit_bytes.max, tx_in_transit_bytes.avg,"
                      << "tx_lost_pkts,"
                      << "tx_rate_bps.min, tx_rate_bps.max, tx_rate_bps.avg,"
                      << "tx_retransmits, tx_spurious_losses,tx_timer_losses,"
                      << "CONNECTION_METRICS" << std::endl;
        }
        first_write_ = false;

        auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
        SPDLOG_LOGGER_INFO(logger_,
                           "{}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, "
                           "{}, {}, {}, {}, CONNECTION_METRICS",
                           connection_metrics.invalid_ctrl_stream_msg,
                           connection_metrics.last_sample_time,
                           connection_metrics.rx_dgram_decode_failed,
                           connection_metrics.rx_dgram_invalid_type,
                           connection_metrics.rx_stream_buffer_error,
                           connection_metrics.rx_stream_invalid_type,
                           connection_metrics.rx_stream_unknown_track_alias,
                           connection_metrics.quic.cwin_congested,
                           connection_metrics.quic.prev_cwin_congested,
                           connection_metrics.quic.rtt_us,
                           connection_metrics.quic.rx_dgrams,
                           connection_metrics.quic.rx_dgrams_bytes,
                           connection_metrics.quic.rx_rate_bps,
                           connection_metrics.quic.srtt_us,
                           connection_metrics.quic.tx_congested,
                           connection_metrics.quic.tx_cwin_bytes,
                           connection_metrics.quic.tx_dgram_ack,
                           connection_metrics.quic.tx_dgram_cb,
                           connection_metrics.quic.tx_dgram_drops,
                           connection_metrics.quic.tx_dgram_spurious,
                           connection_metrics.quic.tx_in_transit_bytes,
                           connection_metrics.quic.tx_lost_pkts,
                           connection_metrics.quic.tx_rate_bps,
                           connection_metrics.quic.tx_retransmits,
                           connection_metrics.quic.tx_spurious_losses,
                           connection_metrics.quic.tx_timer_losses);

        last_metric_time_ = now;
    }

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
    logger->set_pattern("%Y-%m-%d %H:%M:%S.%e, %n, %l, %v");

    auto config_file = result["config"].as<std::string>();
    SPDLOG_LOGGER_INFO(logger, "--------------------------------------------");
    SPDLOG_LOGGER_INFO(logger, "Starting...pub");
    SPDLOG_LOGGER_INFO(logger, "\tconfig file {}", config_file);
    SPDLOG_LOGGER_INFO(logger, "\tclient config:");
    SPDLOG_LOGGER_INFO(logger, "\t\tconnect_uri = {}", client_config.connect_uri);
    SPDLOG_LOGGER_INFO(logger, "\t\tendpoint = {}", client_config.endpoint_id);
    SPDLOG_LOGGER_INFO(logger, "--------------------------------------------");

    std::signal(SIGINT, HandleTerminateSignal);

    auto client = std::make_shared<qperf::PerfPubClient>(logger, client_config, config_file);

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
