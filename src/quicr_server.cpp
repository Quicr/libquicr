#include <quicr/quicr_common.h>
#include <quicr/quicr_server.h>

#include "encode.h"
#include "message_buffer.h"

namespace quicr {
/*
 * Start the  QUICR server at the port specified.
 *  @param delegate: Callback handlers for QUICR operations
 */
QuicRServer::QuicRServer(qtransport::ITransport& transport_in,
                         ServerDelegate& delegate)
  : transport(transport_in)
{
  transport_context_id = transport.start();
}

// Transport APIs
bool
QuicRServer::is_transport_ready()
{
  return false;
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
  return false;
}

void
QuicRServer::publishIntentResponse(const QUICRNamespace& quicr_namespace,
                                   const PublishIntentResult& result)
{
  throw std::runtime_error("Unimplemented");
}

void
QuicRServer::subscribeResponse(const QUICRNamespace& quicr_namespace,
                               const uint64_t& transaction_id,
                               const SubscribeResult& result)
{
  messages::SubscribeResponse response;
  response.transaction_id = transaction_id;
  response.quicr_namespace = quicr_namespace;
  response.response = result.status;

  messages::MessageBuffer msg;
  msg << response;
  transport.enqueue(transport_context_id, 0x0, std::move(msg.buffer));
}

void
QuicRServer::subscriptionEnded(const QUICRNamespace& quicr_namespace,
                               const SubscribeResult& result)
{
  throw std::runtime_error("Unimplemented");
}

void
QuicRServer::sendNamedObject(const QUICRName& quicr_name,
                             uint8_t priority,
                             uint64_t best_before,
                             bool use_reliable_transport,
                             bytes&& data)
{
}

void
QuicRServer::sendNamedFragment(const QUICRName& name,
                               uint8_t priority,
                               uint64_t best_before,
                               bool use_reliable_transport,
                               uint64_t offset,
                               bool is_last_fragment,
                               bytes&& data)
{
  throw std::runtime_error("Unimplemented");
}

}