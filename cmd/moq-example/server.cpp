
#include <quicr/moq_instance.h>

#include <cantina/logger.h>
#include <condition_variable>
#include <csignal>

#include "subscription.h"


// Module-level variables used to control program execution
namespace moq_server {
    static std::mutex main_mutex;                     // Main's mutex
    static bool terminate{ false };                   // Termination flag
    static std::condition_variable cv;                // Main thread waits on this
    static const char* termination_reason{ nullptr }; // Termination reason
}

/*
 *  signalHandler
 *
 *  Description:
 *      This function will handle operating system signals related to
 *      termination and then instruct the main thread to terminate.
 *
 *  Parameters:
 *      signal_number [in]
 *          The signal caught.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
signalHandler(int signal_number)
{
    const auto lock = std::lock_guard<std::mutex>(moq_server::main_mutex);

    // If termination is in process, just return
    if (moq_server::terminate) {
        return;
    }

    // Indicate that the process should terminate
    moq_server::terminate = true;

    // Set the termination reason string
    switch (signal_number) {
        case SIGINT:
            moq_server::termination_reason = "Interrupt signal received";
            break;

#ifndef _WIN32
        case SIGHUP:
            moq_server::termination_reason = "Hangup signal received";
            break;

        case SIGQUIT:
            moq_server::termination_reason = "Quit signal received";
            break;
#endif

        default:
            moq_server::termination_reason = "Unknown signal received";
            break;
    }

    // Notify the main execution thread to terminate
    moq_server::cv.notify_one();
}

/*
 *  installSignalHandlers
 *
 *  Description:
 *      This function will install the signal handlers for SIGINT, SIGQUIT,
 *      etc. so that the process can be terminated in a controlled fashion.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
installSignalHandlers()
{
#ifdef _WIN32
    if (signal(SIGINT, signalHandler) == SIG_ERR) {
        std::cerr << "Failed to install SIGINT handler" << std::endl;
    }
#else
    struct sigaction sa = {};

    // Configure the sigaction struct
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    // Catch SIGHUP (signal 1)
    if (sigaction(SIGHUP, &sa, nullptr) == -1) {
        std::cerr << "Failed to install SIGHUP handler" << std::endl;
    }

    // Catch SIGINT (signal 2)
    if (sigaction(SIGINT, &sa, nullptr) == -1) {
        std::cerr << "Failed to install SIGINT handler" << std::endl;
    }

    // Catch SIGQUIT (signal 3)
    if (sigaction(SIGQUIT, &sa, nullptr) == -1) {
        std::cerr << "Failed to install SIGQUIT handler" << std::endl;
    }
#endif
}

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

    // Install a signal handlers to catch operating system signals
    installSignalHandlers();

    // Lock the mutex so that main can then wait on it
    std::unique_lock<std::mutex> lock(moq_server::main_mutex);

    quicr::MoQInstanceServerConfig config;
    config.instance_id = "moq_server";
    config.server_bind_ip = "127.0.0.1";
    config.server_port = 1234;
    config.server_proto = qtransport::TransportProtocol::QUIC;
    config.transport_config.debug = true;
    config.transport_config.tls_cert_filename = "./server-cert.pem";
    config.transport_config.tls_key_filename = "./server-key.pem";

    auto logger = std::make_shared<cantina::Logger>("moq_server");

    auto delegate = std::make_shared<serverDelegate>();

    try {
        auto moqInstance = quicr::MoQInstance{config, delegate, logger};

        moqInstance.run_server();

        // Wait until told to terminate
        moq_server::cv.wait(lock, [&]() { return moq_server::terminate; });

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
