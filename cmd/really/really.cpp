#include <quicr/encode.h>
#include <quicr/message_buffer.h>
#include <quicr/quicr_common.h>
#include <quicr/quicr_server.h>
#include <transport/transport.h>

#include "subscription.h"
#include "testLogger.h"

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
  std::lock_guard<std::mutex> lock(really::main_mutex);

  // If termination is in process, just return
  if (really::terminate)
    return;

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
  if (sigaction(SIGHUP, &sa, NULL) == -1) {
    std::cerr << "Failed to install SIGHUP handler" << std::endl;
  }

  // Catch SIGINT (signal 2)
  if (sigaction(SIGINT, &sa, NULL) == -1) {
    std::cerr << "Failed to install SIGINT handler" << std::endl;
  }

  // Catch SIGQUIT (signal 3)
  if (sigaction(SIGQUIT, &sa, NULL) == -1) {
    std::cerr << "Failed to install SIGQUIT handler" << std::endl;
  }
#endif
}

class ReallyServer : public quicr::ServerDelegate
{
public:
  ReallyServer()
  {
    quicr::RelayInfo relayInfo = { .hostname = "127.0.0.1",
                                   .port = 1234,
                                   .proto = quicr::RelayInfo::Protocol::QUIC };

    qtransport::TransportConfig tcfg {.tls_cert_filename = "./server-cert.pem",
                                      .tls_key_filename = "./server-key.pem" };
    server = std::make_unique<quicr::QuicRServer>(relayInfo,
                                                  tcfg,
                                                  *this,
                                                  logger);
  }
  ~ReallyServer() { server.reset(); };

  virtual void onPublishIntent(const quicr::Namespace& quicr_namespace,
                               const std::string& /* origin_url */,
                               bool /* use_reliable_transport */,
                               const std::string& /* auth_token */,
                               quicr::bytes&& /* e2e_token */)
  {
    // TODO: Authenticate token
    quicr::PublishIntentResult result{ quicr::messages::Response::Ok, {}, {} };
    server->publishIntentResponse(quicr_namespace, result);
  };

  virtual void onPublishIntentEnd(const quicr::Namespace& /* quicr_namespace */,
                                  const std::string& /* auth_token */,
                                  quicr::bytes&& /* e2e_token */)
  {
  }

  virtual void onPublisherObject(
    const qtransport::TransportContextId& context_id,
    const qtransport::StreamId& stream_id,
    [[maybe_unused]] bool use_reliable_transport,
    quicr::messages::PublishDatagram&& datagram)
  {
    std::list<Subscriptions::Remote> list =
      subscribeList.find(datagram.header.name);

    for (auto dest : list) {

      if (dest.context_id == context_id && dest.stream_id == stream_id) {
        // split horizon - drop packets back to the source that originated the
        // published object
        continue;
      }

      server->sendNamedObject(dest.subscribe_id, false, datagram);
    }
  }

  virtual void onUnsubscribe(const quicr::Namespace& quicr_namespace,
                             const uint64_t& subscriber_id,
                             const std::string& /* auth_token */)
  {

    std::ostringstream log_msg;
    log_msg << "onUnsubscribe: Namespace " << quicr_namespace.to_hex()
            << " subscribe_id: " << subscriber_id;

    logger.log(qtransport::LogLevel::info, log_msg.str());

    server->subscriptionEnded(subscriber_id,
                              quicr_namespace,
                              quicr::SubscribeResult::SubscribeStatus::Ok);

    Subscriptions::Remote remote = { .subscribe_id = subscriber_id };
    subscribeList.remove(
      quicr_namespace.name(), quicr_namespace.length(), remote);
  }

  virtual void onSubscribe(
    const quicr::Namespace& quicr_namespace,
    const uint64_t& subscriber_id,
    [[maybe_unused]] const qtransport::TransportContextId& context_id,
    [[maybe_unused]] const qtransport::StreamId& stream_id,
    [[maybe_unused]] const quicr::SubscribeIntent subscribe_intent,
    [[maybe_unused]] const std::string& origin_url,
    [[maybe_unused]] bool use_reliable_transport,
    [[maybe_unused]] const std::string& auth_token,
    [[maybe_unused]] quicr::bytes&& data)
  {
    std::ostringstream log_msg;
    log_msg << "onSubscribe: Namespace " << quicr_namespace.to_hex() << "/"
            << int(quicr_namespace.length())
            << " subscribe_id: " << subscriber_id;

    logger.log(qtransport::LogLevel::info, log_msg.str());

    Subscriptions::Remote remote = { .subscribe_id = subscriber_id };
    subscribeList.add(quicr_namespace.name(), quicr_namespace.length(), remote);

    // respond with response
    auto result = quicr::SubscribeResult{
      quicr::SubscribeResult::SubscribeStatus::Ok, "", {}, {}
    };
    server->subscribeResponse(subscriber_id, quicr_namespace, result);
  }

  std::unique_ptr<quicr::QuicRServer> server;
  std::shared_ptr<qtransport::ITransport> transport;
  std::set<uint64_t> subscribers = {};

private:
  Subscriptions subscribeList;
  testLogger logger;
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
    std::unique_ptr<ReallyServer> really_server =
      std::make_unique<ReallyServer>();
    really_server->server->run();

    // Wait until told to terminate
    really::cv.wait(lock, [&]() { return really::terminate == true; });

    // Unlock the mutex
    lock.unlock();

    // Terminate the server object
    really_server.reset();
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
