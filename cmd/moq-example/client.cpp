
#include <quicr/moq_instance.h>

#include "signal_handler.h"

#include <iostream>
#include <oss/cxxopts.hpp>


class clientDelegate : public quicr::MoQInstanceDelegate
{
    void cb_newConnection(qtransport::TransportConnId conn_id,
                          const std::span<uint8_t>& endpoint_id,
                          const qtransport::TransportRemote& remote) override {}
    void cb_connectionStatus(qtransport::TransportConnId conn_id, qtransport::TransportStatus status) override {}
    void cb_clientSetup(qtransport::TransportConnId conn_id, quicr::messages::MoqClientSetup client_setup) override {}
    void cb_serverSetup(qtransport::TransportConnId conn_id, quicr::messages::MoqServerSetup server_setup) override {}
};

int
main(int argc, char* argv[])
{
    int result_code = EXIT_SUCCESS;

    cantina::LoggerPointer logger = std::make_shared<cantina::Logger>("qclient");

    cxxopts::Options options("qclient", "MOQ Example Client");
    options
      .set_width(75)
      .set_tab_expansion()
      .allow_unrecognised_options()
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

    bool enable_pub { false };
    if (result.count("pub_namespace") && result.count("pub_name")) {
        enable_pub = true;
        logger->info << "Publisher enabled using track"
                     << " namespace: " << result["pub_namespace"].as<std::string>()
                     << " name: " << result["pub_name"].as<std::string>()
                     << std::flush;
    }

    bool enable_sub { false };
    if (result.count("sub_namespace") && result.count("sub_name")) {
        enable_pub = true;
        logger->info << "Subscriber enabled using track"
                     << " namespace: " << result["sub_namespace"].as<std::string>()
                     << " name: " << result["sub_name"].as<std::string>()
                     << std::flush;
    }

    std::string qlog_path;
    if (result.count("qlog")) {
        qlog_path = result["qlog"].as<std::string>();
    }

    if (result.count("debug") && result["debug"].as<bool>() == true) {
        logger->info << "setting debug level" << std::flush;
        logger->SetLogLevel("DEBUG");
    }

    // Install a signal handlers to catch operating system signals
    installSignalHandlers();

    // Lock the mutex so that main can then wait on it
    std::unique_lock<std::mutex> lock(moq_example::main_mutex);

    quicr::MoQInstanceClientConfig config;
    config.endpoint_id = result["endpoint_id"].as<std::string>();
    config.server_host_ip = result["host"].as<std::string>();
    config.server_port = result["port"].as<uint16_t>();
    config.server_proto = qtransport::TransportProtocol::QUIC;
    config.transport_config.debug = result["debug"].as<bool>();;
    config.transport_config.use_reset_wait_strategy = false;
    config.transport_config.time_queue_max_duration = 5000;
    config.transport_config.tls_cert_filename = nullptr;
    config.transport_config.tls_key_filename = nullptr;
    config.transport_config.quic_qlog_path = qlog_path.size() ? const_cast<char *>(qlog_path.c_str()) : nullptr;

    auto delegate = std::make_shared<clientDelegate>();

    try {
        auto moqInstance = quicr::MoQInstance{config, delegate, logger};

        moqInstance.run_client();

        // Wait until told to terminate
        moq_example::cv.wait(lock, [&]() { return moq_example::terminate; });

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
