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
    logger->info << "onSubscriptionResponse: name: " << quicr_namespace
                 << " status: " << static_cast<unsigned>(result.status)
                 << std::flush;
  }

  void onSubscriptionEnded(
    [[maybe_unused]] const quicr::Namespace& quicr_namespace,
    [[maybe_unused]] const quicr::SubscribeResult::SubscribeStatus& reason)
    override
  {
    logger->info << "onSubscriptionEnded: name: " << quicr_namespace
                 << std::flush;
  }

  void onSubscribedObject([[maybe_unused]] const quicr::Name& quicr_name,
                          [[maybe_unused]] uint8_t priority,
                          [[maybe_unused]] uint16_t expiry_age_ms,
                          [[maybe_unused]] bool use_reliable_transport,
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
    [[maybe_unused]] uint16_t expiry_age_ms,
    [[maybe_unused]] bool use_reliable_transport,
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
  }

private:
  cantina::LoggerPointer logger;
};

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
    std::cerr << "Usage PUB: reallyTest FF0001 pubData" << std::endl;
    std::cerr << "Usage SUB: reallyTest FF0000" << std::endl;
  }

  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* relayName = getenv("REALLY_RELAY");
  if (!relayName) {
    relayName = "127.0.0.1";
  }

  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* portVar = getenv("REALLY_PORT");
  int port = 1234;
  if (portVar) {
    port = atoi(portVar); // NOLINT(cert-err34-c)
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  const auto name = quicr::Name(std::string(relayName));

  logger->info << "Name = " << name << std::flush;

  std::vector<uint8_t> data;
  if (argc == 3) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const auto data_str = std::string(argv[2]);
    data.insert(data.end(), data_str.begin(), data_str.end());
  }

  logger->info << "Connecting to " << relayName << ":" << port << std::flush;

  quicr::RelayInfo relay{
    .hostname = relayName,
    .port = uint16_t(port),
    .proto = quicr::RelayInfo::Protocol::QUIC,
  };

  qtransport::TransportConfig tcfg{
    .tls_cert_filename = nullptr,
    .tls_key_filename = nullptr,
  };

  quicr::Client client(relay, tcfg, logger);
  auto pd = std::make_shared<pubDelegate>(logger);

  if (!client.connect()) {
    logger->Log(cantina::LogLevel::Critical, "Transport connect failed");
    return 0;
  }

  if (!data.empty()) {
    auto nspace = quicr::Namespace(name, 96);
    logger->info << "Publish Intent for name: " << name
                 << " == namespace: " << nspace << std::flush;
    client.publishIntent(pd, nspace, {}, {}, {});
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // do publish
    logger->Log("Publish");
    client.publishNamedObject(name, 0, 1000, false, std::move(data));

  } else {
    // do subscribe
    logger->Log("Subscribe");
    auto sd = std::make_shared<subDelegate>(logger);
    auto nspace = quicr::Namespace(name, 96);

    logger->info << "Subscribe to " << name << "/" << 96 << std::flush;

    client.subscribe(sd,
                     nspace,
                     quicr::SubscribeIntent::immediate,
                     "origin_url",
                     false,
                     "auth_token",
                     quicr::bytes{});

    logger->Log("Sleeping for 20 seconds before unsubscribing");
    std::this_thread::sleep_for(std::chrono::seconds(20));

    logger->Log("Now unsubscribing");
    client.unsubscribe(nspace, {}, {});

    logger->Log("Sleeping for 15 seconds before exiting");
    std::this_thread::sleep_for(std::chrono::seconds(15));
  }
  std::this_thread::sleep_for(std::chrono::seconds(5));

  return 0;
}
