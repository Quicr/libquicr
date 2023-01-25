#include <quicr/quicr_common.h>
#include <quicr/quicr_server.h>

#include "encode.h"
#include "quicr/message_buffer.h"
#include <thread>

namespace quicr {

/*
 * Start the  QUICR server at the port specified.
 *  @param delegate: Callback handlers for QUICR operations
 */
QuicRServer::QuicRServer(qtransport::ITransport& transport_in,
                         ServerDelegate& delegate_in)
  : transport(transport_in),
    delegate(delegate_in)
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
  running = true;
  dequeue_thread = std::thread(dequeueThreadFunc, this);

  return true;
}

int
QuicRServer::runDequeueLoop()
{
  while(running) {
    auto data = transport.dequeue(transport_context_id, 0x0);
    if (!data.has_value()) {
      std::this_thread::sleep_for(std::chrono::microseconds(2));
      continue;
    }

    uint8_t msg_type = data.value().back();
    messages::MessageBuffer msg_buffer {data.value()};
    if (msg_type == static_cast<uint8_t>(messages::MessageType::Subscribe)) {
      handle_subscribe(std::move(msg_buffer));
    } else if (msg_type == static_cast<uint8_t>(messages::MessageType::Publish)) {
      handle_publish(std::move(msg_buffer));
    }
  }

  dequeue_thread.join();
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
  // start populating message to encode
  messages::PublishDatagram datagram;
  // retrieve the context
  PublishContext context{};

  if (!publish_state.count(quicr_name)) {
    auto msid = transport.createMediaStream(transport_context_id, false);
    context.transport_context_id = transport_context_id;
    context.media_stream_id = msid;
    context.state = PublishContext::State::Pending;
    context.group_id = 0;
    context.object_id = 0;
  } else {
    context = publish_state[quicr_name];
    datagram.header.media_id =
      static_cast<messages::uintVar_t>(context.media_stream_id);
  }

  datagram.header.media_id =
    static_cast<messages::uintVar_t>(context.media_stream_id);
  datagram.header.group_id = static_cast<messages::uintVar_t>(context.group_id);
  datagram.header.object_id =
    static_cast<messages::uintVar_t>(context.object_id);
  datagram.header.flags = 0x0;
  datagram.header.offset_and_fin = static_cast<messages::uintVar_t>(1);
  datagram.media_type =
    messages::MediaType::RealtimeMedia; // TODO this should not be here
  datagram.media_data_length = static_cast<messages::uintVar_t>(data.size());
  datagram.media_data = std::move(data);

  messages::MessageBuffer msg;
  msg << datagram;

  transport.enqueue(
    transport_context_id, context.media_stream_id, std::move(msg.buffer));
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


///
/// Private
///

void
QuicRServer::handle_subscribe(messages::MessageBuffer&& msg) {
  messages::Subscribe subscribe;
  msg >> subscribe;
  delegate.onSubscribe(subscribe.quicr_namespace, subscribe.intent, "", false, "", {} );
}

void
QuicRServer::handle_publish(messages::MessageBuffer&& msg) {
  messages::PublishDatagram datagram;
  msg >> datagram;
  delegate.onPublisherObject(datagram.header.name, 0, 0, false, std::move(datagram.media_data));
}

}