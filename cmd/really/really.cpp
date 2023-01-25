#include <quicr/quicr_server.h>
#include <quicr/quicr_common.h>
#include <transport/transport.h>
#include <transport_udp.h>

#include <iostream>
#include <sstream>
#include <iomanip>
#include <set>

static std::string to_hex(const quicr::QUICRNamespace& ns) {
  std::stringstream stream;
  stream << "0x"
         << std::setfill ('0')
         << std::setw(sizeof(uint64_t)*2)
         << std::hex
         << ns.hi
         << std::setw(sizeof(uint64_t)*2)
         << ns.low;
  return stream.str();
}

static std::string to_hex(const quicr::QUICRName& name) {
  std::stringstream stream;
  stream << "0x"
         << std::setfill ('0')
         << std::setw(sizeof(uint64_t)*2)
         << std::hex
         << name.hi
         << std::setw(sizeof(uint64_t)*2)
         << name.low;
  return stream.str();
}

class ReallyServer : public quicr::ServerDelegate {
public:

  ~ReallyServer() = default;
  ReallyServer() {
    quicr::RelayInfo relayInfo = { hostname: "127.0.0.1", port: 1234, proto: quicr::RelayInfo::Protocol::UDP };

    server = std::make_unique<quicr::QuicRServer>(relayInfo, *this);
  }

  virtual void onPublishIntent(const quicr::QUICRNamespace& quicr_name,
                               const std::string& origin_url,
                               bool use_reliable_transport,
                               const std::string& auth_token,
                               quicr::bytes&& e2e_token) {};

  virtual void onPublisherObject(const quicr::QUICRName& quicr_name,
                                 uint8_t priority,
                                 uint16_t expiry_age_ms,
                                 bool use_reliable_transport,
                                 quicr::bytes&& data)  {

    std::cout << " onPublisherObject: Namespace " << to_hex(quicr_name) << std::endl;
    for(const auto& s : subscribers) {
      server->sendNamedObject(quicr_name, 0, 0, false, std::move(data));
    }

  }

  virtual void onPublishedFragment(const quicr::QUICRName& quicr_name,
                                   uint8_t priority,
                                   uint16_t expiry_age_ms,
                                   bool use_reliable_transport,
                                   const uint64_t& offset,
                                   bool is_last_fragment,
                                   quicr::bytes&& data)
  {
  }

  virtual void onSubscribe(const quicr::QUICRNamespace& quicr_namespace,
                           const quicr::SubscribeIntent subscribe_intent,
                           const std::string& origin_url,
                           bool use_reliable_transport,
                           const std::string& auth_token,
                           quicr::bytes&& data)
  {
    std::cout << " onSubscribe: Namespace " << to_hex(quicr_namespace) << std::endl;
    subscribers.insert(quicr_namespace);
    // respond with response
    auto result = quicr::SubscribeResult{quicr::SubscribeResult::SubscribeStatus::Ok, "", {}, {}};
    server->subscribeResponse(quicr_namespace, 0x0, result);
  }

  virtual void on_connection_status(const qtransport::TransportContextId &context_id,
                                    const qtransport::TransportStatus status) {}

  virtual void on_new_connection(const qtransport::TransportContextId &context_id,
                                 const qtransport::TransportRemote &remote) {}

  virtual void on_new_media_stream(const qtransport::TransportContextId &context_id,
                                   const qtransport::MediaStreamId &mStreamId) {}

  virtual void on_recv_notify(const qtransport::TransportContextId &context_id,
                              const qtransport::MediaStreamId &mStreamId) {}

  std::unique_ptr<quicr::QuicRServer> server;
  std::shared_ptr<qtransport::ITransport> transport;
  std::set<quicr::QUICRNamespace> subscribers = {};
};


int main() {
  ReallyServer really_server;
  really_server.server->run();

  while(1) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    //std::cout << "waiting for the data..." << std::endl;
  }
}
