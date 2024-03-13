#include<future>
#include <condition_variable>
#include <future>
#include <map>
#include <mutex>
#include <memory>
#include <set>
#include <thread>

#include "qsession.h"
#include "transport/transport.h"

static const uint16_t default_ttl_ms = 1000;


QSession::QSession(std::shared_ptr<ServerDelegate> server_delegate) {

}

QSession::QSession(quicr::RelayInfo relay) {
  QSession(relay, nullptr);
}


QSession::QSession(quicr::RelayInfo relay, std::shared_ptr<ServerDelegate> server_delegate)
{
  logger = std::make_shared<cantina::Logger>("qsession_logger");
  add_uri_templates();
  logger->Log("Connecting to " + relay.hostname + ":" +
              std::to_string(relay.port));
  qtransport::TransportConfig tcfg{ .tls_cert_filename = NULL,
                                    .tls_key_filename = NULL };
  client = std::make_unique<quicr::Client>(relay, tcfg, logger);

  if(server_delegate) {
    server =
      std::make_shared<quicr::Server>(relay, tcfg, server_delegate, logger, uri_convertor);

  }


}


quicr::Namespace QSession::to_namespace(const std::string& ns_str) {
  return url_encoder.EncodeUrl(ns_str);
}

void QSession::set_app_queue(std::shared_ptr<AsyncQueue<QuicrObject>> q) {
  inbound_objects = q;
}
  

bool 
QSession::connect() {
   if (!client->connect()) {
    return false;
  }
  return true;
}

bool 
QSession::subscribe(quicr::Namespace ns) {
  if (sub_delegates.count(ns)) {
    return true;
  }

  auto promise = std::promise<bool>();
  auto future = promise.get_future();
  const auto delegate =
    std::make_shared<SubDelegate>(logger, inbound_objects, std::move(promise));

  logger->Log("Subscribe to " + std::string(ns));
  quicr::bytes empty{};
  client->subscribe(delegate,
                    ns,
                    quicr::SubscribeIntent::immediate,
                    "bogus_origin_url",
                    false,
                    "bogus_auth_token",
                    std::move(empty));

  const auto success = future.get();
  if (success) {
    sub_delegates.insert_or_assign(ns, delegate);
  }

  return success;

}

bool
QSession::publish_intent(quicr::Namespace ns)
{
  logger->Log("Publish Intent for namespace: " + std::string(ns));
  auto promise = std::promise<bool>();
  auto future = promise.get_future();
  const auto delegate =
    std::make_shared<PubDelegate>(logger, std::move(promise));

  client->publishIntent(delegate, ns, {}, {}, {});
  return future.get();
}

void QSession::unsubscribe(quicr::Namespace nspace){}

void
QSession::publish(const quicr::Name& name, quicr::bytes&& data)
{
  logger->Log("Publish, name=" + std::string(name) +
              " size=" + std::to_string(data.size()));
  client->publishNamedObject(name, 0, default_ttl_ms, false, std::move(data));
}



//
// Private Implementation
//

void QSession::add_uri_templates() {

  // setup namespace encoder templates
  // TODO (this should come config file passed from ui to net chip during boot)
  // TODO harcoding for now
  static std::set<std::string> url_templates {
    "quicr://webex.cisco.com<pen=1>/version/<int8>/appId/<int8>/org/<int12>/channel/<int16>/room/<int16>/endpoint/<int16>",
    "quicr://webex.cisco.com<pen=1>/version/<int8>/appId/<int8>/org/<int12>/channel/<int16>/room/<int16>",
  };
  
  for (const std::string& url_template : url_templates)
  {
    logger->Log("Add URL templates for " + url_template);
    url_encoder.AddTemplate(url_template, true);
  }

}