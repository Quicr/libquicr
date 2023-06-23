
#include <chrono>
#include <cstring>
#include <iostream>
#include <quicr/quicr_client.h>
#include <quicr/quicr_common.h>
#include <sstream>
#include <thread>

#include <hpke/random.h>
#include <mls/common.h>
#include <mls/state.h>
#include <bytes/bytes.h>

#include "testLogger.h"
#include "mls_session.h"

using namespace mls;



/// quicr delegates
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
    log_msg << "onSubscriptionResponse: name: " << quicr_namespace.to_hex()
            << "/" << int(quicr_namespace.length())
            << " status: " << int(static_cast<uint8_t>(result.status));

    logger.log(qtransport::LogLevel::info, log_msg.str());
  }

  void onSubscriptionEnded(
    [[maybe_unused]] const quicr::Namespace& quicr_namespace,
    [[maybe_unused]] const quicr::SubscribeResult::SubscribeStatus& reason)
    override
  {

    std::stringstream log_msg;
    log_msg << "onSubscriptionEnded: name: " << quicr_namespace.to_hex() << "/"
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

    log_msg << "recv object: name: " << quicr_name.to_hex()
            << " data sz: " << data.size();

    if (data.size())
      log_msg << " data: " << data.data();

    logger.log(qtransport::LogLevel::info, log_msg.str());
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

int main(int argc, char* argv[])
{

  auto pd = std::make_shared<pubDelegate>();
  if (argc > 4 || argc < 3) {
    std::cerr
      << "MLS address and port set in MLS_RELAY and MLS_PORT env "
         "variables."
      << std::endl;
    std::cerr << std::endl;
    std::cerr << "Usage quicr_mlstest creator(1)/joiner(0) user_id "<< std::endl;
    std::cerr << "Ex: quicr_mlstest 1 alice" << std::endl;
    exit(-1);
  }

  testLogger logger;

  char* relayName = getenv("MLS_RELAY");
  if (!relayName) {
    static char defaultRelay[] = "127.0.0.1";
    relayName = defaultRelay;
  }

  int port = 1234;
  char* portVar = getenv("MLS_PORT");
  if (portVar) {
    port = atoi(portVar);
  }
  int is_creator = atoi(argv[1]);
  auto user = std::string(argv[2]);
  auto name = quicr::Name(std::string(argv[1]));

  std::stringstream log_msg;

  log_msg << "Name = " << name.to_hex();
  logger.log(qtransport::LogLevel::info, log_msg.str());

  std::vector<uint8_t> data;
  if (argc == 4) {
    data.insert(
      data.end(), (uint8_t*)(argv[3]), ((uint8_t*)(argv[3])) + strlen(argv[3]));
  }

  MlsUserSession session = MlsUserSession(from_ascii("1234"), from_ascii(user));


  log_msg.str("");
  log_msg << "Connecting to " << relayName << ":" << port;
  logger.log(qtransport::LogLevel::info, log_msg.str());

  quicr::RelayInfo relay{ .hostname = relayName,
                          .port = uint16_t(port),
                          .proto = quicr::RelayInfo::Protocol::UDP };

  qtransport::TransportConfig tcfg{ .tls_cert_filename = NULL,
                                    .tls_key_filename = NULL };
  quicr::QuicRClient client(relay, tcfg, logger);

  if (data.size() > 0) {
    auto nspace = quicr::Namespace(name, 96);
    logger.log(qtransport::LogLevel::info, "Publish Intent for name: " + name.to_hex() + " == namespace: " + nspace.to_hex());
    client.publishIntent(pd, nspace, {}, {}, {});
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // do publish
    logger.log(qtransport::LogLevel::info, "Publish");
    //auto keypackage_data = tls::marshal(key_package);
    //client.publishNamedObject(name, 0, 10000, false, std::move(keypackage_data));
    quicr::bytes empty;
    client.publishNamedObject(name, 0, 10000, false, std::move(data));

  } else {
    // do subscribe
    logger.log(qtransport::LogLevel::info, "Subscribe");
    auto sd = std::make_shared<subDelegate>(logger);
    auto nspace = quicr::Namespace(name, 96);

    log_msg.str(std::string());
    log_msg.clear();

    log_msg << "Subscribe to " << name.to_hex() << "/" << 96;
    logger.log(qtransport::LogLevel::info, log_msg.str());

    quicr::SubscribeIntent intent = quicr::SubscribeIntent::immediate;
    quicr::bytes empty;
    client.subscribe(
      sd, nspace, intent, "origin_url", false, "auth_token", std::move(empty));

    logger.log(qtransport::LogLevel::info,
               "Sleeping for 20 seconds before unsubscribing");
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
