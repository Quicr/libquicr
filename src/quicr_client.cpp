#include "quicr_client_raw_session.h"

#include <quicr/quicr_client.h>
#include <transport/transport.h>

#include <memory>
#include <utility>

namespace quicr {

///
/// Client
///

Client::Client(const RelayInfo& relay_info,
               const qtransport::TransportConfig& tconfig,
               const cantina::LoggerPointer& logger)
{
  switch (relay_info.proto) {
    case RelayInfo::Protocol::UDP:
      [[fallthrough]];
    case RelayInfo::Protocol::QUIC:
      client_session =
        std::make_unique<ClientRawSession>(relay_info, tconfig, logger);
      break;
    default:
      throw ClientException("Unsupported relay protocol");
      break;
  }
}

Client::Client(std::shared_ptr<qtransport::ITransport> transport_in,
               const cantina::LoggerPointer& logger)
{
  client_session =
    std::make_unique<ClientRawSession>(std::move(transport_in), logger);
}

bool
Client::connect()
{
  return client_session->connect();
}

bool
Client::disconnect()
{
  return client_session->disconnect();
}

bool
Client::publishIntent(std::shared_ptr<PublisherDelegate> pub_delegate,
                      const quicr::Namespace& quicr_namespace,
                      const std::string& origin_url,
                      const std::string& auth_token,
                      bytes&& payload,
                      bool use_reliable_transport)
{
  return client_session->publishIntent(std::move(pub_delegate),
                                       quicr_namespace,
                                       origin_url,
                                       auth_token,
                                       std::move(payload),
                                       use_reliable_transport);
}

void
Client::publishIntentEnd(const quicr::Namespace& quicr_namespace,
                         const std::string& auth_token)
{
  client_session->publishIntentEnd(quicr_namespace, auth_token);
}

void
Client::subscribe(std::shared_ptr<SubscriberDelegate> subscriber_delegate,
                  const quicr::Namespace& quicr_namespace,
                  const SubscribeIntent& intent,
                  const std::string& origin_url,
                  bool use_reliable_transport,
                  const std::string& auth_token,
                  bytes&& e2e_token)
{
  client_session->subscribe(std::move(subscriber_delegate),
                            quicr_namespace,
                            intent,
                            origin_url,
                            use_reliable_transport,
                            auth_token,
                            std::move(e2e_token));
}

void
Client::unsubscribe(const quicr::Namespace& quicr_namespace,
                    const std::string& origin_url,
                    const std::string& auth_token)
{
  client_session->unsubscribe(quicr_namespace, origin_url, auth_token);
}

void
Client::publishNamedObject(const quicr::Name& quicr_name,
                           uint8_t priority,
                           uint16_t expiry_age_ms,
                           bool use_reliable_transport,
                           bytes&& data)
{
  client_session->publishNamedObject(quicr_name,
                                     priority,
                                     expiry_age_ms,
                                     use_reliable_transport,
                                     std::move(data));
}

void
Client::publishNamedObjectFragment(const quicr::Name& quicr_name,
                                   uint8_t priority,
                                   uint16_t expiry_age_ms,
                                   bool use_reliable_transport,
                                   const uint64_t& offset,
                                   bool is_last_fragment,
                                   bytes&& data)
{
  client_session->publishNamedObjectFragment(quicr_name,
                                             priority,
                                             expiry_age_ms,
                                             use_reliable_transport,
                                             offset,
                                             is_last_fragment,
                                             std::move(data));
}

} // namespace quicr
