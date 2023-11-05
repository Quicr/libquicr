#include "quicr_server_raw_session.h"

#include <quicr/quicr_server.h>

namespace quicr {
/*
 * Start the  QUICR server at the port specified.
 *  @param delegate_in: Callback handlers for QUICR operations
 */
Server::Server(const RelayInfo& relayInfo,
               const qtransport::TransportConfig& tconfig,
               std::shared_ptr<ServerDelegate> delegate_in,
               const cantina::LoggerPointer& logger,
               std::shared_ptr<UriConvertor> converter_in)
{
  uri_convertor = converter_in;

  switch (relayInfo.proto) {
    case RelayInfo::Protocol::UDP:
      [[fallthrough]];
    case RelayInfo::Protocol::QUIC:
      server_session = std::make_unique<ServerRawSession>(
        relayInfo, tconfig, std::move(delegate_in), logger);
      server_session->set_uri_convertor(uri_convertor);
      break;
    default:
      throw ServerException("Unsupported relay protocol");
      break;
  }
}

Server::Server(std::shared_ptr<qtransport::ITransport> transport_in,
               std::shared_ptr<ServerDelegate> delegate_in,
               const cantina::LoggerPointer& logger,
               std::shared_ptr<UriConvertor> convertor_in)
{
  server_session = std::make_unique<ServerRawSession>(
    std::move(transport_in), std::move(delegate_in), logger);
  uri_convertor = convertor_in;
  server_session->set_uri_convertor(uri_convertor);
}

// Transport APIs
bool
Server::is_transport_ready()
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
Server::run()
{
  return server_session->run();
}

void
Server::publishIntentResponse(const quicr::Namespace& quicr_namespace,
                              const PublishIntentResult& result)
{
  server_session->publishIntentResponse(quicr_namespace, result);
}

void
Server::subscribeResponse(const uint64_t& subscriber_id,
                          const quicr::Namespace& quicr_namespace,
                          const SubscribeResult& result)
{
  server_session->subscribeResponse(subscriber_id, quicr_namespace, result);
}

void
Server::subscriptionEnded(const uint64_t& subscriber_id,
                          const quicr::Namespace& quicr_namespace,
                          const SubscribeResult::SubscribeStatus& reason)
{
  server_session->subscriptionEnded(subscriber_id, quicr_namespace, reason);
}

void
Server::sendNamedObject(const uint64_t& subscriber_id,
                        bool use_reliable_transport,
                        uint8_t priority,
                        uint16_t expiry_age_ms,
                        const messages::PublishDatagram& datagram)
{
  server_session->sendNamedObject(
    subscriber_id, use_reliable_transport, priority, expiry_age_ms, datagram);
}

void
Server::sendNamedObject(const uint64_t& subscriber_id,
                  const messages::MoqObject& object) {
  server_session->sendNamedObject(subscriber_id, object);
}

} // namespace quicr
