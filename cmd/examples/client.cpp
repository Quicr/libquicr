// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <oss/cxxopts.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <quicr/client.h>

#include "helper_functions.h"
#include "signal_handler.h"

namespace qclient_vars {
    bool publish_clock{ false };
}

/**
 * @brief  Subscribe track handler
 * @details Subscribe track handler used for the subscribe command line option.
 */
class MySubscribeTrackHandler : public quicr::SubscribeTrackHandler
{
  public:
    MySubscribeTrackHandler(const quicr::FullTrackName& full_track_name)
      : SubscribeTrackHandler(full_track_name)
    {
    }

    void ObjectReceived(const quicr::ObjectHeaders&, quicr::BytesSpan data) override
    {
        std::string msg(data.begin(), data.end());
        SPDLOG_INFO("Received message: {0}", msg);
    }

    void StatusChanged(Status status) override
    {
        switch (status) {
            case Status::kOk: {
                if (auto track_alias = GetTrackAlias(); track_alias.has_value()) {
                    SPDLOG_INFO("Track alias: {0} is ready to read", track_alias.value());
                }
            } break;
            default:
                break;
        }
    }
};

/**
 * @brief Publish track handler
 * @details Publish track handler used for the publish command line option
 */
class MyPublishTrackHandler : public quicr::PublishTrackHandler
{
  public:
    MyPublishTrackHandler(const quicr::FullTrackName& full_track_name,
                          quicr::TrackMode track_mode,
                          uint8_t default_priority,
                          uint32_t default_ttl)
      : quicr::PublishTrackHandler(full_track_name, track_mode, default_priority, default_ttl)
    {
    }

    void StatusChanged(Status status) override
    {
        switch (status) {
            case Status::kOk: {
                if (auto track_alias = GetTrackAlias(); track_alias.has_value()) {
                    SPDLOG_INFO("Track alias: {0} is ready to read", track_alias.value());
                }
            } break;
            default:
                break;
        }
    }
};

/**
 * @brief MoQ client
 * @details Implementation of the MoQ Client
 */
class MyClient : public quicr::Client
{
  public:
    MyClient(const quicr::ClientConfig& cfg, bool& stop_threads)
      : quicr::Client(cfg)
      , stop_threads_(stop_threads)
    {
    }

    void StatusChanged(Status status) override
    {
        switch (status) {
            case Status::kReady:
                SPDLOG_INFO("Connection ready");
                break;
            case Status::kConnecting:
                break;
            default:
                SPDLOG_INFO("Connection failed {0}", static_cast<int>(status));
                stop_threads_ = true;
                moq_example::terminate = true;
                moq_example::termination_reason = "Connection failed";
                moq_example::cv.notify_all();
                break;
        }
    }

  private:
    bool& stop_threads_;
};

/* -------------------------------------------------------------------------------------------------
 * Publisher Thread to perform publishing
 * -------------------------------------------------------------------------------------------------
 */

void
DoPublisher(const quicr::FullTrackName& full_track_name, const std::shared_ptr<quicr::Client>& client, const bool& stop)
{
    auto track_handler = std::make_shared<MyPublishTrackHandler>(
      full_track_name, quicr::TrackMode::kStreamPerGroup /*mode*/, 2 /*prirority*/, 3000 /*ttl*/);

    SPDLOG_INFO("Started publisher track");

    bool published_track{ false };
    bool sending{ false };
    uint64_t group_id{ 0 };
    uint64_t object_id{ 0 };

    while (not stop) {
        if ((!published_track) && (client->GetStatus() == MyClient::Status::kReady)) {
            SPDLOG_INFO("Publish track ");
            client->PublishTrack(track_handler);
            published_track = true;
        }

        if (track_handler->GetStatus() != MyPublishTrackHandler::Status::kOk) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (!sending) {
            SPDLOG_INFO("--------------------------------------------------------------------------");

            if (qclient_vars::publish_clock) {
                SPDLOG_INFO(" Publishing clock timestamp every second");
            } else {
                SPDLOG_INFO(" Type message and press enter to send");
            }

            SPDLOG_INFO("--------------------------------------------------------------------------");
            sending = true;
        }

        std::string msg;
        if (qclient_vars::publish_clock) {
            std::this_thread::sleep_for(std::chrono::milliseconds(999));
            msg = quicr::example::GetTimeStr();
            SPDLOG_INFO(msg);
        } else { // stdin
            getline(std::cin, msg);
            SPDLOG_INFO("Send message: {0}", msg);
        }

        if (object_id % 10 == 0) { // Set new group
            object_id = 0;
            group_id++;
        }

        quicr::ObjectHeaders obj_headers = { group_id,       object_id++,  msg.size(),  2 /*priority*/,
                                             3000 /* ttl */, std::nullopt, std::nullopt };

        track_handler->PublishObject(obj_headers, { reinterpret_cast<uint8_t*>(msg.data()), msg.size() });
    }

    client->UnpublishTrack(track_handler);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    SPDLOG_INFO("Publisher done track");
}

/* -------------------------------------------------------------------------------------------------
 * Subscriber thread to perform subscribe
 * -------------------------------------------------------------------------------------------------
 */
void
DoSubscriber(const quicr::FullTrackName& full_track_name,
             const std::shared_ptr<quicr::Client>& client,
             const bool& stop)
{
    auto track_handler = std::make_shared<MySubscribeTrackHandler>(full_track_name);

    SPDLOG_INFO("Started subscriber");

    bool subscribe_track{ false };

    while (not stop) {
        if ((!subscribe_track) && (client->GetStatus() == MyClient::Status::kReady)) {
            SPDLOG_INFO("Subscribing to track");
            client->SubscribeTrack(track_handler);
            subscribe_track = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    client->UnsubscribeTrack(track_handler);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    SPDLOG_INFO("Subscriber done track");
}

/* -------------------------------------------------------------------------------------------------
 * Main program
 * -------------------------------------------------------------------------------------------------
 */
quicr::ClientConfig
InitConfig(cxxopts::ParseResult& cli_opts, bool& enable_pub, bool& enable_sub)
{
    quicr::ClientConfig config;

    std::string qlog_path;
    if (cli_opts.count("qlog")) {
        qlog_path = cli_opts["qlog"].as<std::string>();
    }

    if (cli_opts.count("debug") && cli_opts["debug"].as<bool>() == true) {
        SPDLOG_INFO("setting debug level");
        spdlog::set_level(spdlog::level::debug);
    }

    if (cli_opts.count("version") && cli_opts["version"].as<bool>() == true) {
        SPDLOG_INFO("QuicR library version: {}", QUICR_VERSION);
        exit(0);
    }

    if (cli_opts.count("pub_namespace") && cli_opts.count("pub_name")) {
        enable_pub = true;
        SPDLOG_INFO("Publisher enabled using track namespace: {0} name: {1}",
                    cli_opts["pub_namespace"].as<std::string>(),
                    cli_opts["pub_name"].as<std::string>());
    }

    if (cli_opts.count("clock") && cli_opts["clock"].as<bool>() == true) {
        SPDLOG_INFO("Running in clock publish mode");
        qclient_vars::publish_clock = true;
    }

    if (cli_opts.count("sub_namespace") && cli_opts.count("sub_name")) {
        enable_sub = true;
        SPDLOG_INFO("Subscriber enabled using track namespace: {0} name: {1}",
                    cli_opts["sub_namespace"].as<std::string>(),
                    cli_opts["sub_name"].as<std::string>());
    }

    config.endpoint_id = cli_opts["endpoint_id"].as<std::string>();
    config.connect_uri = cli_opts["url"].as<std::string>();
    config.transport_config.debug = cli_opts["debug"].as<bool>();

    config.transport_config.use_reset_wait_strategy = false;
    config.transport_config.time_queue_max_duration = 5000;
    config.transport_config.tls_cert_filename = "";
    config.transport_config.tls_key_filename = "";
    config.transport_config.quic_qlog_path = qlog_path;

    return config;
}

int
main(int argc, char* argv[])
{
    int result_code = EXIT_SUCCESS;

    cxxopts::Options options(
        "qclient", std::string("MOQ Example Client using QuicR Version: ") + std::string(QUICR_VERSION));
    options.set_width(75)
      .set_tab_expansion()
      //.allow_unrecognised_options()
      .add_options()
      ("h,help", "Print help")
      ("d,debug", "Enable debugging") // a bool parameter
      ("v,version", "QuicR Version") // a bool parameter
      ("r,url", "Relay URL", cxxopts::value<std::string>()->default_value("moq://localhost:1234"))
      ("e,endpoint_id", "This client endpoint ID", cxxopts::value<std::string>()->default_value("moq-client"))
      ("q,qlog", "Enable qlog using path", cxxopts::value<std::string>());

    options.add_options("Publisher")("pub_namespace", "Track namespace", cxxopts::value<std::string>())(
      "pub_name", "Track name", cxxopts::value<std::string>())(
      "clock", "Publish clock timestamp every second instead of using STDIN chat");

    options.add_options("Subscriber")("sub_namespace", "Track namespace", cxxopts::value<std::string>())(
      "sub_name", "Track name", cxxopts::value<std::string>());

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help({ "", "Publisher", "Subscriber" }) << std::endl;
        return EXIT_SUCCESS;
    }

    // Install a signal handlers to catch operating system signals
    installSignalHandlers();

    // Lock the mutex so that main can then wait on it
    std::unique_lock lock(moq_example::main_mutex);

    bool enable_pub{ false };
    bool enable_sub{ false };
    quicr::ClientConfig config = InitConfig(result, enable_pub, enable_sub);

    try {
        bool stop_threads{ false };
        auto client = std::make_shared<MyClient>(config, stop_threads);

        if (client->Connect() != quicr::Transport::Status::kConnecting) {
            SPDLOG_ERROR("Failed to connect to server due to invalid params, check URI");
            exit(-1);
        }

        std::thread pub_thread, sub_thread;

        if (enable_pub) {
            const auto& pub_track_name = quicr::example::MakeFullTrackName(
              result["pub_namespace"].as<std::string>(), result["pub_name"].as<std::string>(), 1001);

            pub_thread = std::thread(DoPublisher, pub_track_name, client, std::ref(stop_threads));
        }
        if (enable_sub) {
            const auto& sub_track_name = quicr::example::MakeFullTrackName(
              result["sub_namespace"].as<std::string>(), result["sub_name"].as<std::string>(), 2001);

            sub_thread = std::thread(DoSubscriber, sub_track_name, client, std::ref(stop_threads));
        }

        // Wait until told to terminate
        moq_example::cv.wait(lock, [&]() { return moq_example::terminate; });

        stop_threads = true;
        SPDLOG_INFO("Stopping threads...");

        if (pub_thread.joinable()) {
            pub_thread.join();
        }

        if (sub_thread.joinable()) {
            sub_thread.join();
        }

        client->Disconnect();

        SPDLOG_INFO("Client done");
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));

        // Unlock the mutex
        lock.unlock();
    } catch (const std::invalid_argument& e) {
        std::cerr << "Invalid argument: " << e.what() << std::endl;
        result_code = EXIT_FAILURE;
    } catch (const std::exception& e) {
        std::cerr << "Unexpected exception: " << e.what() << std::endl;
        result_code = EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Unexpected exception" << std::endl;
        result_code = EXIT_FAILURE;
    }

    SPDLOG_INFO("Exit");

    return result_code;
}
