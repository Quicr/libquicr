#pragma once

#include <memory>
#include <map>

#include "testLogger.h"
#include <quicr/quicr_client.h>
#include <quicr/quicr_common.h>
#include "mls_user_session.h"
#include "sub_delegate.h"


class QuicrClientHelper : public QuicrMessageProxy
{
public:
  QuicrClientHelper(const std::string& user, testLogger& logger, bool is_creator);
  void subscribe(quicr::Namespace nspace, testLogger& logger);
  void unsubscribe(quicr::Namespace nspace);
  void publishJoin(quicr::Name& name);
  void publishData(quicr::Namespace& nspace, bytes&& data);
  // Subscriber Delagate Operations

  // proxy handlers for quicr messages
  void handle(const quicr::Name& name, quicr::bytes&& data) override;
  MlsUserSession& getSession() const;
  bool isUserCreator();

private:
  // helper to create MLS State and User wrapper.
  std::unique_ptr<MlsUserSession> setupMLSSession(const std::string& user,const std::string& group,  bool is_creator);

  CipherSuite suite{ CipherSuite::ID::P256_AES128GCM_SHA256_P256 };
  quicr::QuicRClient* client;
  bool is_user_creator;
  std::string user;
  std::string group;
  testLogger& logger;
  std::map<quicr::Namespace, std::shared_ptr<SubDelegate>> sub_delegates { };
  std::map<std::string, MLsUserInfo> user_info_map{};
  std::unique_ptr<MlsUserSession> session = nullptr;
};