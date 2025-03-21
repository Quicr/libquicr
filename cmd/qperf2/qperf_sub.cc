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
#include <string>
#include <thread>

#include "qperf_sub.hpp"

namespace qperf {

    std::atomic_bool terminate = false;

    /**
     * @brief  Subscribe track handler
     * @details Subscribe track handler used for the subscribe command line option.
     */
    PerfSubscribeTrackHandler::PerfSubscribeTrackHandler(const PerfConfig& perf_config, std::uint32_t test_identifier)
      : SubscribeTrackHandler(perf_config.full_track_name,
                              perf_config.priority,
                              quicr::ctrl_messages::GroupOrderEnum::kOriginalPublisherOrder,
                              quicr::ctrl_messages::FilterTypeEnum::kLatestObject)
      , terminate_(false)
      , perf_config_(perf_config)
      , first_pass_(true)
      , last_bytes_(0)
      , local_now_(0)
      , last_local_now_(0)
      , total_objects_(0)
      , total_bytes_(0)
      , test_identifier_(test_identifier)
      , test_mode_(qperf::TestMode::kNone)
      , max_bitrate_(0)
      , min_bitrate_(0)
      , avg_bitrate_(0.0)
      , metric_samples_(0)
      , bitrate_total_(0)
      , max_object_time_delta_(0)
      , min_object_time_delta_(std::numeric_limits<std::int64_t>::max())
      , avg_object_time_delta_(0.0)
      , total_time_delta_(0)
      , max_object_arrival_delta_(0)
      , min_object_arrival_delta_(std::numeric_limits<std::int64_t>::max())
      , avg_object_arrival_delta_(0.0)
      , total_arrival_delta_(0)
    {
    }

    auto PerfSubscribeTrackHandler::Create(const std::string& section_name,
                                           ini::IniFile& inif,
                                           std::uint32_t test_identifier)
    {
        PerfConfig perf_config;
        PopulateScenarioFields(section_name, inif, perf_config);
        return std::shared_ptr<PerfSubscribeTrackHandler>(new PerfSubscribeTrackHandler(perf_config, test_identifier));
    }

    void PerfSubscribeTrackHandler::StatusChanged(Status status)
    {
        switch (status) {
            case Status::kOk: {
                auto track_alias = GetTrackAlias();
                if (track_alias.has_value()) {
                    SPDLOG_INFO(
                      "{}, {}, {} Ready to read", test_identifier_, perf_config_.test_name, track_alias.value());
                }
                break;
            }
            case Status::kNotConnected:
                SPDLOG_INFO("{}, {} Subscribe Handler - kNotConnected", test_identifier_, perf_config_.test_name);
                break;
            case Status::kNotSubscribed:
                SPDLOG_INFO("{}, {} Subscribe Handler - kNotSubscribed", test_identifier_, perf_config_.test_name);
                break;
            case Status::kPendingResponse:
                SPDLOG_INFO(
                  "{}, {} Subscribe Handler - kPendingSubscribeResponse", test_identifier_, perf_config_.test_name);
                break;

            // rest of these terminate
            case Status::kSendingUnsubscribe:
                SPDLOG_INFO("{}, {} Subscribe Handler - kSendingUnsubscribe", test_identifier_, perf_config_.test_name);
                terminate_ = true;
                break;
            case Status::kError:
                SPDLOG_INFO("{}, {} Subscribe Handler - kSubscribeError", test_identifier_, perf_config_.test_name);
                terminate_ = true;
                break;
            case Status::kNotAuthorized:
                SPDLOG_INFO("{}, {} Subscribe Handler - kNotAuthorized", test_identifier_, perf_config_.test_name);
                terminate_ = true;
                break;
            default:
                SPDLOG_INFO("{}, {} Subscribe Handler - UNKNOWN", test_identifier_, perf_config_.test_name);
                // leave...
                terminate_ = true;
                break;
        }
    }

    void PerfSubscribeTrackHandler::ObjectReceived(const quicr::ObjectHeaders& object_header,
                                                   quicr::BytesSpan data_span)
    {
        auto received_time = std::chrono::system_clock::now();
        local_now_ = std::chrono::time_point_cast<std::chrono::microseconds>(received_time).time_since_epoch().count();

        total_objects_ += 1;
        total_bytes_ += data_span.size();

        if (first_pass_) {

            last_local_now_ = local_now_;
            start_data_time_ = local_now_;
        }

        memcpy(&test_mode_, data_span.data(), sizeof(std::uint8_t));

        if (test_mode_ == qperf::TestMode::kRunning) {

            qperf::ObjectTestHeader test_header;
            memset(&test_header, '\0', sizeof(test_header));
            memcpy(&test_header,
                   &data_span[0],
                   data_span.size() < sizeof(test_header) ? sizeof(test_header.test_mode) : sizeof(test_header));

            auto remote_now = test_header.time;
            std::int64_t transmit_delta = local_now_ - remote_now;
            std::int64_t arrival_delta = local_now_ - last_local_now_;

            if (transmit_delta <= 0) {
                SPDLOG_INFO("-- negative/zero transmit delta (check ntp) -- {} {} {} {} {}",
                            object_header.group_id,
                            object_header.object_id,
                            local_now_,
                            remote_now,
                            transmit_delta);
            }

            if (arrival_delta <= 0) {
                SPDLOG_INFO("-- negative/zero arrival delta -- {} {} {} {} {}",
                            object_header.group_id,
                            object_header.object_id,
                            local_now_,
                            last_local_now_,
                            arrival_delta);
            }

            if (first_pass_) {
                SPDLOG_INFO("--------------------------------------------");
                SPDLOG_INFO("{}", perf_config_.test_name);
                SPDLOG_INFO("Started Receiving");
                SPDLOG_INFO("\tTest time {} ms", perf_config_.total_transmit_time);
                SPDLOG_INFO("--------------------------------------------");
            }

            SPDLOG_INFO("OR, RUNNING, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}",
                        test_identifier_,
                        perf_config_.test_name,
                        object_header.group_id,
                        object_header.object_id,
                        data_span.size(),
                        local_now_,
                        remote_now,
                        transmit_delta,
                        arrival_delta,
                        total_objects_,
                        total_bytes_);

            if (!first_pass_) {

                total_time_delta_ += transmit_delta;
                max_object_time_delta_ = transmit_delta > (std::int64_t)max_object_time_delta_
                                           ? transmit_delta
                                           : (std::int64_t)max_object_time_delta_;
                min_object_time_delta_ = transmit_delta < (std::int64_t)min_object_time_delta_
                                           ? transmit_delta
                                           : (std::int64_t)min_object_time_delta_;

                total_arrival_delta_ += arrival_delta;
                max_object_arrival_delta_ = arrival_delta > (std::int64_t)max_object_arrival_delta_
                                              ? arrival_delta
                                              : (std::int64_t)max_object_arrival_delta_;
                min_object_arrival_delta_ = arrival_delta < (std::int64_t)min_object_arrival_delta_
                                              ? arrival_delta
                                              : (std::int64_t)min_object_arrival_delta_;
            }

        } else if (test_mode_ == qperf::TestMode::kComplete) {

            ObjectTestComplete test_complete;

            memset(&test_complete, '\0', sizeof(test_complete));
            memcpy(&test_complete, data_span.data(), sizeof(test_complete));

            std::int64_t total_time = local_now_ - start_data_time_;
            avg_object_time_delta_ = (double)total_time_delta_ / (double)total_objects_;
            avg_object_arrival_delta_ =
              (double)total_arrival_delta_ / (double)total_objects_ - 1; // subtract 1st object

            SPDLOG_INFO("--------------------------------------------");
            SPDLOG_INFO("{}", perf_config_.test_name);
            SPDLOG_INFO("Testing Complete");
            SPDLOG_INFO("       Total test run time (ms) {}", total_time / 1000.0f);
            SPDLOG_INFO("      Configured test time (ms) {}", perf_config_.total_transmit_time);
            SPDLOG_INFO("       Total subscribed objects {}, bytes {}", total_objects_, total_bytes_);
            SPDLOG_INFO("        Total published objects {}, bytes {}",
                        test_complete.test_metrics.total_published_objects,
                        test_complete.test_metrics.total_published_bytes);
            SPDLOG_INFO("       Subscribed delta objects {}, bytes {}",
                        test_complete.test_metrics.total_published_objects - total_objects_,
                        test_complete.test_metrics.total_published_bytes - total_bytes_);
            SPDLOG_INFO("                  Bitrate (bps):");
            SPDLOG_INFO("                            min {}", min_bitrate_);
            SPDLOG_INFO("                            max {}", max_bitrate_);
            SPDLOG_INFO("                            avg {:.3f}", avg_bitrate_);
            SPDLOG_INFO("                                {}", FormatBitrate(static_cast<std::uint32_t>(avg_bitrate_)));
            SPDLOG_INFO("        Object time delta (us):");
            SPDLOG_INFO("                            min {}", min_object_time_delta_);
            SPDLOG_INFO("                            max {}", max_object_time_delta_);
            SPDLOG_INFO("                            avg {:04.3f} ", avg_object_time_delta_);
            SPDLOG_INFO("     Object arrival delta (us):");
            SPDLOG_INFO("                            min {}", min_object_arrival_delta_);
            SPDLOG_INFO("                            max {}", max_object_arrival_delta_);
            SPDLOG_INFO("                            avg {:04.3f}", avg_object_arrival_delta_);
            SPDLOG_INFO("--------------------------------------------");
            // id,test_name,total_time,total_transmit_time,total_objects,total_bytes,sent_object,sent_bytes,min_bitrate,max_bitrate,avg_bitrate,min_time,maxtime,avg_time,min_arrival,max_arrival,avg_arrival
            SPDLOG_INFO("OR COMPLETE, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}",
                        test_identifier_,
                        perf_config_.test_name,
                        total_time,
                        perf_config_.total_transmit_time,
                        total_objects_,
                        total_bytes_,
                        test_complete.test_metrics.total_published_objects,
                        test_complete.test_metrics.total_published_bytes,
                        min_bitrate_,
                        max_bitrate_,
                        avg_bitrate_,
                        min_object_time_delta_,
                        max_object_time_delta_,
                        avg_object_time_delta_,
                        min_object_arrival_delta_,
                        max_object_arrival_delta_,
                        avg_object_arrival_delta_);
            terminate_ = true;
            return;
        } else {
            SPDLOG_WARN(
              "OR, {}, {} - unkown data identifier {}", test_identifier_, perf_config_.test_name, (int)test_mode_);
        }

        last_local_now_ = local_now_;
        first_pass_ = false;
    }

    void PerfSubscribeTrackHandler::MetricsSampled(const quicr::SubscribeTrackMetrics& metrics)
    {
        metrics_ = metrics;
        if (last_bytes_ == 0) {
            last_metric_time_ =
              std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
            last_bytes_ = metrics.bytes_received;
            return;
        }

        auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
        auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - last_metric_time_);

        if (test_mode_ == qperf::TestMode::kRunning) {
            std::uint64_t delta_bytes = metrics_.bytes_received - last_bytes_;
            std::uint64_t bitrate = ((delta_bytes) * 8) / diff.count();
            metric_samples_ += 1;
            bitrate_total_ += bitrate;
            if (min_bitrate_ == 0) {
                min_bitrate_ = bitrate;
            }
            max_bitrate_ = bitrate > max_bitrate_ ? bitrate : max_bitrate_;
            min_bitrate_ = bitrate < min_bitrate_ ? bitrate : min_bitrate_;
            avg_bitrate_ = (double)bitrate_total_ / (double)metric_samples_;
            SPDLOG_INFO("Metrics:, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}",
                        test_identifier_,
                        perf_config_.test_name,
                        bitrate,
                        FormatBitrate(bitrate),
                        delta_bytes,
                        diff.count(),
                        metrics_.objects_received,
                        metrics_.bytes_received,
                        max_bitrate_,
                        min_bitrate_,
                        avg_bitrate_);
        }

        last_metric_time_ = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
        last_bytes_ = metrics.bytes_received;
    }

    /**
     * @brief MoQ client
     * @details Implementation of the MoQ Client
     */

    PerfSubClient::PerfSubClient(const quicr::ClientConfig& cfg,
                                 const std::string& configfile,
                                 std::uint32_t test_identifier)
      : quicr::Client(cfg)
      , configfile_(configfile)
      , test_identifier_(test_identifier)
    {
    }

    void PerfSubClient::StatusChanged(Status status)
    {
        switch (status) {
            case Status::kReady:
                SPDLOG_INFO("Client status - kReady");
                inif_.load(configfile_);
                for (const auto& section_pair : inif_) {
                    const std::string& section_name = section_pair.first;
                    SPDLOG_INFO("Starting test - {}", section_name);
                    auto sub_handler = track_handlers_.emplace_back(
                      PerfSubscribeTrackHandler::Create(section_name, inif_, test_identifier_));
                    SubscribeTrack(sub_handler);
                }
                break;
            case Status::kNotReady:
                SPDLOG_INFO("Client status - kNotReady");
                break;
            case Status::kConnecting:
                SPDLOG_INFO("Client status - kConnecting");
                break;
            case Status::kNotConnected:
                SPDLOG_INFO("Client status - kNotConnected");
                break;
            case Status::kPendingSeverSetup:
                SPDLOG_INFO("Client status - kPendingSeverSetup");
                break;

            case Status::kFailedToConnect:
                SPDLOG_ERROR("Client status - kFailedToConnect");
                terminate_ = true;
                break;
            case Status::kInternalError:
                SPDLOG_ERROR("Client status - kInternalError");
                terminate_ = true;
                break;
            case Status::kInvalidParams:
                SPDLOG_ERROR("Client status - kInvalidParams");
                terminate_ = true;
                break;
            default:
                SPDLOG_ERROR("Connection failed {0}", static_cast<int>(status));
                terminate_ = true;
                break;
        }
    }

    bool PerfSubClient::HandlersComplete()
    {
        std::lock_guard<std::mutex> _(track_handlers_mutex_);
        bool ret = true;
        // Don't like this - should be dependent on a 'state'
        if (track_handlers_.size() > 0) {
            for (auto handler : track_handlers_) {
                if (!handler->IsComplete()) {
                    ret = false;
                }
            }
        } else {
            ret = false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return ret;
    }

    void PerfSubClient::Terminate()
    {
        std::lock_guard<std::mutex> _(track_handlers_mutex_);
        for (auto handler : track_handlers_) {
            // Unpublish the track
            SPDLOG_INFO("unsubscribe track {}", handler->TestName());
            UnsubscribeTrack(handler);
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
        ("i,test_id",        "Test idenfiter number",                                cxxopts::value<std::uint32_t>()->default_value("1"))
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

    auto endpoint_test_id =
      result["endpoint_id"].as<std::string>() + ":" + std::to_string(result["test_id"].as<std::uint32_t>());

    quicr::ClientConfig client_config;
    client_config.connect_uri = result["connect_uri"].as<std::string>();
    client_config.endpoint_id = endpoint_test_id;
    client_config.metrics_sample_ms = 5000;
    client_config.transport_config = config;

    auto log_id = endpoint_test_id;

    const auto logger = spdlog::stderr_color_mt(log_id);

    auto test_identifier = result["test_id"].as<std::uint32_t>();

    auto client =
      std::make_shared<qperf::PerfSubClient>(client_config, result["config"].as<std::string>(), test_identifier);

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

    while (!terminate && !client->HandlersComplete()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    client->Terminate();
    client->Disconnect();

    return EXIT_SUCCESS;
}