
#include <quicr/moq_instance.h>

#include <cantina/logger.h>
#include <condition_variable>
#include <csignal>
#include <oss/cxxopts.hpp>


#include "signal_handler.h"

#include "subscription.h"

class serverDelegate : public quicr::MoQInstanceDelegate
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

    auto logger = std::make_shared<cantina::Logger>("qserver");

    cxxopts::Options options("qclient", "MOQ Example Client");
    options
      .set_width(75)
      .set_tab_expansion()
      .allow_unrecognised_options()
      .add_options()
      ("h,help", "Print help")
      ("d,debug", "Enable debugging") // a bool parameter
      ("b,bind_ip", "Bind IP", cxxopts::value<std::string>()->default_value("127.0.0.1"))
      ("p,port", "Listening port", cxxopts::value<uint16_t>()->default_value("1234"))
      ("e,endpoint_id", "This relay/server endpoint ID", cxxopts::value<std::string>()->default_value("moq-server"))
      ("c,cert", "Certificate file", cxxopts::value<std::string>()->default_value("./server-cert.pem"))
      ("k,key", "Certificate key file", cxxopts::value<std::string>()->default_value("./server-key.pem"))
      ("q,qlog", "Enable qlog using path", cxxopts::value<std::string>())
    ; // end of options

    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
        std::cout << options.help({""}) << std::endl;
        return true;
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

    quicr::MoQInstanceServerConfig config;
    config.endpoint_id = result["endpoint_id"].as<std::string>();
    config.server_bind_ip = result["bind_ip"].as<std::string>();
    config.server_port = result["port"].as<uint16_t>();
    config.server_proto = qtransport::TransportProtocol::QUIC;
    config.transport_config.debug = result["debug"].as<bool>();
    config.transport_config.tls_cert_filename = const_cast<char *>(result["cert"].as<std::string>().c_str());
    config.transport_config.tls_key_filename = const_cast<char *>(result["key"].as<std::string>().c_str());
    config.transport_config.use_reset_wait_strategy = false;
    config.transport_config.time_queue_max_duration = 5000;
    config.transport_config.quic_qlog_path = qlog_path.size() ? const_cast<char *>(qlog_path.c_str()) : nullptr;

    auto delegate = std::make_shared<serverDelegate>();

    try {
        auto moqInstance = quicr::MoQInstance{config, delegate, logger};

        moqInstance.run_server();

        // Wait until told to terminate
        moq_example::cv.wait(lock, [&]() { return moq_example::terminate; });

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
