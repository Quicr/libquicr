#include <quicr/quicr_common.h>
#include <quicr/quicr_server.h>

namespace quicr {
/*
 * Start the  QUICR server at the port specified.
 *  @param delegate: Callback handlers for QUICR operations
 */
QuicRServer::QuicRServer(ITransport&, ServerDelegate& delegate) {}

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
}

void
QuicRServer::subscribeResponse(const QUICRNamespace& quicr_namespace,
                               const SubscribeResult& result)
{
}

void
QuicRServer::subscriptionEnded(const QUICRNamespace& quicr_namespace,
                               const SubscribeResult& result)
{
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
}

}