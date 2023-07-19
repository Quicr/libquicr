#pragma once

#include <memory>
#include <map>

#include "testLogger.h"
#include <quicr/quicr_client.h>
#include <quicr/quicr_common.h>
#include "mls_user_session.h"
#include "sub_delegate.h"


class QuicrClientHelper
{
public:
  QuicrClientHelper(std::string user, testLogger& logger);
  void subscribe(quicr::Namespace nspace, testLogger& logger);
  void unsubscribe(quicr::Namespace nspace);
  void publishJoin(quicr::Name& name, MlsUserSession& session);

private:
  std::map<quicr::Namespace, std::shared_ptr<SubDelegate>> sub_delegates { };
  quicr::QuicRClient* client;
  testLogger& logger;

};