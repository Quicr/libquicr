/*
 *  quicr_client_raw_session.cpp
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 *
 *  Description:
 *      This file implements a session layer between the client APIs and the
 *      transport that uses raw data packets (either UDP or QUIC).
 *
 *  Portability Issues:
 *      None.
 */

#include "quicr_client_raw_session.h"

#include "quicr/encode.h"
#include "quicr/gap_check.h"
#include "quicr/message_buffer.h"
#include "quicr/quicr_client.h"
#include "quicr/quicr_client_delegate.h"
#include "quicr/quicr_common.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace quicr {

namespace {

qtransport::TransportRemote
to_TransportRemote(const RelayInfo& info) noexcept
{
  return {
    .host_or_ip = info.hostname,
    .port = info.port,
    .proto = info.proto == RelayInfo::Protocol::UDP
               ? qtransport::TransportProtocol::UDP
               : qtransport::TransportProtocol::QUIC,
  };
}

} // namespace

/*===========================================================================*/
// ClientRawSession
/*===========================================================================*/

ClientRawSession::ClientRawSession(const RelayInfo& relay_info,
                                   const qtransport::TransportConfig& tconfig,
                                   const cantina::LoggerPointer& logger)
  : logger(std::make_shared<cantina::Logger>("QSES", logger))
{
  this->logger->Log("Initialize Client");

  if (relay_info.proto == RelayInfo::Protocol::UDP) {
    // For plain UDP, pacing is needed. For QUIC it's not needed.
    need_pacing = true;
  }

  const auto server = to_TransportRemote(relay_info);
  transport = qtransport::ITransport::make_client_transport(
    server, tconfig, *this, this->logger);
}

ClientRawSession::ClientRawSession(
  std::shared_ptr<qtransport::ITransport> transport_in,
  const cantina::LoggerPointer& logger)
  : has_shared_transport{ true }
  , logger(std::make_shared<cantina::Logger>("QSES", logger))
  , transport(std::move(transport_in))
{
}

ClientRawSession::~ClientRawSession()
{
  if (!transport)
    return;

  if (!has_shared_transport &&
      transport->status() != qtransport::TransportStatus::Disconnected) {
    // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall)
    disconnect();
  }
}

bool
ClientRawSession::connect()
{
  if (!transport) {
    throw std::runtime_error(
      "Transport has been destroyed, create a new session object!");
  }

  const auto context_id = transport->start();

  LOGGER_INFO(logger, "Connecting session " << context_id << "...");

  while (!stopping &&
         transport->status() == qtransport::TransportStatus::Connecting) {
    LOGGER_DEBUG(logger, "Connecting... " << int(stopping.load()));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  if (stopping || !transport) {
    LOGGER_INFO(logger, "Cancelling connecting session " << context_id);
    return false;
  }

  if (!connected()) {
    std::ostringstream msg;
    msg << "Session " << context_id
        << " failed to connect to server, transport status: "
        << int(transport->status());
    logger->Log(cantina::LogLevel::Critical, msg.str());

    throw std::runtime_error(msg.str());
  }

  transport_context_id = context_id;
  transport_dgram_stream_id = transport->createStream(context_id, false);

  return true;
}

bool
ClientRawSession::disconnect()
{
  // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall)
  if (stopping || !(connected() || connecting())) {
    // Not connected or already stopping/stopped
    return true;
  }

  const auto& context_id =
    transport_context_id ? transport_context_id.value() : 0;
  LOGGER_DEBUG(logger, "Disconnecting session " << context_id << "...");

  stopping = true;
  try {
    transport->close(context_id);
  } catch (const std::exception& e) {
    LOGGER_ERROR(
      logger, "Error disconnecting session " << context_id << ": " << e.what());
    return false;
  } catch (...) {
    LOGGER_ERROR(logger, "Unknown error disconnecting session " << context_id);
    return false;
  }

  LOGGER_INFO(logger, "Disconnected session " << context_id << "!");

  transport_dgram_stream_id = std::nullopt;
  transport_context_id = std::nullopt;

  return true;
}

bool
ClientRawSession::connected() const
{
  return transport && transport->status() == qtransport::TransportStatus::Ready;
}

bool
ClientRawSession::connecting() const
{
  return transport && transport->status() == qtransport::TransportStatus::Connecting;
}

/*===========================================================================*/
// Transport Delegate Events
/*===========================================================================*/

void
ClientRawSession::on_connection_status(
  const qtransport::TransportContextId& context_id,
  const qtransport::TransportStatus status)
{
  {
    LOGGER_DEBUG(logger,
                 "connection_status: cid: " << context_id
                                            << " status: " << int(status));
  }

  switch (status) {
    case qtransport::TransportStatus::Connecting:
      [[fallthrough]];
    case qtransport::TransportStatus::Ready:
      stopping = false;
      break;
    case qtransport::TransportStatus::Disconnected:
      LOGGER_INFO(logger,
                  "Received disconnect from transport for context: "
                    << context_id);
      [[fallthrough]];
    case qtransport::TransportStatus::Shutdown:
      [[fallthrough]];
    case qtransport::TransportStatus::RemoteRequestClose:
      LOGGER_INFO(logger, "Shutting down context: " << context_id);
      stopping = true;
      break;
  }
}

void
ClientRawSession::on_new_connection(
  const qtransport::TransportContextId& /* context_id */,
  const qtransport::TransportRemote& /* remote */)
{
}

void
ClientRawSession::on_new_stream(
  const qtransport::TransportContextId& /* context_id */,
  const qtransport::StreamId& /* mStreamId */)
{
}

void
ClientRawSession::on_recv_notify(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& streamId)
{
  if (!transport) {
    return;
  }

  for (int i = 0; i < 150; i++) {
    auto data = transport->dequeue(context_id, streamId);

    if (!data.has_value()) {
      return;
    }

    //      std::cout << "on_recv_notify: context_id: " << context_id
    //                << " stream_id: " << streamId
    //                << " data sz: " << data.value().size() << std::endl;

    messages::MessageBuffer msg_buffer{ data.value() };

    try {
      handle(std::move(msg_buffer));
    } catch (const std::exception& e) {
      LOGGER_DEBUG(logger, "Dropping malformed message: " << e.what());
      return;
    } catch (...) {
      LOGGER_CRITICAL(logger,
                      "Received malformed message with unknown fatal error");
      throw;
    }
  }
}

/*===========================================================================*/
// QuicrClientSession API Methods
/*===========================================================================*/

bool
ClientRawSession::publishIntent(std::shared_ptr<PublisherDelegate> pub_delegate,
                                const quicr::Namespace& quicr_namespace,
                                const std::string& /* origin_url */,
                                const std::string& /* auth_token */,
                                bytes&& payload,
                                bool use_reliable_transport,
                                uint8_t priority)
{
  if (pub_delegates.contains(quicr_namespace)) {
    return true;
  }

  pub_delegates[quicr_namespace] = std::move(pub_delegate);

  const auto& context_id = transport_context_id.value();
  auto stream_id = transport_dgram_stream_id.value();
  if (use_reliable_transport) {
    stream_id = transport->createStream(context_id, true, priority);
    logger->debug << "Set stream: " << stream_id
                  << " to priority: " << static_cast<int>(priority)
                  << std::flush;
  }

  publish_state[quicr_namespace] = {
    .state = PublishContext::State::Pending,
    .transport_context_id = context_id,
    .transport_stream_id = stream_id,
  };

  const auto intent = messages::PublishIntent{
    messages::MessageType::PublishIntent,
    messages::create_transaction_id(),
    quicr_namespace,
    std::move(payload),
    stream_id,
    1,
  };

  messages::MessageBuffer msg{ sizeof(messages::PublishIntent) +
                               intent.payload.size() };
  msg << intent;

  auto error = transport->enqueue(context_id, stream_id, msg.take());

  return error == qtransport::TransportError::None;
}

void
ClientRawSession::publishIntentEnd(
  const quicr::Namespace& quicr_namespace,
  [[maybe_unused]] const std::string& auth_token)
{
  // TODO(trigaux): Authenticate token.

  if (!pub_delegates.contains(quicr_namespace)) {
    return;
  }

  pub_delegates.erase(quicr_namespace);

  const auto ps_it = publish_state.find(quicr_namespace);
  if (ps_it != publish_state.end()) {
    const auto intent_end = messages::PublishIntentEnd{
      messages::MessageType::PublishIntentEnd,
      quicr_namespace,
      {} // TODO(trigaux): Figure out payload.
    };

    messages::MessageBuffer msg;
    msg << intent_end;

    transport->enqueue(transport_context_id.value(),
                       ps_it->second.transport_stream_id,
                       msg.take());

    publish_state.erase(ps_it);
  }
}

void
ClientRawSession::subscribe(
  std::shared_ptr<SubscriberDelegate> subscriber_delegate,
  const quicr::Namespace& quicr_namespace,
  const SubscribeIntent& intent,
  [[maybe_unused]] const std::string& origin_url,
  bool use_reliable_transport,
  [[maybe_unused]] const std::string& auth_token,
  [[maybe_unused]] bytes&& e2e_token)
{
  const auto _ = std::lock_guard<std::mutex>(session_mutex);

  const auto& context_id = transport_context_id.value();
  auto transaction_id = messages::create_transaction_id();

  if (!sub_delegates.contains(quicr_namespace)) {
    sub_delegates[quicr_namespace] = std::move(subscriber_delegate);

    auto stream_id = transport_dgram_stream_id.value();
    if (use_reliable_transport) {
      stream_id = transport->createStream(context_id, true);
    }

    subscribe_state[quicr_namespace] = SubscribeContext{
      .state = SubscribeContext::State::Pending,
      .transport_context_id = context_id,
      .transport_stream_id = stream_id,
      .transaction_id = transaction_id,
    };
  }

  // We allow duplicate subscriptions, so we always send
  const auto sub_it = subscribe_state.find(quicr_namespace);
  if (sub_it == subscribe_state.end()) {
    return;
  }

  auto msg = messages::MessageBuffer{ sizeof(messages::Subscribe) };
  const auto subscribe =
    messages::Subscribe{ 0x1, transaction_id, quicr_namespace, intent };
  msg << subscribe;

  transport->enqueue(
    context_id, sub_it->second.transport_stream_id, msg.take());
}

void
ClientRawSession::unsubscribe(const quicr::Namespace& quicr_namespace,
                              const std::string& /* origin_url */,
                              const std::string& /* auth_token */)
{
  // The removal of the delegate is done on receive of subscription ended
  auto msg = messages::MessageBuffer{};
  const auto unsub = messages::Unsubscribe{ 0x1, quicr_namespace };
  msg << unsub;

  const auto& context_id = transport_context_id.value();
  const auto state_it = subscribe_state.find(quicr_namespace);
  if (state_it != subscribe_state.end()) {
    transport->enqueue(
      context_id, state_it->second.transport_stream_id, msg.take());
  }

  const auto _ = std::lock_guard<std::mutex>(session_mutex);
  removeSubscription(quicr_namespace,
                     SubscribeResult::SubscribeStatus::ConnectionClosed);
}

void
ClientRawSession::publishNamedObject(const quicr::Name& quicr_name,
                                     uint8_t priority,
                                     uint16_t expiry_age_ms,
                                     bool use_reliable_transport,
                                     bytes&& data)
{
  // start populating message to encode
  auto datagram = messages::PublishDatagram{};

  auto found = publish_state.find(quicr_name);

  if (found == publish_state.end()) {
    LOGGER_INFO(
      logger, "No publish intent for '" << quicr_name << "' missing, dropping");

    return;
  }

  auto& [ns, context] = *found;

  const auto& context_id = transport_context_id.value();
  if (context.transport_stream_id == transport_dgram_stream_id.value()) {
    use_reliable_transport = false;
  }

  if (context.state != PublishContext::State::Ready) {
    context.transport_context_id = context_id;
    context.state = PublishContext::State::Ready;

    LOGGER_INFO(logger, "Adding new context for published ns: " << ns);

  } else {
    auto gap_log = gap_check(
      true, quicr_name, context.last_group_id, context.last_object_id);

    if (!gap_log.empty()) {
      logger->Log(gap_log);
    }
  }

  datagram.header.name = quicr_name;
  datagram.header.media_id = context.transport_stream_id;
  datagram.header.group_id = context.last_group_id;
  datagram.header.object_id = context.last_object_id;
  datagram.header.priority = priority;
  datagram.header.offset_and_fin = 1ULL;
  datagram.media_type = messages::MediaType::RealtimeMedia;

  const auto stream_id = use_reliable_transport
                           ? context.transport_stream_id
                           : transport_dgram_stream_id.value();

  // Fragment the payload if needed
  if (data.size() <= quicr::max_transport_data_size || use_reliable_transport) {
    messages::MessageBuffer msg;

    datagram.media_data_length = data.size();
    datagram.media_data = std::move(data);

    msg << datagram;

    // No fragmenting needed
    transport->enqueue(
      context_id, stream_id, msg.take(), priority, expiry_age_ms);

  } else {
    // Fragments required. At this point this only counts whole blocks
    auto frag_num = data.size() / quicr::max_transport_data_size;
    const auto frag_remaining_bytes =
      data.size() % quicr::max_transport_data_size;

    auto offset = size_t(0);

    while (frag_num-- > 0) {
      auto msg = messages::MessageBuffer{};

      if (frag_num == 0 && frag_remaining_bytes == 0) {
        datagram.header.offset_and_fin = (offset << 1U) + 1U;
      } else {
        datagram.header.offset_and_fin = offset << 1U;
      }

      const auto ptr_offset = static_cast<ptrdiff_t>(offset);
      bytes frag_data(data.begin() + ptr_offset,
                      data.begin() + ptr_offset +
                        quicr::max_transport_data_size);

      datagram.media_data_length = frag_data.size();
      datagram.media_data = std::move(frag_data);

      msg << datagram;

      offset += quicr::max_transport_data_size;

      /*
       * For UDP based transports, some level of pacing is required to prevent
       * buffer overruns throughput the network path and with the remote end.
       *  TODO(paulej): Fix... This is set a bit high because the server code is
       * running too slow
       */
      if (need_pacing && (frag_num % 30) == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }

      if (transport->enqueue(
            context_id, stream_id, msg.take(), priority, expiry_age_ms) !=
          qtransport::TransportError::None) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        // No point in finishing fragment if one is dropped
        return;
      }
    }

    // Send last fragment, which will be less than max_transport_data_size
    if (frag_remaining_bytes > 0) {
      messages::MessageBuffer msg;
      datagram.header.offset_and_fin = uintVar_t((offset << 1U) + 1U);

      const auto ptr_offset = static_cast<ptrdiff_t>(offset);
      bytes frag_data(data.begin() + ptr_offset, data.end());
      datagram.media_data_length = static_cast<uintVar_t>(frag_data.size());
      datagram.media_data = std::move(frag_data);

      msg << datagram;

      if (auto err = transport->enqueue(
            context_id, stream_id, msg.take(), priority, expiry_age_ms);
          err != qtransport::TransportError::None) {
        LOGGER_WARNING(logger,
                       "Published object delayed due to enqueue error "
                         << static_cast<unsigned>(err));
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
    }
  }
}

void
ClientRawSession::publishNamedObjectFragment(
  const quicr::Name& /* quicr_name */,
  uint8_t /* priority */,
  uint16_t /* expiry_age_ms */,
  bool /* use_reliable_transport */,
  const uint64_t& /* offset */,
  bool /* is_last_fragment */,
  bytes&& /* data */)
{
  throw std::runtime_error("UnImplemented");
}

/*===========================================================================*/
// Internal Helper Methods
/*===========================================================================*/

void
ClientRawSession::removeSubscription(
  const quicr::Namespace& quicr_namespace,
  const SubscribeResult::SubscribeStatus& reason)
{
  const auto& context_id = transport_context_id.value();
  auto state_it = subscribe_state.find(quicr_namespace);
  if (state_it != subscribe_state.end()) {
    if (state_it->second.transport_stream_id > 1) {
      transport->closeStream(context_id, state_it->second.transport_stream_id);
    }

    subscribe_state.erase(state_it);
  }

  if (sub_delegates.contains(quicr_namespace)) {
    if (const auto& sub_delegate = sub_delegates[quicr_namespace]) {
      sub_delegate->onSubscriptionEnded(quicr_namespace, reason);
    }

    sub_delegates.erase(quicr_namespace);
  }
}

bool
ClientRawSession::notify_pub_fragment(
  const messages::PublishDatagram& datagram,
  const std::shared_ptr<SubscriberDelegate>& delegate,
  const std::map<uint32_t, bytes>& frag_map)
{
  if ((frag_map.rbegin()->first & 1U) != 0x1) {
    return false; // Not complete, return false that this can NOT be deleted
  }

  auto reassembled = bytes{};
  auto seq_bytes = size_t(0);
  for (const auto& [sequence_num, data] : frag_map) {
    if ((sequence_num >> 1U) - seq_bytes != 0) {
      // Gap in offsets, missing data, return false that this can NOT be deleted
      return false;
    }

    reassembled.insert(reassembled.end(),
                       std::make_move_iterator(data.begin()),
                       std::make_move_iterator(data.end()));

    seq_bytes += data.size();
  }

  delegate->onSubscribedObject(
    datagram.header.name, 0x0, 0x0, false, std::move(reassembled));

  return true;
}

void
ClientRawSession::handle_pub_fragment(
  messages::PublishDatagram&& datagram,
  const std::shared_ptr<SubscriberDelegate>& delegate)
{
  // Search from the current `circular_index` backwards to find a buffer that
  // knows about this datagram's name
  const auto name = datagram.header.name;
  auto curr_index = circular_index;
  auto not_found = false;
  while (!fragments.at(circular_index).contains(name)) {
    curr_index = (curr_index > 0) ? curr_index - 1 : fragments.size() - 1;
    if (curr_index == circular_index) {
      // We have completed a cycle without finding our name
      not_found = true;
      break;
    }
  }

  // If we looped back to `circular_index`, then there is no current entry for
  // this name, so we should insert in the current buffer.  If inserting here
  // would cause overflow, we proactively move to the next buffer.
  if (not_found) {
    auto curr_buffer_size = fragments.at(curr_index).size();
    if (curr_buffer_size >= max_pending_per_buffer - 1) {
      circular_index += 1;
      if (circular_index >= fragments.size()) {
        circular_index = 0;
      }

      fragments.at(circular_index).clear();
      curr_index = circular_index;
    }

    fragments.at(curr_index).insert({ name, {} });
  }

  // Insert into the appropriate buffer and see whether we have a complete
  // datagram
  auto& buffer = fragments.at(curr_index);
  auto& msg_fragments = buffer.at(name);
  msg_fragments.emplace(datagram.header.offset_and_fin,
                        std::move(datagram.media_data));
  if (notify_pub_fragment(datagram, delegate, msg_fragments)) {
    buffer.erase(name);
  }
}

void
ClientRawSession::handle(messages::MessageBuffer&& msg)
{
  if (msg.empty()) {
    std::cout << "Transport Reported Empty Data" << std::endl;
    return;
  }

  auto msg_type = static_cast<messages::MessageType>(msg.front());
  switch (msg_type) {
    case messages::MessageType::SubscribeResponse: {
      auto response = messages::SubscribeResponse{};
      msg >> response;

      const auto result = SubscribeResult{ .status = response.response };

      if (sub_delegates.contains(response.quicr_namespace)) {
        auto& context = subscribe_state[response.quicr_namespace];
        context.state = SubscribeContext::State::Ready;

        if (const auto& sub_delegate =
              sub_delegates[response.quicr_namespace]) {
          sub_delegate->onSubscribeResponse(response.quicr_namespace, result);
        }
      } else {
        std::cout << "Got SubscribeResponse: No delegate found for namespace"
                  << response.quicr_namespace << std::endl;
      }

      break;
    }

    case messages::MessageType::SubscribeEnd: {
      auto subEnd = messages::SubscribeEnd{};
      msg >> subEnd;

      const auto _ = std::lock_guard<std::mutex>(session_mutex);
      removeSubscription(subEnd.quicr_namespace, subEnd.reason);
      break;
    }

    case messages::MessageType::Publish: {
      auto datagram = messages::PublishDatagram{};
      msg >> datagram;

      if (auto found = sub_delegates.find(datagram.header.name);
          found != sub_delegates.end()) {
        const auto& [ns, delegate] = *found;

        auto& context = subscribe_state[ns];

        auto gap_log = gap_check(false,
                                 datagram.header.name,
                                 context.last_group_id,
                                 context.last_object_id);

        if (!gap_log.empty()) {
          logger->Log(gap_log);
        }

        if (datagram.header.offset_and_fin != uintVar_t(0x1)) {
          handle_pub_fragment(std::move(datagram), delegate);
          break;
        }

        // No-fragment, process as single object
        delegate->onSubscribedObject(datagram.header.name,
                                     0x0,
                                     0x0,
                                     false,
                                     std::move(datagram.media_data));
      }
      break;
    }

    case messages::MessageType::PublishIntentResponse: {
      auto response = messages::PublishIntentResponse{};
      msg >> response;

      if (!pub_delegates.contains(response.quicr_namespace)) {
        std::cout
          << "Got PublishIntentResponse: No delegate found for namespace "
          << response.quicr_namespace << std::endl;
        return;
      }

      if (const auto& delegate = pub_delegates[response.quicr_namespace]) {
        const auto result = PublishIntentResult{
          .status = response.response,
          .redirectInfo = {},
          .reassignedName = {},
        };
        delegate->onPublishIntentResponse(response.quicr_namespace, result);
      }

      break;
    }

    default:
      break;
  }
}
} // namespace quicr
