
#include <quicr/moq_instance.h>

#include <cantina/logger.h>
#include <condition_variable>
#include <csignal>

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
main()
{
    int result_code = EXIT_SUCCESS;

    auto logger = std::make_shared<cantina::Logger>("qserver");
    logger->SetLogLevel("DEBUG");

    // Install a signal handlers to catch operating system signals
    installSignalHandlers();

    // Lock the mutex so that main can then wait on it
    std::unique_lock<std::mutex> lock(moq_example::main_mutex);

    quicr::MoQInstanceServerConfig config;
    config.instance_id = "moq_server";
    config.server_bind_ip = "127.0.0.1";
    config.server_port = 1234;
    config.server_proto = qtransport::TransportProtocol::QUIC;
    config.transport_config.debug = true;
    config.transport_config.tls_cert_filename = "./server-cert.pem";
    config.transport_config.tls_key_filename = "./server-key.pem";
    config.transport_config.use_reset_wait_strategy = false;
    config.transport_config.time_queue_max_duration = 5000;

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
