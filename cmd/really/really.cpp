#include <quicr/quicr_common.h>
#include <quicr/quicr_server.h>
#include <transport/transport.h>

#include "subscription.h"
#include <iostream>
#include <set>

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
    const quicr::Name& quicr_name,
    const qtransport::TransportContextId& context_id,
    const qtransport::MediaStreamId& stream_id,
    [[maybe_unused]] uint8_t priority,
    [[maybe_unused]] uint16_t expiry_age_ms,
    [[maybe_unused]] bool use_reliable_transport,
    quicr::bytes&& data)
  {

    std::cout << " onPublisherObject: Name " << quicr_name.to_hex()
              << std::endl;

    std::list<Subscriptions::Remote> list = subscribeList.find(quicr_name);

    for (auto dest : list) {

      if (dest.context_id == context_id && dest.stream_id == stream_id) {
        // split horizon - drop packets back to the source that originated the
        // published object
        continue;
      }

      quicr::bytes copy = data;
      server->sendNamedObject(
        dest.subscribe_id, quicr_name, 0, 0, false, std::move(copy));
    }
  }

  virtual void onPublishedFragment(const quicr::Name& /* quicr_name */,
                                   uint8_t /* priority */,
                                   uint16_t /* expiry_age_ms */,
                                   bool /* use_reliable_transport */,
                                   const uint64_t& /* offset */,
                                   bool /* is_last_fragment */,
                                   quicr::bytes&& /* data */)
  {
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
    std::cout << " onSubscribe: Namespace " << quicr_namespace.to_hex()
              << std::endl;

    Subscriptions::Remote remote = { .subscribe_id = subscriber_id };
    subscribeList.add(quicr_namespace.name(), quicr_namespace.length(), remote);

    // respond with response
    auto result = quicr::SubscribeResult{
      quicr::SubscribeResult::SubscribeStatus::Ok, "", {}, {}
    };
    server->subscribeResponse(quicr_namespace, 0x0, result);
  }

  virtual void on_connection_status(
    const qtransport::TransportContextId& /* context_id */,
    const qtransport::TransportStatus /* status */)
  {
  }

  virtual void on_new_connection(
    const qtransport::TransportContextId& /* context_id */,
    const qtransport::TransportRemote& /* remote */)
  {
  }

  virtual void on_new_media_stream(
    const qtransport::TransportContextId& /* context_id */,
    const qtransport::MediaStreamId& /* mStreamId */)
  {
  }

  virtual void on_recv_notify(
    const qtransport::TransportContextId& /* context_id */,
    const qtransport::MediaStreamId& /* mStreamId */)
  {
  }

  std::unique_ptr<quicr::QuicRServer> server;
  std::shared_ptr<qtransport::ITransport> transport;
  std::set<uint64_t> subscribers = {};

private:
  Subscriptions subscribeList;
  qtransport::LogHandler logger;
};

int
main()
{
  ReallyServer really_server;
  really_server.server->run();

  while (1) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    // std::cout << "waiting for the data..." << std::endl;
  }
}
