#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#include <quicr/encode.h>
#include <quicr/message_buffer.h>
#include <quicr/quicr_client.h>
#include <quicr/quicr_common.h>

namespace quicr {

/*
 * Nested map to reassemble message fragments
 *
 *    Structure:
 *       fragments[<circular index>] = map[quicr_name] = map[offset] = data
 *
 *    Circular index is a small int value that increments from 1 to max. It
 *    wraps to 1 after reaching max size.  In this sense, it's a circular
 * buffer. Upon moving to a new index the new index data will be purged (if any
 * exists).
 *
 *    Fragment reassembly avoids timers and time interval based checks. It
 *    instead is based on received data. Every message quicr_name is checked to
 *    see if it's complete. If so, the published object callback will be
 * executed. If not, it'll only update the map with the new offset value.
 * Incomplete messages can exist in the cache for as long as the circular index
 * hasn't wrapped to the same point in cache.  Under high load/volume, this can
 * wrap within a minute or two.  Under very little load, this could linger for
 * hours. This is okay considering the only harm is a little extra memory being
 * used. Extra memory is a trade-off for being event/message driven instead of
 * timer based with threading/locking/...
 */
static std::map<int, std::map<quicr::Name, std::map<int, bytes>>> fragments;

void
SubscriberDelegate::onSubscribedObject(const quicr::Name& /* quicr_name */,
                                       uint8_t /* priority */,
                                       uint16_t /* expiry_age_ms*/,
                                       bool /* use_reliable_transport */,
                                       bytes&& /* data */)
{
}

void
SubscriberDelegate::onSubscribedObjectFragment(
  const quicr::Name& /* quicr_name */,
  uint8_t /* priority */,
  uint16_t /* expiry_age_ms*/,
  bool /* use_reliable_transport */,
  const uint64_t& /* offset */,
  bool /* is_last_fragment */,
  bytes&& /* data */)
{
}

///
/// Transport Delegate Implementation
///
class QuicRTransportDelegate : public ITransport::TransportDelegate
{
public:
  QuicRTransportDelegate(QuicRClient& client_in)
    : client(client_in)
  {
  }

  virtual ~QuicRTransportDelegate() = default;

  virtual void on_connection_status(
    const qtransport::TransportContextId& context_id,
    const qtransport::TransportStatus status)
  {
    std::stringstream log_msg;
    log_msg << "connection_status: cid: " << context_id
            << " status: " << int(status);
    client.log_handler.log(qtransport::LogLevel::debug, log_msg.str());

    if (status == qtransport::TransportStatus::Disconnected) {
      log_msg.str("");
      log_msg << "Removing state for context_id: " << context_id;
      client.log_handler.log(qtransport::LogLevel::info, log_msg.str());

      client.removeSubscribeState(true, {},
                                  SubscribeResult::SubscribeStatus::ConnectionClosed);

      // TODO: Need to reconnect or inform app that client is no longer connected
    }

  }

  virtual void on_new_connection(
    const qtransport::TransportContextId& /* context_id */,
    const qtransport::TransportRemote& /* remote */)
  {
  }

  virtual void on_new_stream(
    const qtransport::TransportContextId& /* context_id */,
    const qtransport::StreamId& /* mStreamId */)
  {
  }

  virtual void on_recv_notify(const qtransport::TransportContextId& context_id,
                              const qtransport::StreamId& streamId)
  {
    for (int i=0; i < 60; i++) {
      auto data = client.transport->dequeue(context_id, streamId);

      if (!data.has_value()) {
          return;
      }

//      std::cout << "on_recv_notify: context_id: " << context_id
//                << " stream_id: " << streamId
//                << " data sz: " << data.value().size() << std::endl;

      messages::MessageBuffer msg_buffer{data.value()};

      try {
        client.handle(std::move(msg_buffer));
      } catch (const messages::MessageBuffer::ReadException &e) {
        client.log_handler.log(qtransport::LogLevel::info,
                               "Dropping malformed message: " +
                               std::string(e.what()));
          return;
      } catch (const std::exception & /* ex */) {
        client.log_handler.log(qtransport::LogLevel::info,
                               "Dropping malformed message");
          return;
      } catch (...) {
          client.log_handler.log(
                  qtransport::LogLevel::fatal,
                  "Received malformed message with unknown fatal error");
          throw;
      }
    }
  }

private:
  QuicRClient& client;
};

void
QuicRClient::make_transport(RelayInfo& relay_info,
                            qtransport::TransportConfig tconfig,
                            qtransport::LogHandler& logger)
{
  qtransport::TransportRemote server = {
    .host_or_ip = relay_info.hostname,
    .port = relay_info.port,
    .proto = relay_info.proto == RelayInfo::Protocol::UDP
               ? qtransport::TransportProtocol::UDP
               : qtransport::TransportProtocol::QUIC,
  };
  transport_delegate = std::make_unique<QuicRTransportDelegate>(*this);


  transport = qtransport::ITransport::make_client_transport(
    server, std::move(tconfig), *transport_delegate, logger);

  transport_context_id = transport->start();

  while (transport->status() == qtransport::TransportStatus::Connecting) {
    std::ostringstream log_msg;
    log_msg << "Waiting for client to be ready, got: " << int(transport->status());
    logger.log(qtransport::LogLevel::info, log_msg.str());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  if (transport->status() != qtransport::TransportStatus::Ready) {
    std::ostringstream log_msg;
    log_msg << "Transport failed to connect to server, got: "
            << int(transport->status());
    logger.log(qtransport::LogLevel::fatal, log_msg.str());
    throw std::runtime_error(log_msg.str());
  }


  transport_stream_id = transport->createStream(transport_context_id, false);
}

///
/// QuicRClient
///

QuicRClient::QuicRClient(RelayInfo& relay_info,
                         qtransport::TransportConfig tconfig,
                         qtransport::LogHandler& logger)
  : log_handler(logger)
{
  log_handler.log(qtransport::LogLevel::info, "Initialize QuicRClient");

  if (relay_info.proto == RelayInfo::Protocol::UDP) {
    // For plain UDP, pacing is needed. Wtih QUIC it's not needed
    need_pacing = true;
  }

  make_transport(relay_info, std::move(tconfig), logger);
}

QuicRClient::QuicRClient(std::shared_ptr<ITransport> transport_in)
  : log_handler(def_log_handler)
{
  transport = transport_in;
}

QuicRClient::~QuicRClient()
{
  removeSubscribeState(true, {},
                      SubscribeResult::SubscribeStatus::ConnectionClosed);
  transport.reset(); // wait for transport close
}

bool
QuicRClient::publishIntent(std::shared_ptr<PublisherDelegate> pub_delegate,
                           const quicr::Namespace& quicr_namespace,
                           const std::string& /* origin_url */,
                           const std::string& /* auth_token */,
                           bytes&& payload)
{
  if (!pub_delegates.count(quicr_namespace)) {
    pub_delegates[quicr_namespace] = pub_delegate;
  }

  messages::PublishIntent intent{ messages::MessageType::PublishIntent,
                                  messages::create_transaction_id(),
                                  quicr_namespace,
                                  std::move(payload),
                                  transport_stream_id,
                                  1 };

  messages::MessageBuffer msg{ sizeof(messages::PublishIntent) +
                               payload.size() };
  msg << intent;

  auto error =
    transport->enqueue(transport_context_id, transport_stream_id, msg.get());

  return error == qtransport::TransportError::None;
}

void
QuicRClient::publishIntentEnd(const quicr::Namespace& quicr_namespace,
                              [[maybe_unused]] const std::string& auth_token)
{
  if (!pub_delegates.count(quicr_namespace)) {
    return;
  }
  pub_delegates.erase(quicr_namespace);

  // TODO: Authenticate token.

  messages::PublishIntentEnd intent_end{
    messages::MessageType::PublishIntentEnd,
    quicr_namespace,
    {} // TODO: Figure out payload.
  };

  messages::MessageBuffer msg;
  msg << intent_end;

  transport->enqueue(transport_context_id, transport_stream_id, msg.get());
}

void
QuicRClient::subscribe(std::shared_ptr<SubscriberDelegate> subscriber_delegate,
                       const quicr::Namespace& quicr_namespace,
                       const SubscribeIntent& intent,
                       [[maybe_unused]] const std::string& origin_url,
                       [[maybe_unused]] bool use_reliable_transport,
                       [[maybe_unused]] const std::string& auth_token,
                       [[maybe_unused]] bytes&& e2e_token)
{

  std::lock_guard<std::mutex> lock(mutex);

  if (!sub_delegates.count(quicr_namespace)) {
    sub_delegates[quicr_namespace] = subscriber_delegate;
  }

  // encode subscribe
  messages::MessageBuffer msg{};
  auto transaction_id = messages::create_transaction_id();
  messages::Subscribe subscribe{ 0x1, transaction_id, quicr_namespace, intent };
  msg << subscribe;

  // qtransport::MediaStreamId msid{};
  if (!subscribe_state.count(quicr_namespace)) {
    // create a new media-stream for this subscribe
    // msid = transport->createMediaStream(transport_context_id, false);
    subscribe_state[quicr_namespace] =
      SubscribeContext{ SubscribeContext::State::Pending,
                        transport_context_id,
                        transport_stream_id,
                        transaction_id };
    transport->enqueue(transport_context_id, transport_stream_id, msg.get());
    return;
  } else {
    auto& ctx = subscribe_state[quicr_namespace];
    if (ctx.state == SubscribeContext::State::Ready) {
      // already subscribed
      return;
    } else if (ctx.state == SubscribeContext::State::Pending) {
      // todo - resend or wait or may be take in timeout in the api
    }
    transport->enqueue(transport_context_id, transport_stream_id, msg.get());
  }
}

void QuicRClient::removeSubscribeState(bool all, const quicr::Namespace& quicr_namespace,
                                       const SubscribeResult::SubscribeStatus& reason)
{

  std::unique_lock<std::mutex> lock(mutex);

  if (all) { // Remove all states
    std::vector<quicr::Namespace> namespaces_to_remove;
    for (const auto& ns: subscribe_state) {
      namespaces_to_remove.push_back(ns.first);
    }

    lock.unlock(); // Unlock before calling self

    for (const auto& ns: namespaces_to_remove) {
      removeSubscribeState(false, ns, reason);
    }
  }
  else {

    if (subscribe_state.count(quicr_namespace)) {
      subscribe_state.erase(quicr_namespace);
    }

    if (sub_delegates.count(quicr_namespace)) {
      if (auto sub_delegate = sub_delegates[quicr_namespace].lock())
        sub_delegate->onSubscriptionEnded(quicr_namespace,
                                          reason);

      // clean up the delegate memory
      sub_delegates.erase(quicr_namespace);
    }
  }
}

void
QuicRClient::unsubscribe(const quicr::Namespace& quicr_namespace,
                         const std::string& /* origin_url */,
                         const std::string& /* auth_token */)
{
  // The removal of the delegate is done on receive of subscription ended
  std::lock_guard<std::mutex> lock(mutex);

  messages::MessageBuffer msg{};
  messages::Unsubscribe unsub{ 0x1, quicr_namespace };
  msg << unsub;

  if (subscribe_state.count(quicr_namespace)) {
    subscribe_state.erase(quicr_namespace);
  }

  transport->enqueue(transport_context_id, transport_stream_id, msg.get());
}



void
QuicRClient::publishNamedObject(const quicr::Name& quicr_name,
                                [[maybe_unused]] uint8_t priority,
                                [[maybe_unused]] uint16_t expiry_age_ms,
                                [[maybe_unused]] bool use_reliable_transport,
                                bytes&& data)
{
  // start populating message to encode
  messages::PublishDatagram datagram;
  // retrieve the context
  PublishContext context{};

  if (!publish_state.count(quicr_name)) {
    context.transport_context_id = transport_context_id;
    context.transport_stream_id = transport_stream_id;
    context.state = PublishContext::State::Pending;
    context.group_id = 0;
    context.object_id = 0;
  } else {
    // TODO: Never hit this since context is not added to published state and
    // objects are not to be repeated
    context = publish_state[quicr_name];
    datagram.header.media_id = static_cast<uintVar_t>(context.transport_stream_id);
  }

  datagram.header.name = quicr_name;
  datagram.header.media_id = static_cast<uintVar_t>(context.transport_stream_id);
  datagram.header.group_id = static_cast<uintVar_t>(context.group_id);
  datagram.header.object_id = static_cast<uintVar_t>(context.object_id);
  datagram.header.flags = 0x0;
  datagram.header.offset_and_fin = static_cast<uintVar_t>(1);
  datagram.media_type = messages::MediaType::RealtimeMedia;

  // Fragment the payload if needed
  if (data.size() <= quicr::MAX_TRANSPORT_DATA_SIZE) {
    messages::MessageBuffer msg;

    datagram.media_data_length = static_cast<uintVar_t>(data.size());
    datagram.media_data = std::move(data);

    msg << datagram;

    // No fragmenting needed
    // Block on queue full
    while ((transport->enqueue(
                       transport_context_id,
                        context.transport_stream_id,
                                msg.get()
            ) == qtransport::TransportError::QueueFull)) {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

  } else {
    // Fragments required. At this point this only counts whole blocks
    int frag_num = data.size() / quicr::MAX_TRANSPORT_DATA_SIZE;
    int frag_remaining_bytes = data.size() % quicr::MAX_TRANSPORT_DATA_SIZE;

    int offset = 0;

    while (frag_num-- > 0) {
      messages::MessageBuffer msg;

      if (frag_num == 0 && !frag_remaining_bytes) {
        datagram.header.offset_and_fin = (offset << 1) + 1;
      } else {
        datagram.header.offset_and_fin = offset << 1;
      }

      bytes frag_data(data.begin() + offset,
                      data.begin() + offset + quicr::MAX_TRANSPORT_DATA_SIZE);

      datagram.media_data_length = frag_data.size();
      datagram.media_data = std::move(frag_data);

      msg << datagram;

      offset += quicr::MAX_TRANSPORT_DATA_SIZE;

      /*
       * For UDP based transports, some level of pacing is required to prevent
       * buffer overruns throughput the network path and with the remote end.
       *  TODO: Fix... This is set a bit high because the server code is running
       * too slow
       */
      if (need_pacing && (frag_num % 30) == 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

      while ((transport->enqueue(
                         transport_context_id,
                          context.transport_stream_id,
                            msg.get())
              ) == qtransport::TransportError::QueueFull) {
          std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
    }

    // Send last fragment, which will be less than MAX_TRANSPORT_DATA_SIZE
    if (frag_remaining_bytes) {
      messages::MessageBuffer msg;
      datagram.header.offset_and_fin = uintVar_t((offset << 1) + 1);

      bytes frag_data(data.begin() + offset, data.end());
      datagram.media_data_length = static_cast<uintVar_t>(frag_data.size());
      datagram.media_data = std::move(frag_data);

      msg << datagram;

      //			std::cout << "Pub-frag remaining msg size: " <<
      // data.size()
      //			          << " offset: " <<
      // uint64_t(datagram.header.offset_and_fin) << std::endl;

      while ((transport->enqueue(
                        transport_context_id,
                          context.transport_stream_id,
                            msg.get())
              ) == qtransport::TransportError::QueueFull) {
          std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
    }
  }
}

void
QuicRClient::publishNamedObjectFragment(const quicr::Name& /* quicr_name */,
                                        uint8_t /* priority */,
                                        uint16_t /* expiry_age_ms */,
                                        bool /* use_reliable_transport */,
                                        const uint64_t& /* offset */,
                                        bool /* is_last_fragment */,
                                        bytes&& /* data */)
{
  throw std::runtime_error("UnImplemented");
}

bool
QuicRClient::notify_pub_fragment(const messages::PublishDatagram& datagram,
                                 const std::map<int, bytes>& frag_map)
{
  if ((frag_map.rbegin()->first & 0x1) != 0x1) {
    return false; // Not complete, return false that this can NOT be deleted
  }

  bytes reassembled;

  int seq_bytes = 0;
  for (const auto& item : frag_map) {
    if ((item.first >> 1) - seq_bytes != 0) {
      // Gap in offsets, missing data, return false that this can NOT be deleted
      return false;
    }

    reassembled.insert(
      reassembled.end(), item.second.begin(), item.second.end());
    seq_bytes += item.second.size();
  }

  for (const auto& entry : sub_delegates) {
    quicr::bytes copy = reassembled;

    if (entry.first.contains(datagram.header.name)) {
      if (auto sub_delegate = sub_delegates[entry.first].lock())
        sub_delegate->onSubscribedObject(
          datagram.header.name, 0x0, 0x0, false, std::move(copy));
    }
  }

  return true;
}

void
QuicRClient::handle_pub_fragment(messages::PublishDatagram&& datagram)
{
  static unsigned int cindex = 1;

  // Check the current index first considering it's likely in the current buffer
  const auto& msg_iter = fragments[cindex].find(datagram.header.name);
  if (msg_iter != fragments[cindex].end()) {
    // Found
    msg_iter->second.emplace(datagram.header.offset_and_fin,
                             std::move(datagram.media_data));
    if (notify_pub_fragment(datagram, msg_iter->second))
      fragments[cindex].erase(msg_iter);

  } else {
    // Not in current buffer, search all buffers
    bool found = false;
    for (auto& buf : fragments) {
      const auto& msg_iter = buf.second.find(datagram.header.name);
      if (msg_iter != buf.second.end()) {
        // Found
        msg_iter->second.emplace(datagram.header.offset_and_fin,
                                 std::move(datagram.media_data));
        if (notify_pub_fragment(datagram, msg_iter->second)) {
          fragments[cindex].erase(msg_iter);
        }
        found = true;
        break;
      }
    }

    if (!found) {
      // If not found in any buffer, then add to current buffer
      fragments[cindex][datagram.header.name].emplace(
        datagram.header.offset_and_fin, std::move(datagram.media_data));
    }
  }

  // Move to next buffer if reached max
  if (fragments[cindex].size() >= MAX_FRAGMENT_NAMES_PENDING_PER_BUFFER) {
    if (cindex < MAX_FRAGMENT_BUFFERS)
      ++cindex;
    else
      cindex = 1;

    fragments.erase(cindex);
  }
}

void
QuicRClient::handle(messages::MessageBuffer&& msg)
{
  if (msg.empty()) {
    std::cout << "Transport Reported Empty Data" << std::endl;
    return;
  }

  auto msg_type = static_cast<messages::MessageType>(msg.front());
  switch (msg_type) {
    case messages::MessageType::SubscribeResponse: {
      messages::SubscribeResponse response;
      msg >> response;

      SubscribeResult result{ .status = response.response };

      if (sub_delegates.count(response.quicr_namespace)) {
        if (auto sub_delegate = sub_delegates[response.quicr_namespace].lock())
          sub_delegate->onSubscribeResponse(response.quicr_namespace, result);
      } else {
        std::cout << "Got SubscribeResponse: No delegate found for namespace"
                  << response.quicr_namespace.to_hex() << std::endl;
      }

      break;
    }

    case messages::MessageType::SubscribeEnd: {
      messages::SubscribeEnd subEnd;
      msg >> subEnd;

      removeSubscribeState(false, subEnd.quicr_namespace, subEnd.reason);

      break;
    }

    case messages::MessageType::Publish: {
      messages::PublishDatagram datagram;
      msg >> datagram;

      if (datagram.header.offset_and_fin == uintVar_t(0x1)) {
        // No-fragment, process as single object

        for (const auto& entry : sub_delegates) {
          if (entry.first.contains(datagram.header.name)) {
            if (auto sub_delegate = sub_delegates[entry.first].lock())
              sub_delegate->onSubscribedObject(datagram.header.name,
                                               0x0,
                                               0x0,
                                               false,
                                               std::move(datagram.media_data));
          }
        }
      } else { // is a fragment
        handle_pub_fragment(std::move(datagram));
      }

      break;
    }

    case messages::MessageType::PublishIntentResponse: {
      messages::PublishIntentResponse response;
      msg >> response;

      if (!pub_delegates.count(response.quicr_namespace)) {
        std::cout
          << "Got PublishIntentResponse: No delegate found for namespace "
          << response.quicr_namespace << std::endl;
        return;
      }

      if (auto delegate = pub_delegates[response.quicr_namespace].lock()) {
        PublishIntentResult result{ .status = response.response };
        delegate->onPublishIntentResponse(response.quicr_namespace, result);
      }

      break;
    }

    default:
      break;
  }
}
} // namespace quicr