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

#include "qperf.hpp"

namespace qperf {

    std::atomic_bool terminate = false;

    /**
     * @brief  Subscribe track handler
     * @details Subscribe track handler used for the subscribe command line option.
     */
    PerfSubscribeTrackHandler::PerfSubscribeTrackHandler(const PerfConfig& perf_config) : SubscribeTrackHandler(perf_config.full_track_name), perf_config_(perf_config)
    {
    }

    auto PerfSubscribeTrackHandler::Create(const std::string& section_name, ini::IniFile& inif)
    {
        PerfConfig perf_config;
        populate_scenario_fields(section_name, inif, perf_config);
        return std::shared_ptr<PerfSubscribeTrackHandler>(new PerfSubscribeTrackHandler(perf_config));
    }



    void PerfSubscribeTrackHandler::StatusChanged(Status status)
    {
        SPDLOG_INFO("Subscribe Track Handler - StatusChanged {}", (int)status);
        switch (status) {
            case Status::kOk: {
                if (auto track_alias = GetTrackAlias(); track_alias.has_value()) {
                    SPDLOG_INFO("Track alias: {0} is ready to read", track_alias.value());
                }
                break;
            }
            case Status::kNotConnected:
                SPDLOG_INFO("Subscribe Handler - kNotConnected");
                break;
            case Status::kSubscribeError:
                SPDLOG_INFO("Subscribe Handler - kSubscribeError");
                break;
            case Status::kNotAuthorized:
                SPDLOG_INFO("Subscribe Handler - kNotAuthorized");
                break;
            case Status::kNotSubscribed:
                SPDLOG_INFO("Subscribe Handler - kNotSubscribed");            
                break;
            case Status::kPendingSubscribeResponse:
                SPDLOG_INFO("Subscribe Handler - kPendingSubscribeResponse");            
                break;
            case Status::kSendingUnsubscribe:
                SPDLOG_INFO("Subscribe Handler - kSendingUnsubscribe");      
                break;                                                                           
            default:
                SPDLOG_INFO("Subscribe Handler - UNKNOWN");
                // leave...            
                break;
        }
    }

    /**
     * @brief MoQ client
     * @details Implementation of the MoQ Client
     */

    PerfSubClient::PerfSubClient(const quicr::ClientConfig& cfg, const std::string& configfile)
      : quicr::Client(cfg), configfile_(configfile)
    {
    }

    void PerfSubClient::StatusChanged(Status status)
    {
        std::vector<std::shared_ptr<PerfSubscribeTrackHandler>> sub_track_handlers;
        switch (status) {
            case Status::kReady:
                SPDLOG_INFO("Client status - kReady");
                inif_.load(configfile_);
                for (const auto& sectionPair : inif_) {
                    const std::string& section_name = sectionPair.first;
                    const ini::IniSection& section = sectionPair.second;
                    std::cerr << "section: " << section_name << std::endl;
                    auto sub_handler = sub_track_handlers.emplace_back(PerfSubscribeTrackHandler::Create(section_name, inif_));
                    SubscribeTrack(sub_handler);
                }
                break;
            case Status::kNotReady:
                SPDLOG_INFO("Client status - kNotReady");
                break;
            case Status::kInternalError:
                SPDLOG_INFO("Client status - kInternalError");
                break;
            case Status::kInvalidParams:
                SPDLOG_INFO("Client status - kInvalidParams");
                break;
            case Status::kConnecting:
                SPDLOG_INFO("Client status - kConnecting");
                break;
            case Status::kNotConnected:
                SPDLOG_INFO("Client status - kNotConnected");
                break;                                                                
            case Status::kFailedToConnect:
                SPDLOG_INFO("Client status - kNotConnected");
                break;  
            case Status::kPendingSeverSetup:
                SPDLOG_INFO("Client status - kNotConnected");
                break;                                
            default:
                std::cerr << "PerfSubClient - UKNOWN status {}" << std::endl;
                SPDLOG_INFO("Connection failed {0}", static_cast<int>(status));
                terminate_ = true;
                break;
        }
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

    auto client = std::make_shared<qperf::PerfSubClient>(client_config, result["config"].as<std::string>());

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

    return EXIT_SUCCESS;
}