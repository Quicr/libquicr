#include <quicr/encode.h>
#include <quicr/message_buffer.h>
#include <quicr/quicr_common.h>
#include <quicr/quicr_server.h>
#include <transport/transport.h>

#include "subscription.h"
#include <cantina/logger.h>

#include <condition_variable>
#include <csignal>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>

// Module-level variables used to control program execution
namespace really {
static std::mutex main_mutex;                     // Main's mutex
static bool terminate{ false };                   // Termination flag
static std::condition_variable cv;                // Main thread waits on this
static const char* termination_reason{ nullptr }; // Termination reason
} // namespace really

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
  const auto lock = std::lock_guard<std::mutex>(really::main_mutex);

  // If termination is in process, just return
  if (really::terminate) {
    return;
  }

  // Indicate that the process should terminate
  really::terminate = true;

  // Set the termination reason string
  switch (signal_number) {
    case SIGINT:
      really::termination_reason = "Interrupt signal received";
      break;

#ifndef _WIN32
    case SIGHUP:
      really::termination_reason = "Hangup signal received";
      break;

    case SIGQUIT:
      really::termination_reason = "Quit signal received";
      break;
#endif

    default:
      really::termination_reason = "Unknown signal received";
      break;
  }

  // Notify the main execution thread to terminate
  really::cv.notify_one();
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

class ReallyServerDelegate : public quicr::ServerDelegate
{
public:
  explicit ReallyServerDelegate(const cantina::LoggerPointer& parent_logger)
    : logger{ std::make_shared<cantina::Logger>("SDEL", parent_logger) }
  {
  }

  /**
   * Hacky dependency injection.
   * TODO(trigaux): Remove this once delegate no longer depends on server.
   */
  void setServer(std::shared_ptr<quicr::Server> server_in)
  {
    server = std::move(server_in);
  }

  void onPublishIntent(const quicr::Namespace& quicr_namespace,
                       const std::string& /* origin_url */,
                       const std::string& /* auth_token */,
                       quicr::bytes&& /* e2e_token */) override
  {
    // TODO(trigaux): Authenticate token
    logger->info << "Publish intent namespace: " << quicr_namespace
                 << std::flush;

    // TODO(trigaux): Move logic into quicr::Server
    const auto result =
      quicr::PublishIntentResult{ quicr::messages::Response::Ok, {}, {} };
    server->publishIntentResponse(quicr_namespace, result);
  };

  void onPublishIntentEnd(const quicr::Namespace& /* quicr_namespace */,
                          const std::string& /* auth_token */,
                          quicr::bytes&& /* e2e_token */) override
  {
  }

  void onPublisherObject(const qtransport::TransportConnId& conn_id,
                         [[maybe_unused]] const qtransport::DataContextId& data_ctx_id,
                         quicr::messages::PublishDatagram&& datagram) override
  {
    const auto list = subscribeList.find(datagram.header.name);

    for (auto dest : list) {
      if (dest.conn_id == conn_id) {
        // split horizon - drop packets back to the source that originated the
        // published object
        continue;
      }

      // TODO(trigaux): Move logic into quicr::Server
      server->sendNamedObject(dest.subscribe_id, 1, 200, datagram);
    }
  }

  void onUnsubscribe(const quicr::Namespace& quicr_namespace,
                     const uint64_t& subscriber_id,
                     const std::string& /* auth_token */) override
  {

    logger->info << "onUnsubscribe: Namespace " << quicr_namespace
                 << " subscribe_id: " << subscriber_id << std::flush;

    // TODO(trigaux): Move logic into quicr::Server
    server->subscriptionEnded(subscriber_id,
                              quicr_namespace,
                              quicr::SubscribeResult::SubscribeStatus::Ok);

    const auto remote = Subscriptions::Remote{
      .subscribe_id = subscriber_id,
    };
    subscribeList.remove(
      quicr_namespace.name(), quicr_namespace.length(), remote);
  }

  void onSubscribePause([[maybe_unused]] const quicr::Namespace& quicr_namespace,
                        [[maybe_unused]] const uint64_t subscriber_id,
                        [[maybe_unused]] const qtransport::TransportConnId conn_id,
                        [[maybe_unused]] const qtransport::DataContextId data_ctx_id,
                        [[maybe_unused]] const bool pause) override {}

  void onSubscribe(
    const quicr::Namespace& quicr_namespace,
    const uint64_t& subscriber_id,
    [[maybe_unused]] const qtransport::TransportConnId& conn_id,
    [[maybe_unused]] const qtransport::DataContextId& data_ctx_id,
    [[maybe_unused]] const quicr::SubscribeIntent subscribe_intent,
    [[maybe_unused]] const std::string& origin_url,
    [[maybe_unused]] const std::string& auth_token,
    [[maybe_unused]] quicr::bytes&& data) override
  {
    logger->info << "onSubscribe: Namespace " << quicr_namespace << "/"
                 << static_cast<unsigned>(quicr_namespace.length())
                 << " subscribe_id: " << subscriber_id << std::flush;

    const auto remote = Subscriptions::Remote{
      .subscribe_id = subscriber_id,
    };
    subscribeList.add(quicr_namespace.name(), quicr_namespace.length(), remote);

    // TODO(trigaux): Move logic into quicr::Server
    auto result = quicr::SubscribeResult{
      quicr::SubscribeResult::SubscribeStatus::Ok, "", {}, {}
    };
    server->subscribeResponse(subscriber_id, quicr_namespace, result);
  }

private:
  cantina::LoggerPointer logger;

  // TODO(trigaux): Remove this once all above server logic is moved
  std::shared_ptr<quicr::Server> server;

  std::shared_ptr<qtransport::ITransport> transport;

  std::set<uint64_t> subscribers = {};
  Subscriptions subscribeList;
};

int
main()
{
  int result_code = EXIT_SUCCESS;

  // Install a signal handlers to catch operating system signals
  installSignalHandlers();

  // Lock the mutex so that main can then wait on it
  std::unique_lock<std::mutex> lock(really::main_mutex);

  try {
    const auto relayInfo = quicr::RelayInfo{
      .hostname = "127.0.0.1",
      .port = 1234,
      .proto = quicr::RelayInfo::Protocol::QUIC,
    };

    const auto tcfg = qtransport::TransportConfig{
      .tls_cert_filename = "./server-cert.pem",
      .tls_key_filename = "./server-key.pem",
    };

    auto logger = std::make_shared<cantina::Logger>("really");
    auto delegate = std::make_shared<ReallyServerDelegate>(logger);
    auto server =
      std::make_shared<quicr::Server>(relayInfo, tcfg, delegate, logger);

    // TODO(trigaux): Remove this once delegate no longer depends on server.
    delegate->setServer(server);

    server->run();

    // Wait until told to terminate
    really::cv.wait(lock, [&]() { return really::terminate; });

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
