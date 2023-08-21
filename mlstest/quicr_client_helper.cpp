#include <iostream>
#include <thread>

#include "pub_delegate.h"
#include "quicr_client_helper.h"
#include "sub_delegate.h"
#include "fake_config.h"

QuicrClientHelper::QuicrClientHelper(const std::string& user_in, testLogger& logger_in,
                                     bool is_creator)
  : user(user_in),
    group("1234"),
    logger(logger_in),
    session(setupMLSSession(user_in, group, is_creator))
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
  //if (is_creator) {
  //  session.make_state();
  ///}
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
  auto nspace = quicr::Namespace(name, 80);
  logger.log(qtransport::LogLevel::info,
             "Publish Intent for name: " + name.to_hex() +
               ", namespace: " + nspace.to_hex());
  auto pd = std::make_shared<PubDelegate>();
  client->publishIntent(pd, nspace, {}, {}, {});
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // do publish
  logger.log(qtransport::LogLevel::info, "Publish, name=" + name.to_hex());
  auto kp_data = tls::marshal(user_info_map.at(user).keypackage);
  client->publishNamedObject(name, 0, 10000, false, std::move(kp_data));
}

void QuicrClientHelper::publishData(quicr::Namespace& nspace, bytes&& data) {

  auto pd = std::make_shared<PubDelegate>();
  client->publishIntent(pd, nspace, {}, {}, {});
  std::this_thread::sleep_for(std::chrono::seconds(1));


  std::stringstream log_msg;
  log_msg << "Publish, name= " << nspace.name().to_hex() << ", size=" << data.size();
  logger.log(qtransport::LogLevel::info, log_msg.str());
  client->publishNamedObject(nspace.name(), 0, 10000, false, std::move(data));
}


void QuicrClientHelper::handle(const quicr::Name& name, quicr::bytes&& data){
    auto namspace = quicr::Namespace(name,80);
    namespaceConfig nspace_config;

    SUBSCRIBE_OP_TYPE operation = SUBSCRIBE_OP_TYPE::Invalid;
    for (auto const& [op, nspace] : nspace_config.subscribe_op_map) {
      if(nspace == namspace) {
        operation = op;
        break;
      }
    }

    switch (operation) {
      case SUBSCRIBE_OP_TYPE::KeyPackage: {
        if (!is_user_creator) {
          logger.log(qtransport::LogLevel::info, "Omit Key Package processing if not the creator");
          break;
        }
        logger.log(qtransport::LogLevel::info,
                   "Received KeyPackage from participant.Add to MLS session ");
        auto [welcome, commit] = session->process_key_package(std::move(data));

        logger.log(qtransport::LogLevel::info,"Publishing Welcome Message ");
        auto welcome_name = nspace_config.subscribe_op_map[SUBSCRIBE_OP_TYPE::Welcome];
        publishData(welcome_name, std::move(welcome));

        logger.log(qtransport::LogLevel::info,"Publishing Commit Message");
        auto commit_name = nspace_config.subscribe_op_map[SUBSCRIBE_OP_TYPE::Commit];
        publishData(commit_name, std::move(commit));
        break;
      }

      case SUBSCRIBE_OP_TYPE::Welcome: {
        logger.log(qtransport::LogLevel::info,
                   "Received Welcome message from the creator. Processing it now ");

        if (is_user_creator) {
          //do nothing
          break;
        }
        session = MlsUserSession::create_for_welcome(user_info_map.at(user), std::move(data));

        break;
      }

      case SUBSCRIBE_OP_TYPE::Commit: {
        logger.log(qtransport::LogLevel::info, "Commit message process is not implemented");
        break;
      }
      default: {
        break;
      }
    }

}

bool QuicrClientHelper::isUserCreator() {
  return is_user_creator;
};

MlsUserSession& QuicrClientHelper::getSession() const
{
  if(session == nullptr) {
      throw std::runtime_error("MLS Session is null");
  }
    return *session.get();
}

// private

std::unique_ptr<MlsUserSession> QuicrClientHelper::setupMLSSession(const std::string& user,
                                   const std::string& group, bool is_creator) {

    user_info_map[user] = MlsUserSession::setup_mls_userinfo(user, group, suite);

    if (is_creator) {
      return MlsUserSession::create(user_info_map[user]);
    }
    // the session will be created as part of welcome processing
    return nullptr;
}
