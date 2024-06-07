
#include <quicr/moq_instance.h>

#include "signal_handler.h"

#include <chrono>
#include <iostream>
#include <thread>

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
    logger->SetLogLevel("DEBUG");

    if ((argc != 2) && (argc != 3)) {
        std::cerr << "Relay address and port set in MOQ_RELAY and MOQ_PORT env variables." << std::endl;
        std::cerr << std::endl;
        std::cerr << "Usage publisher: qclient pub <track_namespace> <track_name>" << std::endl;
        std::cerr << "Usage subscriber: qclient <track_namespace> <track_name>" << std::endl;
        exit(-1); // NOLINT(concurrency-mt-unsafe)
    }

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const auto* relayName = getenv("MOQ_RELAY");
    if (relayName == nullptr) {
        relayName = "127.0.0.1";
    }

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const auto* portVar = getenv("MOQ_PORT");
    int port = 1234;
    if (portVar != nullptr) {
        port = atoi(portVar); // NOLINT(cert-err34-c)
    }

    // Install a signal handlers to catch operating system signals
    installSignalHandlers();

    // Lock the mutex so that main can then wait on it
    std::unique_lock<std::mutex> lock(moq_example::main_mutex);

    quicr::MoQInstanceClientConfig config;
    config.instance_id = "moq_client";
    config.server_host_ip = "127.0.0.1";
    config.server_port = 1234;
    config.server_proto = qtransport::TransportProtocol::QUIC;
    config.transport_config.debug = true;
    config.transport_config.use_reset_wait_strategy = false;
    config.transport_config.time_queue_max_duration = 5000;

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
