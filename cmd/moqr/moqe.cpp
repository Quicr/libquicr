#include <iostream>

#include "server_delegate.h"
#include "moqe.h"

MoqEndPoint::MoqEndPoint(std::string& relay_in,
                         uint16_t port,
                         std::shared_ptr<NumeroURIConvertor> uri_convertor_in,
                         std::shared_ptr<ServerDelegate> delegate,
                         std::shared_ptr<cantina::Logger> logger_in)
  : uri_convertor(uri_convertor_in)
    , server_delegate(delegate)
    , logger(logger_in)
{
  const auto relay =
    quicr::RelayInfo{ .hostname = relay_in,
                      .port = uint16_t(port),
                      .proto = quicr::RelayInfo::Protocol::QUIC };

  qsession = std::make_unique<QSession>(relay);
}

void MoqEndPoint::run()
{


    if (!data.empty()) {
      auto nspace = quicr::Namespace(name, 96);
      logger->info << "Publish Intent for name: " << name
                   << " == namespace: " << nspace << std::flush;
      client.publishIntent(pd, nspace, {}, {}, {});

      auto sd = std::make_shared<subDelegate>(logger);
      logger->info << "Announcer registering for subscribes " << nspace << std::flush;

      std::this_thread::sleep_for(std::chrono::seconds(200));


      // do publish
      //logger->Log("Publish");
      //client.publishNamedObject(name, 0, 1000, false, std::move(data));

    } else {
      // do subscribe
      logger->Log("Subscribe");
      auto sd = std::make_shared<subDelegate>(logger);
      logger->info << "Subscribe to " << name << std::flush;

      client.subscribe(sd,
                       ns,
                       quicr::SubscribeIntent::immediate,
                       "origin_url",
                       false,
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

}


int main(int argc, char *argv[]) {

    if ((argc != 2) && (argc != 3)) {
      std::cerr
        << "Relay address and port set in REALLY_RELAY and REALLY_PORT env "
           "variables."
        << std::endl;
      std::cerr << std::endl;
      exit(-1); // NOLINT(concurrency-mt-unsafe)
    }

    cantina::LoggerPointer logger = std::make_shared<cantina::Logger>("moq_ep");

    auto uri_templates = std::vector<std::string> {
      "moqt://conference.example.com<pen=100><sub_pen=1>/conferences/<int8>/mediatype/<int4>/endpoint/<int8>/track/<int8>"
    };

    std::shared_ptr<NumeroURIConvertor> uri_convertor =
      std::make_shared<NumeroURIConvertor>();

    uri_convertor->add_uri_templates(uri_templates);

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const auto* relay = getenv("REALLY_RELAY");
    if (relay == nullptr) {
      relay = "127.0.0.1";
    }

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const auto* portVar = getenv("REALLY_PORT");
    int relay_port = 1234;
    if (portVar != nullptr) {
      relay_port = atoi(portVar); // NOLINT(cert-err34-c)
    }

    auto delegate = std::make_shared<ServerDelegate>(logger);

    MoqEndPoint moqe {std::string(relay), relay_port, uri_convertor, delegate};



    moqe.run();
}