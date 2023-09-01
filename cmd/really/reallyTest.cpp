#include <quicr/quicr_client.h>
#include <quicr/quicr_common.h>
#include <transport/transport.h>
#include <cantina/logger.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>

class subDelegate : public quicr::SubscriberDelegate
{
public:
  subDelegate(cantina::LoggerPointer& logger)
    : logger(std::make_shared<cantina::Logger>("SDEL", logger))
  {
  }

  void onSubscribeResponse(
    [[maybe_unused]] const quicr::Namespace& quicr_namespace,
    [[maybe_unused]] const quicr::SubscribeResult& result) override
  {
    logger->info << "onSubscriptionResponse: name: " << quicr_namespace << "/"
                 << int(quicr_namespace.length()) << " status: "
                 << static_cast<unsigned>(result.status)
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
                          [[maybe_unused]] uint16_t expiry_age_ms,
                          [[maybe_unused]] bool use_reliable_transport,
                          [[maybe_unused]] quicr::bytes&& data) override
  {
    std::stringstream log_msg;

    logger->info << "recv object: name: " << quicr_name
                 << " data sz: " << data.size();

    if (data.size())
      logger->info << " data: " << data.data();

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
  void onPublishIntentResponse(
    [[maybe_unused]] const quicr::Namespace& quicr_namespace,
    [[maybe_unused]] const quicr::PublishIntentResult& result) override
  {
  }
};

int
main(int argc, char* argv[])
{
  auto pd = std::make_shared<pubDelegate>();
  if ((argc != 2) && (argc != 3)) {
    std::cerr
      << "Relay address and port set in RELAY_RELAY and REALLY_PORT env "
         "variables."
      << std::endl;
    std::cerr << std::endl;
    std::cerr << "Usage PUB: reallyTest FF0001 pubData" << std::endl;
    std::cerr << "Usage SUB: reallyTest FF0000" << std::endl;
    exit(-1);
  }

  cantina::LoggerPointer logger =
    std::make_shared<cantina::Logger>("reallyTest");

  char* relayName = getenv("REALLY_RELAY");
  if (!relayName) {
    static char defaultRelay[] = "127.0.0.1";
    relayName = defaultRelay;
  }

  int port = 1234;
  char* portVar = getenv("REALLY_PORT");
  if (portVar) {
    port = atoi(portVar);
  }

  auto name = quicr::Name(std::string(argv[1]));

  logger->info << "Name = " << name << std::flush;

  std::vector<uint8_t> data;
  if (argc == 3) {
    data.insert(
      data.end(), (uint8_t*)(argv[2]), ((uint8_t*)(argv[2])) + strlen(argv[2]));
  }

  logger->info << "Connecting to " << relayName << ":" << port << std::flush;

  quicr::RelayInfo relay{ .hostname = relayName,
                          .port = uint16_t(port),
                          .proto = quicr::RelayInfo::Protocol::QUIC};

  qtransport::TransportConfig tcfg{ .tls_cert_filename = NULL,
                                    .tls_key_filename = NULL };
  quicr::QuicRClient client(relay, tcfg, logger);
  if (!client.connect()) {
      logger->Log(cantina::LogLevel::Critical, "Transport connect failed");
      return 0;
  }


  if (data.size() > 0) {
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

    quicr::SubscribeIntent intent = quicr::SubscribeIntent::immediate;
    quicr::bytes empty;
    client.subscribe(
      sd, nspace, intent, "origin_url", false, "auth_token", std::move(empty));

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
