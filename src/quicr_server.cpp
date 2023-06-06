#include "quicr_server_raw_session.h"
#include <quicr/quicr_server.h>

#include <quicr/quicr_server.h>

namespace quicr {
/*
 * Start the  QUICR server at the port specified.
 *  @param delegate_in: Callback handlers for QUICR operations
 */
QuicRServer::QuicRServer(RelayInfo& relayInfo,
                         qtransport::TransportConfig tconfig,
                         ServerDelegate& delegate_in,
                         qtransport::LogHandler& logger)
{
  switch (relayInfo.proto) {
    case RelayInfo::Protocol::UDP:
      [[fallthrough]];
    case RelayInfo::Protocol::QUIC:
      server_session = std::make_unique<QuicRServerRawSession>(
        relayInfo, tconfig, delegate_in, logger);
      break;
    default:
      throw QuicRServerException("Unsupported relay protocol");
      break;
  }
}

QuicRServer::QuicRServer(std::shared_ptr<qtransport::ITransport> transport_in,
                         ServerDelegate& delegate_in,
                         qtransport::LogHandler& logger)
{
  server_session =
    std::make_unique<QuicRServerRawSession>(transport_in, delegate_in, logger);
}

// Transport APIs
bool
QuicRServer::is_transport_ready()
{
  return server_session->is_transport_ready();
}

/**
 * @brief Run Server API event loop
 *
 * @details This method will open listening sockets and run an event loop
 *    for callbacks.
 *
 * @returns true if error, false if no error
 */
bool
QuicRServer::run()
{
  return server_session->run();
}

void
QuicRServer::publishIntentResponse(
  const quicr::Namespace& quicr_namespace,
  const qtransport::TransportContextId& context_id,
  const PublishIntentResult& result)
{
  server_session->publishIntentResponse(quicr_namespace, context_id, result);
}

void
QuicRServer::subscribeResponse(const uint64_t& subscriber_id,
                               const quicr::Namespace& quicr_namespace,
                               const SubscribeResult& result)
{
  server_session->subscribeResponse(subscriber_id, quicr_namespace, result);
}

void
QuicRServer::subscriptionEnded(const uint64_t& subscriber_id,
                               const quicr::Namespace& quicr_namespace,
                               const SubscribeResult::SubscribeStatus& reason)
{
  server_session->subscriptionEnded(subscriber_id, quicr_namespace, reason);
}

void
QuicRServer::sendNamedObject(const uint64_t& subscriber_id,
                             bool use_reliable_transport,
                             uint8_t priority,
                             uint16_t expiry_age_ms,
                             bool new_stream,
                             const messages::PublishDatagram& datagram)
{
  server_session->sendNamedObject(subscriber_id,
                                  priority,
                                  expiry_age_ms,
                                  use_reliable_transport,
                                  new_stream,
                                  datagram);
}

} /* namespace end */
