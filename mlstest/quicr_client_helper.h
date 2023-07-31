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
  QuicrClientHelper(std::string user, testLogger& logger, bool is_creator);
  void subscribe(quicr::Namespace nspace, testLogger& logger);
  void unsubscribe(quicr::Namespace nspace);
  void publishJoin(quicr::Name& name);

  // Subscriber Delagate Operations

  // proxy handlers for quicr messages
  void handle(const quicr::Name& name, quicr::bytes&& data) override;
  MlsUserSession& getSession();
private:
  std::map<quicr::Namespace, std::shared_ptr<SubDelegate>> sub_delegates { };
  quicr::QuicRClient* client;
  testLogger& logger;
  MlsUserSession session;


};