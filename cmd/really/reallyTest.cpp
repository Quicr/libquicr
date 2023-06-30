
#include <chrono>
#include <cstring>
#include <iostream>
#include <quicr/quicr_client.h>
#include <quicr/quicr_common.h>
#include <sstream>
#include <thread>
#include <atomic>

#include "testLogger.h"

std::atomic<bool> subscribe_done{false};

class subDelegate : public quicr::SubscriberDelegate
{
public:
  subDelegate(testLogger& logger)
    : logger(logger)
  {
  }

  void onSubscribeResponse(
    [[maybe_unused]] const quicr::Namespace& quicr_namespace,
    [[maybe_unused]] const quicr::SubscribeResult& result) override
  {

    std::stringstream log_msg;
    log_msg << "onSubscriptionResponse: name: " << quicr_namespace << "/"
            << int(quicr_namespace.length())
            << " status: " << int(static_cast<uint8_t>(result.status));

    logger.log(qtransport::LogLevel::info, log_msg.str());
  }

  void onSubscriptionEnded(
    [[maybe_unused]] const quicr::Namespace& quicr_namespace,
    [[maybe_unused]] const quicr::SubscribeResult::SubscribeStatus& reason)
    override
  {

    std::stringstream log_msg;
    log_msg << "onSubscriptionEnded: name: " << quicr_namespace << "/"
            << int(quicr_namespace.length());

    logger.log(qtransport::LogLevel::info, log_msg.str());
  }

  void onSubscribedObject([[maybe_unused]] const quicr::Name& quicr_name,
                          [[maybe_unused]] uint8_t priority,
                          [[maybe_unused]] uint16_t expiry_age_ms,
                          [[maybe_unused]] bool use_reliable_transport,
                          [[maybe_unused]] quicr::bytes&& data) override
  {
    std::stringstream log_msg;

    log_msg << "recv object: name: " << quicr_name
            << " data sz: " << data.size();

    if (data.size())
      log_msg << " data: " << data.data();

    logger.log(qtransport::LogLevel::info, log_msg.str());

    subscribe_done = true;

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
  testLogger& logger;
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
  testLogger logger;
  std::stringstream log_msg;
  log_msg << "Name = " << name;
  logger.log(qtransport::LogLevel::info, log_msg.str());

  std::vector<uint8_t> data;
  if (argc == 3) {
    data.insert(
      data.end(), (uint8_t*)(argv[2]), ((uint8_t*)(argv[2])) + strlen(argv[2]));
  }

  log_msg.str("");
  log_msg << "Connecting to " << relayName << ":" << port;
  logger.log(qtransport::LogLevel::info, log_msg.str());

  quicr::RelayInfo relay{ .hostname = relayName,
                          .port = uint16_t(port),
                          .proto = quicr::RelayInfo::Protocol::UDP };

  qtransport::TransportConfig tcfg{ .tls_cert_filename = NULL,
                                    .tls_key_filename = NULL };
  quicr::QuicRClient client(relay, tcfg, logger);
  auto pub_delegate = std::make_shared<pubDelegate>();
  auto sub_delegate = std::make_shared<subDelegate>(logger);

  if (data.size() > 0) {
    auto nspace = quicr::Namespace(name, 96);
    logger.log(qtransport::LogLevel::info,
               "Publish Intent for name: " + std::string(name) +
                 " == namespace: " + std::string(nspace));
    client.publishIntent(pub_delegate, nspace, {}, {}, {});
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // do publish
    logger.log(qtransport::LogLevel::info, "Publish");
    client.publishNamedObject(name, 0, 10000, false, std::move(data));

  } else {
    // do subscribe
    logger.log(qtransport::LogLevel::info, "Subscribe");
    auto nspace = quicr::Namespace(name, 96);

    log_msg.str(std::string());
    log_msg.clear();

    log_msg << "Subscribe to " << name << "/" << 96;
    logger.log(qtransport::LogLevel::info, log_msg.str());

    quicr::SubscribeIntent intent = quicr::SubscribeIntent::immediate;
    quicr::bytes empty;
    client.subscribe(
      sub_delegate, nspace, intent, "origin_url", false, "auth_token", std::move(empty));

    while(!subscribe_done) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        logger.log(qtransport::LogLevel::info, "waiting for subscribe to be done");
    }

    logger.log(qtransport::LogLevel::info, "Attempting to fetch now");
    client.fetchNamedObject(nspace, name);

    logger.log(qtransport::LogLevel::info,
               "Sleeping for 30 seconds before unsubscribing");
    std::this_thread::sleep_for(std::chrono::seconds(20));

    logger.log(qtransport::LogLevel::info, "Now unsubscribing");
    client.unsubscribe(nspace, {}, {});

    logger.log(qtransport::LogLevel::info,
               "Sleeping for 15 seconds before exiting");
    std::this_thread::sleep_for(std::chrono::seconds(15));
  }
  std::this_thread::sleep_for(std::chrono::seconds(5));

  return 0;
}
