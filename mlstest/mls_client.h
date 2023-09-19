#pragma once

#include "mls_session.h"
#include "namespace_config.h"
#include "sub_delegate.h"

#include <cantina/logger.h>
#include <quicr/quicr_client.h>
#include <quicr/quicr_common.h>

#include <map>
#include <memory>
#include <future>

class MLSClient
{
public:
  struct Config
  {
    uint64_t group_id;
    uint32_t user_id;
    cantina::LoggerPointer logger;
    quicr::RelayInfo relay;
  };

  MLSClient(const Config& config);

  // Connect to the server and make subscriptions
  bool connect(bool as_creator);

  // MLS operations
  bool join();

  // Access internal state
  bool joined() const;
  const MLSSession& session() const;

private:
  mls::CipherSuite suite{ mls::CipherSuite::ID::P256_AES128GCM_SHA256_P256 };

  cantina::LoggerPointer logger;

  uint64_t group_id;
  uint32_t user_id;
  NamespaceConfig namespaces;

  struct PendingJoin {
    MLSInitInfo init_info;
    std::promise<bool> joined;
  };

  std::optional<std::promise<bool>> join_promise;
  std::variant<MLSInitInfo, MLSSession> mls_session;

  std::unique_ptr<quicr::QuicRClient> client;
  std::map<quicr::Namespace, std::shared_ptr<SubDelegate>> sub_delegates{};

  bool subscribe(quicr::Namespace nspace);
  bool publish_intent(quicr::Namespace nspace);
  void publish(const quicr::Name& name, bytes&& data);
  void handle(const quicr::Name& name, quicr::bytes&& data);

  friend class SubDelegate;
};
