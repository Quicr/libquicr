#include <cantina/logger.h>
#include <quicr/quicr_client.h>
#include <quicr/quicr_common.h>
#include <transport/transport.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>

class subDelegate : public quicr::SubscriberDelegate
{
public:
  explicit subDelegate(cantina::LoggerPointer& logger)
    : logger(std::make_shared<cantina::Logger>("SDEL", logger))
  {
  }

  void onSubscribeResponse(
    [[maybe_unused]] const quicr::Namespace& quicr_namespace,
    [[maybe_unused]] const quicr::SubscribeResult& result) override
  {
    logger->info << "onSubscriptionResponse: name: " << quicr_namespace << "/"
                 << int(quicr_namespace.length())
                 << " status: " << static_cast<unsigned>(result.status)
                 << std::flush;
  }

  void onSubscriptionEnded(
    [[maybe_unused]] const quicr::Namespace& quicr_namespace,
    [[maybe_unused]] const quicr::SubscribeResult::SubscribeStatus& reason)
    override
  {
    logger->info << "onSubscriptionEnded: name: " << quicr_namespace << "/"
                 << static_cast<unsigned>(quicr_namespace.length())
                 << std::flush;
  }

  void onSubscribedObject([[maybe_unused]] const quicr::Name& quicr_name,
                          [[maybe_unused]] uint8_t priority,
                          [[maybe_unused]] quicr::bytes&& data) override
  {
    logger->info << "recv object: name: " << quicr_name
                 << " data sz: " << data.size();

    if (!data.empty()) {
      logger->info << " data: " << data.data();
    }

    logger->info << std::flush;
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
  cantina::LoggerPointer logger;
};

class pubDelegate : public quicr::PublisherDelegate
{
public:
  std::atomic<bool> got_intent_response { false };

  explicit pubDelegate(cantina::LoggerPointer& logger)
    : logger(std::make_shared<cantina::Logger>("PDEL", logger))
  {
  }

  void onPublishIntentResponse(
    const quicr::Namespace& quicr_namespace,
    const quicr::PublishIntentResult& result) override
  {
    LOGGER_INFO(logger,
                "Received PublishIntentResponse for "
                  << quicr_namespace << ": "
                  << static_cast<int>(result.status));
    got_intent_response = true;
  }

private:
  cantina::LoggerPointer logger;
};

int do_publisher(cantina::LoggerPointer logger,
                 quicr::Client& client,
                 std::shared_ptr<pubDelegate> pd,
                 quicr::Name name) {

    auto nspace = quicr::Namespace(name, 96);
    logger->info << "Publish Intent for name: " << name
                 << " == namespace: " << nspace << std::flush;

    client.publishIntent(pd, nspace, {}, {}, {}, quicr::TransportMode::ReliablePerGroup, 2);
    logger->info << "Waiting for intent response, up to 2.5 seconds" << std::flush;

    for (int c=0; c < 50; c++) {
      if (pd->got_intent_response) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds (50));
    }

    if (!pd->got_intent_response) {
      logger->info << "Did not receive publish intent, cannot proceed. Exit" << std::flush;
      return -1;
    }

    logger->info << "Received intent response." << std::flush;

    std::cout << "-----------------------------------------------------------------------" << std::endl;
    std::cout << " Type a message and press ENTER to publish. Type the word exit to end program." << std::flush;
    std::cout << "-----------------------------------------------------------------------" << std::endl;

    while (true) {
      std::string msg;
      getline(std::cin, msg);
      if (!msg.compare("exit")) {
        logger->info << "Exit" << std::flush;
        break;
      }

      logger->info << "Publish: " << msg << std::flush;
      std::vector<uint8_t> m_data(msg.begin(), msg.end());

      std::vector<qtransport::MethodTraceItem> trace;
      const auto start_time = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::steady_clock::now());

      trace.push_back({"client:publish", start_time});

      client.publishNamedObject(name++, 0, 1000, std::move(m_data), std::move(trace));
    }

    return 0;
}

int do_subscribe(cantina::LoggerPointer logger,
                 quicr::Client& client,
                 quicr::Name name) {

    auto sd = std::make_shared<subDelegate>(logger);
    auto nspace = quicr::Namespace(name, 96);

    logger->info << "Subscribe to " << name << "/" << 96 << std::flush;

    client.subscribe(sd,
                     nspace,
                     quicr::SubscribeIntent::immediate,
                     quicr::TransportMode::ReliablePerGroup,
                     "origin_url",
                     "auth_token",
                     quicr::bytes{});

    logger->info << "Type exit to end program" << std::flush;
    while (true) {
      std::string msg;
      getline(std::cin, msg);
      if (!msg.compare("exit")) {
        logger->info << "Exit" << std::flush;
        break;
      }
    }


    logger->Log("Now unsubscribing");
    client.unsubscribe(nspace, {}, {});

    logger->Log("Sleeping for 5 seconds before exiting");
    std::this_thread::sleep_for(std::chrono::seconds(5));

    return 0;
}

int
main(int argc, char* argv[])
{
  cantina::LoggerPointer logger =
  std::make_shared<cantina::Logger>("reallyTest");

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
  auto name = quicr::Name(std::string(argv[1]));

  logger->info << "Name = " << name << std::flush;

  auto data = std::vector<uint8_t>{};
  if (argc == 3) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const auto data_str = std::string(argv[2]);
    data.insert(data.end(), data_str.begin(), data_str.end());
  }

  logger->info << "Connecting to " << relayName << ":" << port << std::flush;

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
    logger->Log(cantina::LogLevel::Critical, "Transport connect failed");
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
