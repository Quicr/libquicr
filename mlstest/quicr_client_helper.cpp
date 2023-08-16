#include <iostream>
#include <thread>

#include "pub_delegate.h"
#include "quicr_client_helper.h"
#include "sub_delegate.h"
#include "fake_config.h"

QuicrClientHelper::QuicrClientHelper(std::string user, testLogger& logger_in, bool is_creator) : logger(logger_in),
  session(MlsUserSession(from_ascii("1234"), from_ascii(user)))
{
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

  std::stringstream log_msg;

  logger.log(qtransport::LogLevel::info, log_msg.str());

  log_msg.str("");
  log_msg << "Connecting to " << relayName << ":" << port;
  logger.log(qtransport::LogLevel::info, log_msg.str());

  quicr::RelayInfo relay{ .hostname = relayName,
                          .port = uint16_t(port),
                          .proto = quicr::RelayInfo::Protocol::UDP };

  qtransport::TransportConfig tcfg{ .tls_cert_filename = NULL,
                                    .tls_key_filename = NULL };
  client = new quicr::QuicRClient{ relay, tcfg, logger };

  is_user_creator = is_creator;
  if (is_creator) {
    session.make_state();
  }
}

void
QuicrClientHelper::subscribe(quicr::Namespace nspace, testLogger& logger)
{
  if(!client) {
    return ;
  }

  if(!sub_delegates.count(nspace)) {
    sub_delegates[nspace] = std::make_shared<SubDelegate>(this, logger);
  }

  logger.log(qtransport::LogLevel::info, "Subscribe");

  std::stringstream log_msg;

  log_msg.str(std::string());
  log_msg.clear();

  log_msg << "Subscribe to " << nspace.to_hex();
  logger.log(qtransport::LogLevel::info, log_msg.str());

  quicr::SubscribeIntent intent = quicr::SubscribeIntent::immediate;
  quicr::bytes empty;
  client->subscribe(
    sub_delegates[nspace], nspace, intent, "origin_url", false, "auth_token", std::move(empty));
}

void
QuicrClientHelper::unsubscribe(quicr::Namespace nspace)
{
  logger.log(qtransport::LogLevel::info, "Now unsubscribing");
  client->unsubscribe(nspace, {}, {});
}

void
QuicrClientHelper::publishJoin(quicr::Name& name)
{
  
  auto nspace = quicr::Namespace(name, 96);
  logger.log(qtransport::LogLevel::info,
             "Publish Intent for name: " + name.to_hex() +
               ", namespace: " + nspace.to_hex());
  auto pd = std::make_shared<PubDelegate>();
  client->publishIntent(pd, nspace, {}, {}, {});
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // do publish
  logger.log(qtransport::LogLevel::info, "Publish, name=" + name.to_hex());
  auto kp_data = tls::marshal(session.get_key_package());
  client->publishNamedObject(name, 0, 10000, false, std::move(kp_data));
}

void QuicrClientHelper::handle(const quicr::Name& name, quicr::bytes&& data){
    auto namspace = quicr::Namespace(name,96);
    namespaceConfig nspace_config;

    switch (nspace_config.subscribe_op_map[namspace]) {
      case SUBSCRIBE_OP_TYPE::KeyPackage: {
        logger.log(qtransport::LogLevel::info, "Received KeyPackage from participant.Add to MLS session ");
        session.process_key_package(std::move(data));
        break;
      }
      default:{
        break;
      }
    }
}

bool QuicrClientHelper::isUserCreator() {
  return is_user_creator;
};