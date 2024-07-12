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
               const std::string& endpoint_id,
               size_t chunk_size,
               const qtransport::TransportConfig& tconfig,
               const cantina::LoggerPointer& logger,
               std::optional<MeasurementsConfig> metrics_config)
{
  switch (relay_info.proto) {
    case RelayInfo::Protocol::UDP:
      [[fallthrough]];
    case RelayInfo::Protocol::QUIC:
      client_session =
        std::make_unique<ClientRawSession>(relay_info, endpoint_id, chunk_size, tconfig, logger, metrics_config);
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
Client::connected() const
{
  return client_session->connected();
}

bool
Client::publishIntent(std::shared_ptr<PublisherDelegate> pub_delegate,
                      const quicr::Namespace& quicr_namespace,
                      const std::string& origin_url,
                      const std::string& auth_token,
                      bytes&& payload,
                      const TransportMode transportMode,
                      uint8_t priority)
{
  return client_session->publishIntent(std::move(pub_delegate),
                                       quicr_namespace,
                                       origin_url,
                                       auth_token,
                                       std::move(payload),
                                       transportMode,
                                       priority);
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
                  const TransportMode transport_mode,
                  const std::string& origin_url,
                  const std::string& auth_token,
                  bytes&& e2e_token,
                  const uint8_t priority)
{
  client_session->subscribe(std::move(subscriber_delegate),
                            quicr_namespace,
                            intent,
                            transport_mode,
                            origin_url,
                            auth_token,
                            std::move(e2e_token),
                            priority);
}

void
Client::unsubscribe(const quicr::Namespace& quicr_namespace,
                    const std::string& origin_url,
                    const std::string& auth_token)
{
  client_session->unsubscribe(quicr_namespace, origin_url, auth_token);
}

SubscriptionState Client::getSubscriptionState(const quicr::Namespace &quicr_namespace) {
    return client_session->getSubscriptionState(quicr_namespace);
}

void
Client::publishNamedObject(const quicr::Name& quicr_name,
                           uint8_t priority,
                           uint16_t expiry_age_ms,
                           bytes&& data,
                           std::vector<qtransport::MethodTraceItem> &&trace)
{
  client_session->publishNamedObject(quicr_name,
                                     priority,
                                     expiry_age_ms,
                                     std::move(data), std::move(trace));
}

void
Client::publishNamedObjectFragment(const quicr::Name& quicr_name,
                                   uint8_t priority,
                                   uint16_t expiry_age_ms,
                                   const uint64_t& offset,
                                   bool is_last_fragment,
                                   bytes&& data)
{
  client_session->publishNamedObjectFragment(quicr_name,
                                             priority,
                                             expiry_age_ms,
                                             offset,
                                             is_last_fragment,
                                             std::move(data));
}

void
Client::publishMeasurement(const Measurement& measurement)
{
  client_session->publishMeasurement(measurement);
}
} // namespace quicr
