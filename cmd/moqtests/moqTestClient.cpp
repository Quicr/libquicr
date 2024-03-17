#include <cantina/logger.h>
#include <quicr/quicr_client.h>
#include <quicr/quicr_common.h>
#include <transport/transport.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>
#include <condition_variable>
#include <future>

#include "uri_convertor.h"


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
  explicit pubDelegate(cantina::LoggerPointer& logger, quicr::Client& client_in, std::promise<bool> on_response_in)
    : logger(std::make_shared<cantina::Logger>("PDEL", logger)),
      client(client_in),
      on_response(std::move(on_response_in))
  {
  }

  void onPublishIntentResponse(
    const quicr::Namespace& quicr_namespace,
    const quicr::PublishIntentResult& result) override
  {
    LOGGER_INFO(logger,
                "PubDelegate:Received PublishIntentResponse for "
                  << quicr_namespace << ": "
                  << static_cast<int>(result.status));
  }

    //A publisher gets subscribes once announce is completed
  void onSubscribe(const quicr::Namespace& quicr_namespace,
                   const uint64_t& subscriber_id,
                   const qtransport::TransportConnId& conn_id,
                   const qtransport::DataContextId& data_ctx_id,
                   const quicr::SubscribeIntent subscribe_intent) {
    logger->info << "PubDelegate: Received Subscribe for Namespace " << quicr_namespace << std::flush;
    auto result = quicr::SubscribeResult{
            quicr::SubscribeResult::SubscribeStatus::Ok, "", {}, {}
    };

    client.subscribeResponse(subscriber_id, quicr_namespace, result);

    // reset the promise enabling publish sends
    if (on_response) {
      on_response->set_value(true);
      on_response.reset();
    }
  }



private:
  cantina::LoggerPointer logger;
  quicr::Client& client;
  std::optional<std::promise<bool>> on_response;
};

/*
 * Notes:
 * FullTrackName is 60 bits of quicr namespace
 * TrackNamespace is 52 bits
 * GroupId and ObjectId take 48 bits (
 */
int
main(int argc, char* argv[])
{
  cantina::LoggerPointer logger =
    std::make_shared<cantina::Logger>("moqclient");

  /*
   * Idea
   * TrackName is 32 + 12 + 6 + 10  = 60 bits
   * This would give 4096 meetings with 64 media qualities and 1024 participants in a given meeting.
   * This breakup would be fine for a 1000 person meeting. but If one needs a larger scale one
   * add a different sub-pen for larger meeting with bit split as shown
   *
   * TrackName is 32 + 10 + 6 + 16  = 64 bits that would allow 65000 participants.
   */
  //auto uri_templates = std::vector<std::string> {
  //  "moqt://conference.example.com<pen=100><sub_pen=1>/conferences/<int12>/mediatype/<int6>/endpoint/<int10>",
  //  "moqt://conference.example.com<pen=100><sub_pen=1>/conferences/<int12>/mediatype/<int6>" // track_namespace = 44 bits
  //};

  auto uri_templates_date_server = std::vector<std::string> {
    "moqt://moq.mathis.dev<pen=100><sub_pen=1>/app/<int12>/second/<int6>",
    "moqt://moq.mathis.dev<pen=100><sub_pen=1>/app/<int12>" // track_namespace = 44 bits
  };


  std::shared_ptr<NumeroURIConvertor> uri_convertor = std::make_shared<NumeroURIConvertor>();
  uri_convertor->add_uri_templates(uri_templates_date_server);

  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const auto* relayName = getenv("REALLY_RELAY");
  if (relayName == nullptr) {
    relayName = "127.0.0.1";
  }

  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const auto* portVar = getenv("REALLY_PORT");
  int port = 9999;
  if (portVar != nullptr) {
    port = atoi(portVar); // NOLINT(cert-err34-c)
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  auto uri_in = std::string(argv[1]);
  std::cout << "Input Uri:" << uri_in << std::endl;
  auto ns = uri_convertor->to_quicr_namespace(uri_in);
  std::cout << "Namespace for uri:" << ns << std::endl;
  auto converted_uri = uri_convertor->to_namespace_uri(ns);
  const auto name = ns.name();
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
    .tls_cert_filename = nullptr,
    .tls_key_filename = nullptr,
  };

  quicr::Client client(relay, tcfg, logger, uri_convertor);

  if (!client.connect()) {
    logger->Log(cantina::LogLevel::Critical, "Transport connect failed");
    return 0;
  }

  if (!data.empty()) {

    auto promise = std::promise<bool>();
    auto future = promise.get_future();

    auto pd = std::make_shared<pubDelegate>(logger, client, std::move(promise));
    client.publishIntent(pd, ns, {}, {}, {}, quicr::TransportMode::ReliablePerObject);

    auto sd = std::make_shared<subDelegate>(logger);

    std::this_thread::sleep_for(std::chrono::seconds(1));
    const auto success = future.get();
    if (success) {
      constexpr quicr::Name Group_ID_Mask = ~(~0x0_name << 32) << 16;
      constexpr quicr::Name Object_ID_Mask = ~(~0x0_name << 16);
      uint64_t  group_id = 0xABCD;
      uint64_t object_id = 0;


      auto object_name = (0x0_name | group_id) << 16 | (name & ~Group_ID_Mask);
      object_name &= ~Object_ID_Mask;

      logger->info << "App: Publishing Data now" << std::flush;
      for (int i =0; i < 1000; i++) {
        object_name = (0x0_name | ++object_id) | (object_name & ~Object_ID_Mask);
        auto data = quicr::bytes{1, 2, 3, 4, 5};
        client.publishNamedObject(object_name, 0, 1000, std::move(data));
        std::this_thread::sleep_for(std::chrono::seconds(1));
        object_id++;
      }
    }

    std::this_thread::sleep_for(std::chrono::seconds(100));

  } else {
    // do subscribe
    std::this_thread::sleep_for(std::chrono::seconds(2));
    logger->Log("Subscribe");
    auto sd = std::make_shared<subDelegate>(logger);
    logger->info << "Subscribe to " << name << std::flush;
    auto intent = quicr::SubscribeIntent{.mode = quicr::SubscribeIntent::Mode::immediate};

    client.subscribe(sd,
                     ns,
                     intent,
                     quicr::TransportMode::ReliablePerObject,
                     "origin_url",
                     "auth_token",
                     quicr::bytes{});

    logger->Log("Sleeping for 20 seconds before unsubscribing");
    std::this_thread::sleep_for(std::chrono::seconds(200));

    logger->Log("Now unsubscribing");
    client.unsubscribe(ns, {}, {});

    logger->Log("Sleeping for 15 seconds before exiting");
    std::this_thread::sleep_for(std::chrono::seconds(15));
  }
  std::this_thread::sleep_for(std::chrono::seconds(5));

  return 0;
}
