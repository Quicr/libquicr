
#include <quicr/moq_instance.h>

#include "signal_handler.h"

#include <iostream>
#include <oss/cxxopts.hpp>

namespace qclient_vars {
    std::optional<qtransport::TransportConnId> conn_id;
}


class trackDelegate : public quicr::MoQTrackDelegate
{
public:
    trackDelegate(const std::string& t_namespace,
                  const std::string& t_name,
                  uint8_t priority,
                  uint32_t ttl,
                  const cantina::LoggerPointer& logger)
      : MoQTrackDelegate({ t_namespace.begin(), t_namespace.end() },
                         { t_name.begin(), t_name.end() },
                         TrackMode::STREAM_PER_GROUP,
                         priority,
                         ttl,
                         logger)
    {
    }

    void cb_objectReceived(uint64_t group_id, uint64_t object_id, std::vector<uint8_t>&& object) override {}
    void cb_sendCongested(bool cleared, uint64_t objects_in_queue) override {}

    void cb_sendReady() override {
        _logger->info << "Track alias: " << _track_alias.value() << " is ready to send" << std::flush;
    }

    void cb_sendNotReady(TrackSendStatus status) override {}
    void cb_readReady() override
    {
        _logger->info << "Track alias: " << _track_alias.value() << " is ready to read" << std::flush;
    }
    void cb_readNotReady(TrackReadStatus status) override {}

    SendError sendObject(const uint64_t  group_id, const uint64_t object_id, const std::span<const uint8_t>& object, uint8_t priority, uint32_t ttl) {
        //TODO: check send status before sending

    }
};

class clientDelegate : public quicr::MoQInstanceDelegate
{
public:
    clientDelegate(const cantina::LoggerPointer& logger) :
        _logger(std::make_shared<cantina::Logger>("MID", logger)) {}

    void cb_newConnection(qtransport::TransportConnId conn_id,
                          const std::span<uint8_t>& endpoint_id,
                          const qtransport::TransportRemote& remote) override {}

    void cb_connectionStatus(qtransport::TransportConnId conn_id,
                             const std::span<uint8_t>& endpoint_id,
                             qtransport::TransportStatus status) override
    {
        auto ep_id = std::string(endpoint_id.begin(), endpoint_id.end());

        if (status == qtransport::TransportStatus::Ready) {
            _logger->info << "Connection ready conn_id: " << conn_id
                          << " endpoint_id: " << ep_id
                          << std::flush;

            qclient_vars::conn_id = conn_id;
        }
    }
    void cb_clientSetup(qtransport::TransportConnId conn_id, quicr::messages::MoqClientSetup client_setup) override {}
    void cb_serverSetup(qtransport::TransportConnId conn_id, quicr::messages::MoqServerSetup server_setup) override {}

private:
    cantina::LoggerPointer _logger;
};

void do_publisher(const std::string t_namespace,
                  const std::string t_name,
                  const std::shared_ptr<quicr::MoQInstance>& moqInstance,
                  const cantina::LoggerPointer& logger,
                  const bool& stop)
{
    cantina::LoggerPointer _logger = std::make_shared<cantina::Logger>("PUB", logger);

    auto mi = moqInstance;

    auto track_delegate = std::make_shared<trackDelegate>(t_namespace, t_name, 2, 3000, logger);

    _logger->info << "Started publisher track: " << t_namespace << "/" << t_name << std::flush;

    bool published_track { false };
    while (not stop) {
        if (!published_track && qclient_vars::conn_id) {
            _logger->info << "Publish track: " << t_namespace << "/" << t_name << std::flush;
            mi->publishTrack(*qclient_vars::conn_id, track_delegate);
            published_track = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        //TODO: Hardcoded publish, remove and make it interactive sends
        constexpr auto NUM_MESSAGES = 10;
        auto data = std::vector<uint8_t> {0, 1, 2, 3, 4, 5};
        for (int i = 0; i < NUM_MESSAGES; i++) {
            track_delegate->sendObject(100, i, data, 3, 500);
        }
    }

    _logger->info << "Publisher done track: " << t_namespace << "/" << t_name << std::flush;
}

void do_subscriber(const std::string t_namespace,
                   const std::string t_name,
                   const std::shared_ptr<quicr::MoQInstance>& moqInstance,
                   const cantina::LoggerPointer& logger,
                   const bool& stop)
{
    cantina::LoggerPointer _logger = std::make_shared<cantina::Logger>("SUB", logger);

    auto mi = moqInstance;

    auto track_delegate = std::make_shared<trackDelegate>(t_namespace, t_name, 2, 3000, logger);

    _logger->info << "Started subscriber track: " << t_namespace << "/" << t_name << std::flush;

    bool subscribe_track { false };
    while (not stop) {
        if (!subscribe_track && qclient_vars::conn_id) {
            _logger->info << "Subscribe track: " << t_namespace << "/" << t_name << std::flush;
            mi->subscribeTrack(*qclient_vars::conn_id, track_delegate);
            subscribe_track = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    _logger->info << "Subscriber done track: " << t_namespace << "/" << t_name << std::flush;
}

quicr::MoQInstanceClientConfig init_config(cxxopts::ParseResult& cli_opts,
                                           bool& enable_pub,
                                           bool& enable_sub,
                                           const cantina::LoggerPointer& logger)
{
    quicr::MoQInstanceClientConfig config;

    std::string qlog_path;
    if (cli_opts.count("qlog")) {
        qlog_path = cli_opts["qlog"].as<std::string>();
    }

    if (cli_opts.count("debug") && cli_opts["debug"].as<bool>() == true) {
        logger->info << "setting debug level" << std::flush;
        logger->SetLogLevel("DEBUG");
    }

    if (cli_opts.count("pub_namespace") && cli_opts.count("pub_name")) {
        enable_pub = true;
        logger->info << "Publisher enabled using track"
                     << " namespace: " << cli_opts["pub_namespace"].as<std::string>()
                     << " name: " << cli_opts["pub_name"].as<std::string>()
                     << std::flush;
    }

    if (cli_opts.count("sub_namespace") && cli_opts.count("sub_name")) {
        enable_sub = true;
        logger->info << "Subscriber enabled using track"
                     << " namespace: " << cli_opts["sub_namespace"].as<std::string>()
                     << " name: " << cli_opts["sub_name"].as<std::string>()
                     << std::flush;
    }

    config.endpoint_id = cli_opts["endpoint_id"].as<std::string>();
    config.server_host_ip = cli_opts["host"].as<std::string>();
    config.server_port = cli_opts["port"].as<uint16_t>();
    config.server_proto = qtransport::TransportProtocol::QUIC;
    config.transport_config.debug = cli_opts["debug"].as<bool>();;
    config.transport_config.use_reset_wait_strategy = false;
    config.transport_config.time_queue_max_duration = 5000;
    config.transport_config.tls_cert_filename = nullptr;
    config.transport_config.tls_key_filename = nullptr;
    config.transport_config.quic_qlog_path = qlog_path.size() ? const_cast<char *>(qlog_path.c_str()) : nullptr;

    return config;
}

int
main(int argc, char* argv[])
{
    int result_code = EXIT_SUCCESS;

    cantina::LoggerPointer logger = std::make_shared<cantina::Logger>("qclient");

    cxxopts::Options options("qclient", "MOQ Example Client");
    options
      .set_width(75)
      .set_tab_expansion()
      //.allow_unrecognised_options()
      .add_options()
      ("h,help", "Print help")
      ("d,debug", "Enable debugging") // a bool parameter
      ("r,host", "Relay host/IP", cxxopts::value<std::string>()->default_value("localhost"))
      ("p,port", "Relay port", cxxopts::value<uint16_t>()->default_value("1234"))
      ("e,endpoint_id", "This client endpoint ID", cxxopts::value<std::string>()->default_value("moq-client"))
      ("q,qlog", "Enable qlog using path", cxxopts::value<std::string>())
    ; // end of options

    options.add_options("Publisher")
      ("pub_namespace", "Track namespace", cxxopts::value<std::string>())
      ("pub_name", "Track name", cxxopts::value<std::string>())
    ;

    options.add_options("Subscriber")
      ("sub_namespace", "Track namespace", cxxopts::value<std::string>())
      ("sub_name", "Track name", cxxopts::value<std::string>())
    ;

    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
        std::cout << options.help({"", "Publisher", "Subscriber"}) << std::endl;
        return true;
    }

    // Install a signal handlers to catch operating system signals
    installSignalHandlers();

    // Lock the mutex so that main can then wait on it
    std::unique_lock lock(moq_example::main_mutex);

    bool enable_pub { false };
    bool enable_sub { false };
    quicr::MoQInstanceClientConfig config = init_config(result, enable_pub, enable_sub, logger);

    auto delegate = std::make_shared<clientDelegate>(logger);

    try {
        //auto moqInstance = quicr::MoQInstance{config, delegate, logger};
        auto moqInstance = std::make_shared<quicr::MoQInstance>(config, delegate, logger);

        moqInstance->run_client();

        bool stop_threads { false };
        std::thread pub_thread, sub_thread;
        if (enable_pub) {
            pub_thread = std::thread (do_publisher,
                                     result["pub_namespace"].as<std::string>(),
                                     result["pub_name"].as<std::string>(),
                                     moqInstance, logger, stop_threads);
        }

        if (enable_sub) {
            sub_thread = std::thread (do_subscriber,
                                      result["sub_namespace"].as<std::string>(),
                                      result["sub_name"].as<std::string>(),
                                      moqInstance, logger, stop_threads);
        }

        // Wait until told to terminate
        moq_example::cv.wait(lock, [&]() { return moq_example::terminate; });

        stop_threads = true;

        if (pub_thread.joinable()) {
            pub_thread.join();
        }

        if (sub_thread.joinable()) {
            sub_thread.join();
        }

        moqInstance->stop();

        logger->info << "Client done" << std::endl;

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

    return result_code;
}
