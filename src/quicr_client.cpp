#include "quicr_client_raw_session.h"

#include <quicr/quicr_client.h>
#include <transport/transport.h>

#include <memory>
#include <utility>

#include "client_raw_sessions/datagram_session.h"
#include "client_raw_sessions/per_group_session.h"
#include "client_raw_sessions/per_object_session.h"

namespace quicr {

std::unique_ptr<QuicRClientSession>
make_client_session(RelayInfo& relay_info,
                    qtransport::TransportConfig tconfig,
                    qtransport::LogHandler& logger,
                    StreamMode stream_mode)
{
  switch (relay_info.proto) {
    case RelayInfo::Protocol::UDP:
      return std::make_unique<ClientRawSession_Datagram>(
        relay_info, tconfig, logger);
    case RelayInfo::Protocol::QUIC:
      switch (stream_mode) {
        case StreamMode::PerGroup:
          return std::make_unique<ClientRawSession_PerGroup>(
            relay_info, tconfig, logger);
        case StreamMode::PerObject:
          return std::make_unique<ClientRawSession_PerObject>(
            relay_info, tconfig, logger);
        case StreamMode::Datagram:
          [[fallthrough]];
        default:
          return std::make_unique<ClientRawSession_Datagram>(
            relay_info, tconfig, logger);
      }
    default:
      throw QuicRClientException("Unsupported relay protocol");
  }
}

QuicRClient::QuicRClient(RelayInfo& relay_info,
                         qtransport::TransportConfig tconfig,
                         qtransport::LogHandler& logger,
                         StreamMode stream_mode)
{
  client_session =
    make_client_session(relay_info, tconfig, logger, stream_mode);
}

QuicRClient::QuicRClient(std::shared_ptr<qtransport::ITransport> transport_in,
                         qtransport::LogHandler& logger)
{
  client_session =
    std::make_unique<QuicRClientRawSession>(transport_in, logger);
}

bool
QuicRClient::connect()
{
  return client_session->connect();
}

bool
QuicRClient::disconnect()
{
  return client_session->disconnect();
}

bool
QuicRClient::publishIntent(std::shared_ptr<PublisherDelegate> pub_delegate,
                           const quicr::Namespace& quicr_namespace,
                           const std::string& origin_url,
                           const std::string& auth_token,
                           bytes&& payload)
{
  return client_session->publishIntent(
    pub_delegate, quicr_namespace, origin_url, auth_token, std::move(payload));
}

void
QuicRClient::publishIntentEnd(const quicr::Namespace& quicr_namespace,
                              const std::string& auth_token)
{
  client_session->publishIntentEnd(quicr_namespace, auth_token);
}

void
QuicRClient::subscribe(std::shared_ptr<SubscriberDelegate> subscriber_delegate,
                       const quicr::Namespace& quicr_namespace,
                       const SubscribeIntent& intent,
                       const std::string& origin_url,
                       bool use_reliable_transport,
                       const std::string& auth_token,
                       bytes&& e2e_token)
{
  client_session->subscribe(subscriber_delegate,
                            quicr_namespace,
                            intent,
                            origin_url,
                            use_reliable_transport,
                            auth_token,
                            std::move(e2e_token));
}

void
QuicRClient::unsubscribe(const quicr::Namespace& quicr_namespace,
                         const std::string& origin_url,
                         const std::string& auth_token)
{
  client_session->unsubscribe(quicr_namespace, origin_url, auth_token);
}

void
QuicRClient::publishNamedObject(const quicr::Name& quicr_name,
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
QuicRClient::publishNamedObjectFragment(const quicr::Name& quicr_name,
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
