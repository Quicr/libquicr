#include <quicr/quicr_client.h>
#include <quicr/quicr_common.h>
#include <transport/transport.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>

#define LOGGER_TRACE(logger, ...) if (logger) SPDLOG_LOGGER_TRACE(logger, __VA_ARGS__)
#define LOGGER_DEBUG(logger, ...) if (logger) SPDLOG_LOGGER_DEBUG(logger, __VA_ARGS__)
#define LOGGER_INFO(logger, ...) if (logger) SPDLOG_LOGGER_INFO(logger, __VA_ARGS__)
#define LOGGER_WARN(logger, ...) if (logger) SPDLOG_LOGGER_WARN(logger, __VA_ARGS__)
#define LOGGER_ERROR(logger, ...) if (logger) SPDLOG_LOGGER_ERROR(logger, __VA_ARGS__)
#define LOGGER_CRITICAL(logger, ...) if (logger) SPDLOG_LOGGER_CRITICAL(logger, __VA_ARGS__)

class subDelegate : public quicr::SubscriberDelegate
{
public:
  explicit subDelegate(std::shared_ptr<spdlog::logger> logger)
    : logger(std::move(logger))
  {
  }

  void onSubscribeResponse(
    [[maybe_unused]] const quicr::Namespace& quicr_namespace,
    [[maybe_unused]] const quicr::SubscribeResult& result) override
  {
    LOGGER_INFO(logger, "onSubscriptionResponse: name: {0}/{1} status: ", std::string(quicr_namespace), int(quicr_namespace.length()), static_cast<unsigned>(result.status));
  }

  void onSubscriptionEnded(
    [[maybe_unused]] const quicr::Namespace& quicr_namespace,
    [[maybe_unused]] const quicr::SubscribeResult::SubscribeStatus& reason)
    override
  {
    LOGGER_INFO(logger, "onSubscriptionEnded: name: {0}/{1}", std::string(quicr_namespace), static_cast<unsigned>(quicr_namespace.length()));
  }

  void onSubscribedObject([[maybe_unused]] const quicr::Name& quicr_name,
                          [[maybe_unused]] uint8_t priority,
                          [[maybe_unused]] quicr::bytes&& data) override
  {
    std::ostringstream log_msg;
    log_msg << "recv object: name: " << quicr_name << " data sz: " << data.size();

    if (!data.empty()) {
      log_msg << " data: " << data.data();
    }
    LOGGER_INFO(logger, log_msg.str());
  }

  void onSubscribedObjectFragment(
    [[maybe_unused]] const quicr::Name& quicr_name,
    [[maybe_unused]] uint8_t priority,
    [[maybe_unused]] const uint64_t& offset,
    [[maybe_unused]] bool is_last_fragment,
    [[maybe_unused]] quicr::bytes&& data) override
  {
  }

private:
  std::shared_ptr<spdlog::logger> logger;
};

class pubDelegate : public quicr::PublisherDelegate
{
public:
  std::atomic<bool> got_intent_response { false };

  explicit pubDelegate(std::shared_ptr<spdlog::logger> logger)
    : logger(std::move(logger))
  {
  }

  void onPublishIntentResponse(
    const quicr::Namespace& quicr_namespace,
    const quicr::PublishIntentResult& result) override
  {
    LOGGER_INFO(logger, "Received PublishIntentResponse for {0}: {1}", std::string(quicr_namespace), static_cast<int>(result.status));
    got_intent_response = true;
  }

private:
  std::shared_ptr<spdlog::logger> logger;
};

int do_publisher(std::shared_ptr<spdlog::logger> logger,
                 quicr::Client& client,
                 std::shared_ptr<pubDelegate> pd,
                 quicr::Name name) {

    auto nspace = quicr::Namespace(name, 96);
    LOGGER_INFO(logger, "Publish Intent for name: {0} == namespace: {1}", std::string(name), std::string(nspace));

    client.publishIntent(pd, nspace, {}, {}, {}, quicr::TransportMode::ReliablePerGroup, 2);
    LOGGER_INFO(logger, "Waiting for intent response, up to 2.5 seconds");

    for (int c=0; c < 50; c++) {
      if (pd->got_intent_response) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds (50));
    }

    if (!pd->got_intent_response) {
      LOGGER_ERROR(logger, "Did not receive publish intent, cannot proceed. Exit");
      return -1;
    }

    LOGGER_INFO(logger, "Received intent response.");

    std::cout << "-----------------------------------------------------------------------" << std::endl;
    std::cout << " Type a message and press ENTER to publish. Type the word exit to end program." << std::flush;
    std::cout << "-----------------------------------------------------------------------" << std::endl;

    while (true) {
      std::string msg;
      getline(std::cin, msg);
      if (!msg.compare("exit")) {
        LOGGER_INFO(logger, "Exit");
        break;
      }

      LOGGER_INFO(logger, "Publish: {0}", msg);
      std::vector<uint8_t> m_data(msg.begin(), msg.end());

      std::vector<qtransport::MethodTraceItem> trace;
      const auto start_time = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::steady_clock::now());

      trace.push_back({"client:publish", start_time});

      client.publishNamedObject(name++, 0, 1000, std::move(m_data), std::move(trace));
    }

    return 0;
}

int do_subscribe(std::shared_ptr<spdlog::logger> logger,
                 quicr::Client& client,
                 quicr::Name name) {

    auto sd = std::make_shared<subDelegate>(logger);
    auto nspace = quicr::Namespace(name, 96);

    LOGGER_INFO(logger, "Subscribe to {0}/{1}", std::string(name), 96);

    client.subscribe(sd,
                     nspace,
                     quicr::SubscribeIntent::immediate,
                     quicr::TransportMode::ReliablePerGroup,
                     "origin_url",
                     "auth_token",
                     quicr::bytes{});

    LOGGER_INFO(logger, "Type exit to end program");
    while (true) {
      std::string msg;
      getline(std::cin, msg);
      if (!msg.compare("exit")) {
        LOGGER_INFO(logger, "Exit");
        break;
      }
    }


    LOGGER_INFO(logger, "Now unsubscribing");
    client.unsubscribe(nspace, {}, {});

    LOGGER_INFO(logger, "Sleeping for 5 seconds before exiting");
    std::this_thread::sleep_for(std::chrono::seconds(5));

    return 0;
}

int
main(int argc, char* argv[])
{
  auto logger = spdlog::stderr_color_mt("reallyTest");

  if ((argc != 2) && (argc != 3)) {
    std::cerr
      << "Relay address and port set in REALLY_RELAY and REALLY_PORT env "
         "variables."
      << std::endl;
    std::cerr << std::endl;
    std::cerr << "Usage PUB: reallyTest FF0001 pub" << std::endl;
    std::cerr << "Usage SUB: reallyTest FF0000" << std::endl;
    exit(-1); // NOLINT(concurrency-mt-unsafe)
  }

  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const auto* relayName = getenv("REALLY_RELAY");
  if (relayName == nullptr) {
    relayName = "127.0.0.1";
  }

  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const auto* portVar = getenv("REALLY_PORT");
  int port = 1234;
  if (portVar != nullptr) {
    port = atoi(portVar); // NOLINT(cert-err34-c)
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  auto name = quicr::Name(argv[1]);

  LOGGER_INFO(logger, "Name = {0}", std::string(name));

  auto data = std::vector<uint8_t>{};
  if (argc == 3) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const auto data_str = std::string(argv[2]);
    data.insert(data.end(), data_str.begin(), data_str.end());
  }

  LOGGER_INFO(logger, "Connecting to {0}: {1}", relayName, port);

  const auto relay =
    quicr::RelayInfo{ .hostname = relayName,
                      .port = uint16_t(port),
                      .proto = quicr::RelayInfo::Protocol::QUIC };

  const auto tcfg = qtransport::TransportConfig{
    .tls_cert_filename = "",
    .tls_key_filename = "",
  };

  quicr::Client client(relay, "a@cisco.com", 0, tcfg, logger);
  auto pd = std::make_shared<pubDelegate>(logger);

  if (!client.connect()) {
    LOGGER_CRITICAL(logger, "Transport connect failed");
    return 0;
  }

  if (!data.empty()) {
    if (do_publisher(logger, client, pd, name))
      return -1;

  } else {
    if (do_subscribe(logger, client, name))
      return -1;
  }

  return 0;
}
