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
#include "quicr/moq_message_types.h"
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


// MOQ Related Constants
static constexpr quicr::Name track_id_mask = ~(~0x0_name >> 60);
//constexpr quicr::Name Group_ID_Mask = ~(~0x0_name << 32) << 16;
//constexpr quicr::Name Object_ID_Mask = ~(~0x0_name << 16);


static std::tuple<std::string, std::string> split_track_name(std::string track) {
    std::string namespace_part;
    std::string track_name_part;
    const std::string t = "track/";
    auto it =
            std::search(track.begin(), track.end(), t.begin(), t.end());
    if(it == track.end()) {
        throw std::runtime_error("Invalid Track: " + track);
    }

    namespace_part.reserve(distance(track.begin(), it));
    namespace_part.assign(track.begin(), it);
    // move to end form track/
    std::advance(it, t.length());

    do {
        auto slash = std::find(it, track.end(), '/');
        if (slash == track.end()) {
            track_name_part.reserve(distance(it, slash));
            track_name_part.assign(it, slash);
            break;
        }
        it++;

    } while (it != track.end());

    return std::make_tuple(namespace_part, track_name_part);
}

/*===========================================================================*/
// ClientRawSession
/*===========================================================================*/

ClientRawSession::ClientRawSession(const RelayInfo& relay_info,
                                   const qtransport::TransportConfig& tconfig,
                                   const cantina::LoggerPointer& logger,
                                   std::shared_ptr<UriConvertor> uri_convertor_in)
  : logger(std::make_shared<cantina::Logger>("QSES", logger))
    , enable_moq(tconfig.enable_moq)
    , uri_convertor(uri_convertor_in)
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
  const cantina::LoggerPointer& logger,
  std::shared_ptr<UriConvertor> uri_convertor_in)
  : has_shared_transport{ true }
  , logger(std::make_shared<cantina::Logger>("QSES", logger))
  , transport(std::move(transport_in))
  , uri_convertor(uri_convertor_in)
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

  transport_conn_id = transport->start();

  LOGGER_INFO(logger, "Connecting session " << *transport_conn_id << "...");

  while (!stopping &&
         transport->status() == qtransport::TransportStatus::Connecting) {
    LOGGER_DEBUG(logger, "Connecting... " << int(stopping.load()));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  if (stopping || !transport) {
    LOGGER_INFO(logger, "Cancelling connecting session " << *transport_conn_id);
    return false;
  }

  if (!connected()) {
    std::ostringstream msg;
    msg << "Session " << *transport_conn_id
        << " failed to connect to server, transport status: "
        << int(transport->status());
    logger->Log(cantina::LogLevel::Critical, msg.str());

    throw std::runtime_error(msg.str());
  }

  // Create reliable bidirectional control stream
  transport_ctrl_data_ctx_id = transport->createDataContext(*transport_conn_id, true, 0, true);

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

  const auto& conn_id =
    transport_conn_id ? transport_conn_id.value() : 0;
  LOGGER_DEBUG(logger, "Disconnecting session " << conn_id << "...");

  stopping = true;
  try {
    transport->close(conn_id);
  } catch (const std::exception& e) {
    LOGGER_ERROR(
      logger, "Error disconnecting session " << conn_id << ": " << e.what());
    return false;
  } catch (...) {
    LOGGER_ERROR(logger, "Unknown error disconnecting session " << conn_id);
    return false;
  }

  LOGGER_INFO(logger, "Disconnected session " << conn_id << "!");

  transport_conn_id = std::nullopt;
  transport_ctrl_data_ctx_id = std::nullopt;

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
  const qtransport::TransportConnId& conn_id,
  const qtransport::TransportStatus status)
{
  {
    LOGGER_DEBUG(logger,
                 "connection_status: cid: " << conn_id
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
                    << conn_id);
      [[fallthrough]];
    case qtransport::TransportStatus::Shutdown:
      [[fallthrough]];
    case qtransport::TransportStatus::RemoteRequestClose:
      LOGGER_INFO(logger, "Shutting down context: " << conn_id);
      stopping = true;
      break;
  }
}

void
ClientRawSession::on_new_connection(
  const qtransport::TransportConnId& /* conn_id */,
  const qtransport::TransportRemote& /* remote */)
{
}

void
ClientRawSession::on_new_data_context(const qtransport::TransportConnId &conn_id, const qtransport::DataContextId &data_ctx_id) {
  LOGGER_INFO(logger, "New BiDir data context conn_id: " << conn_id << " data_ctx_id: " << data_ctx_id);
}

void
ClientRawSession::on_recv_notify(
  const qtransport::TransportConnId& conn_id,
  const qtransport::DataContextId& data_ctx_id,
  [[maybe_unused]] const bool is_bidir)
{
  if (!transport) {
    return;
  }

  for (int i = 0; i < 150; i++) {
    auto data = transport->dequeue(conn_id, data_ctx_id);

    if (!data.has_value()) {
      return;
    }

    //      std::cout << "on_recv_notify: conn_id: " << conn_id
    //                << " data_ctx_id: " << data_ctx_id
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
                                const TransportMode transport_mode,
                                uint8_t priority)
{
  if (announce_delegates.contains(quicr_namespace)) {
    return true;
  }

  announce_delegates[quicr_namespace] = std::move(pub_delegate);
  const auto& conn_id = transport_conn_id.value();
  auto data_ctx_id = get_data_ctx_id(conn_id, transport_mode, priority);
  auto track_namespace = uri_convertor->to_namespace_uri(quicr_namespace);

  logger->info << "moqt:Announce TrackNamespace: " << track_namespace
               << ", Quicr Namespace: " << quicr_namespace
               << " data_ctx_id: " << data_ctx_id
               << " priority: " << static_cast<int>(priority)
               << " mode: " << static_cast<int>(transport_mode)
               << std::flush;

  auto info = AnnounceInfo{};
  info.announce_id = messages::create_transaction_id();
  info.quicr_namespace = quicr_namespace;
  info.track_namespace = track_namespace;
  info.transport_mode = transport_mode;
  info.transport_context.transport_conn_id = conn_id;
  info.transport_context.transport_data_ctx_id = data_ctx_id;
  info.state = AnnounceInfo::Pending;

  announcements[quicr_namespace] = std::move(info);
  auto announce = messages::MoqAnnounce {
    .track_namespace = track_namespace
  };

  messages::MessageBuffer msg;
  msg << announce;
  auto error = transport->enqueue(conn_id, *transport_ctrl_data_ctx_id, msg.take());
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

    transport->enqueue(ps_it->second.transport_conn_id, *transport_ctrl_data_ctx_id, msg.take());

    if (ps_it->second.transport_mode != TransportMode::Unreliable) {
      transport->deleteDataContext(ps_it->second.transport_conn_id, ps_it->second.transport_data_ctx_id);
    }

    publish_state.erase(ps_it);
  }
}

void
ClientRawSession::subscribe(
  std::shared_ptr<SubscriberDelegate> subscriber_delegate,
  const quicr::Namespace& quicr_namespace,
  const SubscribeIntent& intent,
  const TransportMode transport_mode,
  [[maybe_unused]] const std::string& origin_url,
  [[maybe_unused]] const std::string& auth_token,
  [[maybe_unused]] bytes&& e2e_token,
  const uint8_t priority)
{
  std::lock_guard<std::mutex> _(session_mutex);
  const auto& conn_id = transport_conn_id.value();
  if (!sub_delegates.contains(quicr_namespace)) {
      sub_delegates[quicr_namespace] = std::move(subscriber_delegate);
      logger->info << "(New) Subscribe ns: " << quicr_namespace
                   << " priority: " << static_cast<int>(priority)
                   << " mode: " << static_cast<int>(transport_mode)
                   << std::flush;
  }

  auto moq_uri = uri_convertor->to_namespace_uri(quicr_namespace);
  auto name = quicr_namespace.name();
  auto track_id_qname = name & track_id_mask;
  auto track_alias = track_id_qname.hi();

  logger->warning << "TrackID Mask is Hardcoded, Make it application configuration" << std::flush;

  const auto subscription_id = messages::create_transaction_id();;
  // create new subscription context
  auto subscription = SubscriptionInfo{};
  subscription.track_info.quicr_namespace = quicr_namespace;
  subscription.track_info.fulltrackname = std::move(moq_uri);
    subscription.track_info.track_alias = track_alias;
  subscription.state = SubscriptionInfo::State::pending;
  subscription.subscription_id  = subscription_id;
  // setup transport info
  subscription.transport_context.transport_conn_id = conn_id;
  subscription.transport_context.transport_data_ctx_id = 0;

  auto [start_group, end_group, start_object, end_object] = messages::to_locations(intent);
  auto subscribe = messages::MoqSubscribe {
          .subscribe_id = subscription.subscription_id,
          .track_alias = subscription.track_info.track_alias,
          .track = subscription.track_info.fulltrackname,
          .start_group = start_group,
          .start_object = start_object,
          .end_group = end_group,
          .end_object = end_object,
          .track_params = {}
  };

  logger->info << "moqt:subscribe: Track: " << moq_uri
                << ", Namespace: " << subscription.track_info.quicr_namespace
                << ", Track-Alias: " << subscription.track_info.track_alias
                << ", SubscribeId: " << subscription.subscription_id << std::flush;

  auto msg = messages::MessageBuffer{};
  msg << subscribe;
  transport->enqueue(conn_id, *transport_ctrl_data_ctx_id, msg.take());

  logger->info << "MOQSubscribe: Save Subscription for ID:" << subscription.subscription_id << std::flush;

  // save the state
  subscriptions[subscription_id] = subscription;
  return;
}

void
ClientRawSession::unsubscribe(const quicr::Namespace& quicr_namespace,
                              const std::string& /* origin_url */,
                              const std::string& /* auth_token */)
{
  // The removal of the delegate is done on receive of subscription ended
    const auto state_it = subscribe_state.find(quicr_namespace);
    if (state_it == subscribe_state.end()) {
        // log error
        return;
    }

    auto msg = messages::MessageBuffer{};
    if (enable_moq) {
      auto unsubscribe = messages::MoqUnsubscribe {
          .subscribe_id = state_it->second.transaction_id
      };

      msg << unsubscribe;
      transport->enqueue(state_it->second.transport_conn_id, *transport_ctrl_data_ctx_id, msg.take());
        return;
  }

  const auto unsub = messages::Unsubscribe{ 0x1, quicr_namespace };
  msg << unsub;
  transport->enqueue(state_it->second.transport_conn_id, *transport_ctrl_data_ctx_id, msg.take());

 std::lock_guard<std::mutex> _(session_mutex);
 removeSubscription(quicr_namespace,
                 SubscribeResult::SubscribeStatus::ConnectionClosed);
}

void
ClientRawSession::publishNamedObject(const quicr::Name& quicr_name,
                                     uint8_t priority,
                                     uint16_t expiry_age_ms,
                                     bytes&& data)
{
  // is the track in the scope of announcement
  auto it = announcements.find(quicr_name);
  if (it == announcements.end()) {
    logger->info << "moqt:publish: No publish intent for " << quicr_name << " dropping" << std::flush;
    return;
  }

  // is announce active
  auto& [ns, announce_info] = *it;
  if (announce_info.state != AnnounceInfo::State::Active) {
    logger->info << "moqt:publish:: Publish intent NOT READY for ns: " << ns << std::flush;
    return;
  }

  TrackNamePrefix track_id_qname = static_cast<TrackNamePrefix>(quicr_name & track_id_mask);
  uint64_t group_id = quicr_name.bits<uint64_t>(16, 32);
  uint64_t object_id = quicr_name.bits<uint64_t>(0, 16);

  // get the track under this announce
  if (!announce_info.tracks.count(track_id_qname)) {
    logger->info << "moqt:object: adding new track, FIX THIS TO HAPPEN VIA SUBSCRIBE" << std::flush;
    // first object on this track
    auto track_info = TrackInfo {
      .quicr_namespace = quicr::Namespace(quicr_name, 60),
      .state = TrackInfo::State::Ready, // TODO: Set it based on subscribe for the track
      .last_group_id = group_id,
      .last_object_id = object_id
    };

    announce_info.tracks.emplace(track_id_qname, track_info);
  }


  auto& track = announce_info.tracks[track_id_qname];
  logger->info << "moqt:object(before gapcheck): groupId: " << track.last_group_id
               << ", objectId:" << track.last_object_id
               << std::flush;

  // IMPORTANT - Gap check updates the last_group_id and last_object_id to be current group/object
  const auto prev_group_id = track.last_object_id;
  auto gap_log = gap_check(true, quicr_name, track.last_group_id, track.last_object_id);
  if (!gap_log.empty()) {
    logger->Log(gap_log);
  }
  logger->info << "moqt:object(after gapcheck: groupId: "
              << track.last_group_id << ", objectId:"
              << track.last_object_id << std::flush;


  qtransport::ITransport::EnqueueFlags eflags;
  // TODO: This assumes transport mode is set at the annouce level
  // and not per track level.
  switch (track.transport_mode) {
    case TransportMode::ReliablePerGroup: {
      if (track.last_group_id && track.last_group_id != prev_group_id) {
        logger->info <<"moqt:object: Setting new stream: groupId:" << track.last_group_id << std::flush;
        eflags.new_stream = true;
        eflags.use_reset = true;
        eflags.clear_tx_queue = true;
      }
      break;
    }

    case TransportMode::ReliablePerObject: {
      // one stream per object
      eflags.new_stream = true;
      auto object_msg = messages::MoqObjectStream {
        .subscribe_id = 0xABCD, // TODO: this needs to be fixed
        .group_id = track.last_group_id,
        .object_id = track.last_object_id,
        .track_alias = track_id_qname,
        .priority = priority,
        .payload = std::move(data)
      };

      logger->info << "moqt:ObjectStream: qname: " << quicr_name
                  << ", track_alias:" << track_id_qname
                  << ", Group:" << track.last_group_id
                  << ", Object:" << track.last_object_id << std::flush;

      messages::MessageBuffer msg;
      msg << object_msg;
      transport->enqueue(announce_info.transport_context.transport_conn_id,
                         announce_info.transport_context.transport_data_ctx_id,
                         msg.take(), priority, expiry_age_ms, eflags);
      return;
    }

    case TransportMode::ReliablePerTrack: {
        logger->info << "moqt:object:track" << std::flush;
        auto track_hdr = messages::MoqStreamHeaderTrack {};
        track_hdr.subscribe_id = 0x0;
        track_hdr.track_alias = 0;
        track_hdr.priority = priority;
    }
    case TransportMode::Unreliable:
      [[fallthrough]];
    default:
      break;
  }

#if 0
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

      if (transport->enqueue(context.transport_conn_id, context.transport_data_ctx_id, msg.take(),
                             priority, expiry_age_ms, eflags) !=
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
            context.transport_conn_id, context.transport_data_ctx_id, msg.take(), priority, expiry_age_ms);
          err != qtransport::TransportError::None) {
        LOGGER_WARNING(logger,
                       "Published object delayed due to enqueue error "
                         << static_cast<unsigned>(err));
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
    }
#endif
}

void
ClientRawSession::publishNamedObjectFragment(
  const quicr::Name& /* quicr_name */,
  uint8_t /* priority */,
  uint16_t /* expiry_age_ms */,
  const uint64_t& /* offset */,
  bool /* is_last_fragment */,
  bytes&& /* data */)
{
  throw std::runtime_error("UnImplemented");
}

/*===========================================================================*/
// Internal Helper Methods
/*===========================================================================*/

qtransport::DataContextId ClientRawSession::get_data_ctx_id(
  const qtransport::TransportConnId conn_id,
  const TransportMode transport_mode,
  const uint8_t priority) {

  switch (transport_mode) {
    case TransportMode::ReliablePerTrack: {
      return transport->createDataContext(conn_id, true, priority, true);
    }

    case TransportMode::ReliablePerGroup:
      [[fallthrough]];
    case TransportMode::ReliablePerObject: {
      return transport->createDataContext(conn_id, true, priority, false);
    }

    case TransportMode::Unreliable:
      [[fallthrough]];
    default: { // Treat as unreliable
      return transport->createDataContext(conn_id, false, priority, false);
    }
  }
}


void
ClientRawSession::removeSubscription(
  const quicr::Namespace& quicr_namespace,
  const SubscribeResult::SubscribeStatus& reason)
{
  auto state_it = subscribe_state.find(quicr_namespace);
  if (state_it != subscribe_state.end()) {
    if (state_it->second.transport_mode != TransportMode::Unreliable) {
      transport->deleteDataContext(state_it->second.transport_conn_id, state_it->second.transport_data_ctx_id);
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
    datagram.header.name, 0x0, std::move(reassembled));

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
ClientRawSession::handle_moq(messages::MessageBuffer &&msg) {
    auto msg_type = static_cast<uint8_t>(msg.front());
    logger->info << "HandleMoQ: Got Message:" << msg_type << std::flush;
    switch (msg_type) {
        case messages::MESSAGE_TYPE_SUBSCRIBE_OK: {
            auto subscribe_ok = messages::MoqSubscribeOk{};
            msg >> subscribe_ok;
            logger ->info << "moqt:subscribe_ok: id:" << subscribe_ok.subscribe_id
                          << ", expires:" << subscribe_ok.expires << std::flush;
            const auto result = SubscribeResult{ .status = SubscribeResult::SubscribeStatus::Ok,
                                                 .subscriber_expiry_interval = subscribe_ok.expires};

            // check if the subscription exists
            if (!subscriptions.count(subscribe_ok.subscribe_id)) {
                logger ->info << "moqt:subscribe_ok: invalid" << std::flush;
                return;
            }

            auto& subscription = subscriptions.at(subscribe_ok.subscribe_id);

            if (sub_delegates.contains(subscription.track_info.quicr_namespace)) {
                subscription.state = SubscriptionInfo::State::ready;
                sub_delegates.at(subscription.track_info.quicr_namespace)->onSubscribeResponse(subscription.track_info.quicr_namespace, result);
            } else {
                logger->error << "moqt:subscribe_ok: No delegate found: Track:" << subscription.track_info.fulltrackname
                          << ", Namespace: "<< subscription.track_info.quicr_namespace << std::flush;
            }
            break;
        }

        case messages::MESSAGE_TYPE_ANNOUNCE_OK: {
            auto announce_ok = messages::MoqAnnounceOk{};
            msg >> announce_ok;
            auto qns = uri_convertor->to_quicr_namespace(announce_ok.track_namespace);
            if (!announce_delegates.contains(qns)) {
                std::cout
                << "Got AnnounceOk: No delegate found for namespace "
                        << announce_ok.track_namespace << std::endl;
                return;
            }

            auto it = announcements.find(qns);

            if (it == announcements.end()) {
                LOGGER_ERROR(logger, "No pending announce for '" << qns << "' missing, dropping");
                break;
            }


            if (it->second.state != AnnounceInfo::State::Active) {
                logger->info << "Announce active for ns: " << qns << std::flush;
                it->second.state = AnnounceInfo::State::Active;
            }

            if (const auto& delegate = announce_delegates[qns]) {
                const auto result = PublishIntentResult{
                        .status = messages::Response::Ok,
                };
                delegate->onPublishIntentResponse(qns, result);
            }
            break;
        }

        default:
            logger->info << "HandleMoQ: Default Caase" <<  std::flush;
            break;
    }
}

void
ClientRawSession::handle(messages::MessageBuffer&& msg)
{
  if (msg.empty()) {
    std::cout << "Transport Reported Empty Data" << std::endl;
    return;
  }

  if (enable_moq) {
      handle_moq(std::move(msg));
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
                                 context.group_id,
                                 context.object_id);

        if (!gap_log.empty()) {
          logger->info << "conn_id: " << context.transport_conn_id
                       << " data_ctx_id: " << context.transport_data_ctx_id
                       << " " << gap_log << std::flush;
        }

        if (datagram.header.offset_and_fin != uintVar_t(0x1)) {
          handle_pub_fragment(std::move(datagram), delegate);
          break;
        }

        // No-fragment, process as single object
        delegate->onSubscribedObject(datagram.header.name,
                                     0x0,
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

      auto ps_it = publish_state.find(response.quicr_namespace);

      if (ps_it == publish_state.end()) {
        LOGGER_ERROR(logger, "No publish intent for '" << response.quicr_namespace << "' missing, dropping");
        break;
      }


      if (ps_it->second.state != PublishContext::State::Ready) {
        logger->info << "Publish intent ready for ns: " << response.quicr_namespace << std::flush;
        ps_it->second.state = PublishContext::State::Ready;
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
