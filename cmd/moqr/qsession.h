#pragma once

#include <memory>
#include <map>

#include <UrlEncoder.h>

#include <quicr/quicr_client.h>
#include <quicr/quicr_server.h>
#include <quicr/quicr_server_delegate.h>
#include <quicr/quicr_client_delegate.h>
#include <quicr/name.h>
#include <cantina/logger.h>

#include "sub_delegate.h"
#include "pub_delegate.h"
#include "server_delegate.h"
#include <quicr/quicr_common.h>
class QSession {

public:
  QSession(quicr::RelayInfo relay_info,
           std::shared_ptr<quicr::UriConvertor> uri_convertor);

  QSession(std::shared_ptr<ServerDelegate> server_delegate,
           std::shared_ptr<quicr::UriConvertor> uri_convertor);

  QSession(quicr::RelayInfo relay_info,
           std::shared_ptr<ServerDelegate> server_delegate,
           std::shared_ptr<quicr::UriConvertor> uri_convertor);

  ~QSession() = default;
  bool connect();
  bool publish_intent(quicr::Namespace ns);
  bool subscribe(quicr::Namespace ns);
  void unsubscribe(quicr::Namespace ns);
  void publish(const quicr::Name& name, quicr::bytes&& data);

  // helpers
  quicr::Namespace to_namespace(const std::string& namespace_str);
  void add_uri_templates();

private:
  std::recursive_mutex self_mutex;
  std::unique_lock<std::recursive_mutex> lock()
  {
    return std::unique_lock{ self_mutex };
  }

  std::shared_ptr<AsyncQueue<QuicrObject>> inbound_objects;
  std::atomic_bool stop = false;
  void set_app_queue(std::shared_ptr<AsyncQueue<QuicrObject>> q);
  static constexpr auto inbound_object_timeout = std::chrono::milliseconds(100);
  cantina::LoggerPointer logger = nullptr;
  std::unique_ptr<quicr::Client> client = nullptr;
  std::unique_ptr<quicr::Server> server = nullptr;
  std::map<quicr::Namespace, std::shared_ptr<SubDelegate>> sub_delegates{};
  UrlEncoder url_encoder;
};
