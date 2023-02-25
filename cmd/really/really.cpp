#include <quicr/quicr_common.h>
#include <quicr/quicr_server.h>
#include <quicr/encode.h>
#include <quicr/message_buffer.h>
#include <transport/transport.h>

#include "subscription.h"
#include "testLogger.h"

#include <iostream>
#include <set>
#include <sstream>

class ReallyServer : public quicr::ServerDelegate
{
public:
  ~ReallyServer() = default;
  ReallyServer()
  {
    quicr::RelayInfo relayInfo = { .hostname = "127.0.0.1",
                                   .port = 1234,
                                   .proto = quicr::RelayInfo::Protocol::UDP };

    server = std::make_unique<quicr::QuicRServer>(relayInfo, *this, logger);
  }

  virtual void onPublishIntent(const quicr::Namespace& /*quicr_name */,
                               const std::string& /* origin_url */,
                               bool /* use_reliable_transport */,
                               const std::string& /* auth_token */,
                               quicr::bytes&& /* e2e_token */){};

  virtual void onPublisherObject(
    const qtransport::TransportContextId& context_id,
    const qtransport::MediaStreamId& stream_id,
    [[maybe_unused]] bool use_reliable_transport,
    quicr::messages::PublishDatagram && datagram)
  {
    std::list<Subscriptions::Remote> list = subscribeList.find(datagram.header.name);

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
                             const std::string& /* auth_token */) {


    std::ostringstream log_msg;
    log_msg << "onUnsubscribe: Namespace " << quicr_namespace.to_hex()
            << " subscribe_id: " << subscriber_id;

    logger.log(qtransport::LogLevel::info, log_msg.str());

    server->subscriptionEnded(subscriber_id, quicr_namespace,
                              quicr::SubscribeResult::SubscribeStatus::Ok);

    Subscriptions::Remote remote = { .subscribe_id = subscriber_id };
    subscribeList.remove(quicr_namespace.name(), quicr_namespace.length(), remote);
  }

  virtual void onSubscribe(
    const quicr::Namespace& quicr_namespace,
    const uint64_t& subscriber_id,
    [[maybe_unused]] const qtransport::TransportContextId& context_id,
    [[maybe_unused]] const qtransport::MediaStreamId& stream_id,
    [[maybe_unused]] const quicr::SubscribeIntent subscribe_intent,
    [[maybe_unused]] const std::string& origin_url,
    [[maybe_unused]] bool use_reliable_transport,
    [[maybe_unused]] const std::string& auth_token,
    [[maybe_unused]] quicr::bytes&& data)
  {
    std::ostringstream log_msg;
    log_msg << "onSubscribe: Namespace " << quicr_namespace.to_hex()
            << "/" << int(quicr_namespace.length())
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
  ReallyServer really_server;
  really_server.server->run();

  while (1) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
  }
}
