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
#include "quicr/message_buffer.h"
#include "quicr/quicr_client.h"
#include "quicr/quicr_client_delegate.h"
#include "quicr/quicr_common.h"
#include "quicr/gap_check.h"
#include "quicr/moq_message_types.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace quicr {

namespace {
/*
 * Nested map to reassemble message fragments
 *
 *    Structure:
 *       fragments[<circular index>] = map[quicr_name] = map[offset] = data
 *
 *    Circular index is a small int value that increments from 1 to max. It
 *    wraps to 1 after reaching max size.  In this sense, it's a circular
 *    buffer. Upon moving to a new index the new index data will be purged (if
 *    any exists).
 *
 *    Fragment reassembly avoids timers and time interval based checks. It
 *    instead is based on received data. Every message quicr_name is checked to
 *    see if it's complete. If so, the published object callback will be
 *    executed. If not, it'll only update the map with the new offset value.
 *    Incomplete messages can exist in the cache for as long as the circular
 *    index hasn't wrapped to the same point in cache.  Under high load/volume,
 *    this can wrap within a minute or two.  Under very little load, this could
 *    linger for hours. This is okay considering the only harm is a little extra
 *    memory being used. Extra memory is a trade-off for being event/message
 *    driven instead of timer based with threading/locking/...
 */
std::map<int, std::map<quicr::Name, std::map<int, bytes>>> fragments;

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
}

/*===========================================================================*/
// QuicRClientRawSession
/*===========================================================================*/

QuicRClientRawSession::QuicRClientRawSession(
  RelayInfo& relay_info,
  qtransport::TransportConfig tconfig,
  const cantina::LoggerPointer& logger)
  : logger(std::make_shared<cantina::Logger>("QSES", logger))
{
  this->logger->Log("Initialize QuicRClient");

  if (relay_info.proto == RelayInfo::Protocol::UDP) {
    // For plain UDP, pacing is needed. For QUIC it's not needed.
    need_pacing = true;
  }

  qtransport::TransportRemote server = to_TransportRemote(relay_info);
  transport = qtransport::ITransport::make_client_transport(
    server, std::move(tconfig), *this, this->logger);
  // enable moq for default
  enable_moq = true;
}

QuicRClientRawSession::QuicRClientRawSession(
  std::shared_ptr<qtransport::ITransport> transport_in,
  const cantina::LoggerPointer& logger)
  : has_shared_transport{ true }
  , logger(std::make_shared<cantina::Logger>("QSES", logger))
  , transport(transport_in)
{
}

QuicRClientRawSession::~QuicRClientRawSession()
{
  if (!has_shared_transport &&
      transport->status() != qtransport::TransportStatus::Disconnected) {
    disconnect();
  }
}

bool
QuicRClientRawSession::connect()
{
  client_status = ClientStatus::CONNECTING;
  transport_context_id = transport->start();
  logger->info << "Connecting session " << transport_context_id << "..."
               << std::flush;

  while (!stopping && client_status == ClientStatus::CONNECTING) {
    logger->info << " ... connecting:  " << static_cast<uint32_t>(client_status)
                 << std::flush;

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  if (stopping) {
    logger->info << "Cancelling connecting session " << transport_context_id
                 << std::flush;
    return false;
  }

  if (client_status != ClientStatus::READY) {
    std::ostringstream msg;
    msg << "Session " << transport_context_id
        << " failed to connect to server, transport status: "
        << int(transport->status());
    logger->Log(cantina::LogLevel::Critical, msg.str());

    throw std::runtime_error(msg.str());
  }

  transport_dgram_stream_id = transport->createStream(transport_context_id, false);
  // setup control stream for control messages as a reliable stream
  control_stream_id = transport->createStream(transport_context_id, true);

  std::ostringstream msg;
  msg << "Created Transport Session " << transport_context_id
      << " Datagram Stream Id: " << transport_dgram_stream_id
      << " Control Stream Id:" << control_stream_id;
  logger->Log(cantina::LogLevel::Info, msg.str());
  return true;
}

bool
QuicRClientRawSession::disconnect()
{
  LOGGER_DEBUG(logger,
               "Disconnecting session " << transport_context_id << "...");

  stopping = true;
  try {
    transport->close(transport_context_id);
  } catch (const std::exception& e) {
    logger->error << "Error disconnecting session " << transport_context_id
                  << ": " << e.what() << std::flush;
    return false;
  } catch (...) {
    logger->error << "Unknown error disconnecting session "
                  << transport_context_id << std::flush;
    return false;
  }

  logger->info << "Successfully disconnected session: " << transport_context_id
               << std::flush;

  client_status = ClientStatus::TERMINATED;
  return true;
}

/*===========================================================================*/
// Transport Delegate Events
/*===========================================================================*/

void
QuicRClientRawSession::on_connection_status(
  const qtransport::TransportContextId& context_id,
  const qtransport::TransportStatus status)
{
  {
    LOGGER_DEBUG(logger,"connection_status: cid: " << context_id << " status: " << int(status));
  }

  switch (status) {
    case qtransport::TransportStatus::Connecting:
      client_status = ClientStatus::CONNECTING;
      stopping = false;
      break;
    case qtransport::TransportStatus::Ready:
      client_status = ClientStatus::READY;
      stopping = false;
      break;
    case qtransport::TransportStatus::Disconnected: {
      client_status = ClientStatus::RELAY_NOT_CONNECTED;
      stopping = true;

      logger->info << "Removing state for context_id: " << context_id
                   << std::flush;
      break;
    }
    case qtransport::TransportStatus::Shutdown:
      [[fallthrough]];
    case qtransport::TransportStatus::RemoteRequestClose:
      client_status = ClientStatus::TERMINATED;
      stopping = true;
      break;
  }
}

void
QuicRClientRawSession::on_new_connection(
  const qtransport::TransportContextId& /* context_id */,
  const qtransport::TransportRemote& /* remote */)
{
}

void
QuicRClientRawSession::on_new_stream(
  const qtransport::TransportContextId& /* context_id */,
  const qtransport::StreamId& /* mStreamId */)
{
}

void
QuicRClientRawSession::on_recv_notify(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& streamId)
{
  if (!transport) {
      return;
  }

    logger->info << "on_recv_notify, StreamId: " << streamId << std::flush;

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
      if (enable_moq) {
         handle_moq(std::move(msg_buffer));
      }  else {
          handle(std::move(msg_buffer));
      }
    } catch (const messages::MessageBuffer::ReadException& e) {
      logger->info << "Dropping malformed message: " << e.what() << std::flush;
      return;
    } catch (const std::exception& e) {
      logger->info << "Dropping malformed message: " << e.what() << std::flush;
      return;
    } catch (...) {
      logger->Log(cantina::LogLevel::Critical,
                  "Received malformed message with unknown fatal error");
      throw;
    }
  }
}

/*===========================================================================*/
// QuicrClientSession API Methods
/*===========================================================================*/

bool
QuicRClientRawSession::publishIntent(
  std::shared_ptr<PublisherDelegate> pub_delegate,
  const Namespace & quicr_namespace,
  const std::string& /* origin_url */,
  const std::string& /* auth_token */,
  bytes&& payload,
  bool use_reliable_transport)
{

  if (pub_delegates.count(quicr_namespace)) {
    return true;
  }

  pub_delegates[quicr_namespace] = pub_delegate;
  auto delivery_context = TransportDeliveryContext {
      .control_stream_id = control_stream_id,
  };

  publish_state[quicr_namespace] = {.state = PublishContext::State::Pending,
          .transport_context_id = transport_context_id,
          .delivery_context = delivery_context,
          .control_stream_id = control_stream_id,
          .transport_stream_id = 0};

  auto error = qtransport::TransportError::None;
  if (enable_moq) {
    std::stringstream ss;
    ss << quicr_namespace;
    auto announce = messages::MoqAnnounce {
        .track_namespace = "moq://" + ss.str()
    };
      messages::MessageBuffer msg{};
      msg << announce;
  } else {
      messages::PublishIntent intent{messages::MessageType::PublishIntent,
                                     messages::create_transaction_id(),
                                     quicr_namespace,
                                     std::move(payload)};

      messages::MessageBuffer msg{sizeof(messages::PublishIntent) +
                                  intent.payload.size()};
      msg << intent;
      error = transport->enqueue(transport_context_id, control_stream_id, msg.take());
  }

  return error == qtransport::TransportError::None;
}

void
QuicRClientRawSession::publishIntentEnd(
  const quicr::Namespace& quicr_namespace,
  [[maybe_unused]] const std::string& auth_token)
{
  // TODO: Authenticate token.

  if (!pub_delegates.count(quicr_namespace)) {
    return;
  }

  pub_delegates.erase(quicr_namespace);

  auto ps_it = publish_state.find(quicr_namespace);
  if (ps_it != publish_state.end()) {
      if (enable_moq) {
        std::stringstream nss;
        nss << "moq://" << quicr_namespace;
        auto unannounce = messages::MoqUnannounce {
            .track_namespace = nss.str(),
        };
        messages::MessageBuffer msg;
        msg << unannounce;
        transport->enqueue(transport_context_id, ps_it->second.control_stream_id, msg.take());
      } else {
          messages::PublishIntentEnd intent_end{
                  messages::MessageType::PublishIntentEnd,
                  quicr_namespace,
                  {} // TODO: Figure out payload.
          };

          messages::MessageBuffer msg;
          msg << intent_end;

          transport->enqueue(transport_context_id, ps_it->second.control_stream_id, msg.take());
      }


      publish_state.erase(ps_it);
  }
}

void
QuicRClientRawSession::subscribe(
  std::shared_ptr<SubscriberDelegate> subscriber_delegate,
  const quicr::Namespace& quicr_namespace,
  const SubscribeIntent& intent,
  [[maybe_unused]] const std::string& origin_url,
  bool use_reliable_transport,
  [[maybe_unused]] const std::string& auth_token,
  [[maybe_unused]] bytes&& e2e_token)
{

  std::lock_guard<std::mutex> _(session_mutex);

  auto transaction_id = messages::create_transaction_id();

  if (!sub_delegates.count(quicr_namespace)) {
    sub_delegates[quicr_namespace] = subscriber_delegate;

    qtransport::StreamId stream_id = transport_dgram_stream_id;

    if (use_reliable_transport) {
      stream_id = transport->createStream(transport_context_id, true);
    }

    subscribe_state[quicr_namespace] =
      SubscribeContext{ .state =SubscribeContext::State::Pending,
                        .transport_context_id = transport_context_id,
                        .transport_stream_id = stream_id,
                        .control_stream_id = control_stream_id,
                        .transaction_id = transaction_id};
  }

  // We allow duplicate subscriptions, so we always send it even if it exists already
  auto sub_it = subscribe_state.find(quicr_namespace);
  if (sub_it != subscribe_state.end()) {
      if (enable_moq) {
          // TODO: change the API to use quicr_name instead
          std::stringstream  ss;
          auto qn = quicr_namespace.name();
          name_to_ns[qn] = quicr_namespace;
          ss << "moq://" << qn;
          auto moq_subscribe = messages::MoqSubscribe {
              .track = ss.str()
          };
          messages::MessageBuffer msg{};
          msg << moq_subscribe;
          transport->enqueue(transport_context_id, sub_it->second.control_stream_id, msg.take());
      } else {
          // encode subscribe
          messages::MessageBuffer msg{};
          messages::Subscribe subscribe{0x1, transaction_id, quicr_namespace, intent};
          msg << subscribe;
          transport->enqueue(transport_context_id, sub_it->second.control_stream_id, msg.take());
      }
  }
}

void
QuicRClientRawSession::unsubscribe(const quicr::Namespace& quicr_namespace,
                                   const std::string& /* origin_url */,
                                   const std::string& /* auth_token */)
{
  // The removal of the delegate is done on receive of subscription ended
  std::stringstream  ss;
  ss << "moq://" << quicr_namespace.name();
  messages::MessageBuffer msg{};

  if (enable_moq) {
      auto moq_unsubscribe = messages::MoqUnsubscribe {
          .track = ss.str(),
      };
      msg << moq_unsubscribe;
  } else {
      messages::Unsubscribe unsub{ 0x1, quicr_namespace };
      msg << unsub;
  }

  auto state_it = subscribe_state.find(quicr_namespace);
  if (state_it != subscribe_state.end()) {
      transport->enqueue(transport_context_id,
                         state_it->second.control_stream_id, msg.take());
  }

   std::lock_guard<std::mutex> _(session_mutex);
  removeSubscription(quicr_namespace,
                     SubscribeResult::SubscribeStatus::ConnectionClosed);
}


void
QuicRClientRawSession::publishNamedObject(
  const quicr::Name& quicr_name,
  uint8_t priority,
  uint16_t expiry_age_ms,
  bool use_reliable_transport,
  bytes&& data)
{
  // start populating message to encode
  messages::PublishDatagram datagram;

  auto found = publish_state.find(quicr_name);

  if (found == publish_state.end()) {
      logger->info << "No publish intent for '" << quicr_name
                   << "' missing, dropping" << std::flush;

      return;
  }

  auto& [ns, context] = *found;

  if (context.state != PublishContext::State::Ready) {
    context.transport_context_id = transport_context_id;
    context.state = PublishContext::State::Ready;

    logger->info << "Adding new context for published ns: " << ns << std::flush;

  } else {
    auto gap_log = gap_check(true, quicr_name,
                             context.last_group_id, context.last_object_id);

    if (!gap_log.empty()) {
        logger->Log(gap_log);
    }
  }

  datagram.header.name = quicr_name;
  datagram.header.media_id =context.transport_stream_id;
  datagram.header.group_id = context.last_group_id;
  datagram.header.object_id = context.last_object_id;
  datagram.header.forwarding_preference = 0x0;
  datagram.header.offset_and_fin = 1ULL;
  datagram.media_type = messages::MediaType::RealtimeMedia;

  qtransport::StreamId stream_id = use_reliable_transport ? context.transport_stream_id : transport_dgram_stream_id;

  // Fragment the payload if needed
  if (data.size() <= quicr::MAX_TRANSPORT_DATA_SIZE || use_reliable_transport ) {
    messages::MessageBuffer msg;

    datagram.media_data_length = data.size();
    datagram.media_data = std::move(data);

    msg << datagram;

    // No fragmenting needed
    transport->enqueue(transport_context_id,
                       stream_id,
                       msg.take(),
                       priority,
                       expiry_age_ms);

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

      if (transport->enqueue(transport_context_id,
                             stream_id,
                             msg.take(),
                             priority,
                             expiry_age_ms) !=
          qtransport::TransportError::None) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        // No point in finishing fragment if one is dropped
        return;
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

      if (auto err = transport->enqueue(transport_context_id,
                                        stream_id,
                                        msg.take(),
                                        priority,
                                        expiry_age_ms);
          err != qtransport::TransportError::None) {
        logger->warning << "Published object delayed due to enqueue error "
                        << static_cast<unsigned>(err) << std::flush;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
    }

  }
}


void
QuicRClientRawSession::publishNamedObject(
        const quicr::Name& quicr_name,
        uint8_t priority,
        uint16_t expiry_age_ms,
        ObjectDeliveryMode delivery_mode,
        bytes&& data) {
    // Warning: This is assuming the name always has groupId and objectId and they are
    // awlays at a specific location. All this needs to configurable or hidden inside
    // the name library driven by the application.

    // retrieve the publisher context
    auto found = publish_state.find(quicr_name);

    if (found == publish_state.end()) {
        logger->info << "No publish intent for '" << quicr_name
                     << "' missing, dropping" << std::flush;
        return;
    }

    auto &[ns, context] = *found;
    if (context.state != PublishContext::State::Ready) {
        context.transport_context_id = transport_context_id;
        context.state = PublishContext::State::Ready;
        logger->info << "Adding new context for published ns: " << ns << std::flush;
    } else {
        auto gap_log = gap_check(true, quicr_name,
                                 context.last_group_id, context.last_object_id);

        if (!gap_log.empty()) {
            logger->Log(gap_log);
        }
    }

    auto group = quicr_name.bits<uint16_t>(16, 48);
    auto object = quicr_name.bits<uint16_t>(0, 16);
    auto stream_id = qtransport::StreamId{0};
    auto &delivery_context = context.delivery_context;
    bool reliable_transport = true;

    if (delivery_mode == ObjectDeliveryMode::Object) {
        if (delivery_context.stream_id_per_object.contains(object)) {
            logger->warning << "Not publishing object, already in map" << quicr_name
                            << std::flush;
            return;
        }
        stream_id = transport->createStream(transport_context_id, true);
        delivery_context.stream_id_per_object.insert({object, stream_id});
    } else if (delivery_mode == ObjectDeliveryMode::Group) {
        if (!delivery_context.stream_id_per_group.contains(group)) {
            logger->info << "Adding delivery context for new group " << quicr_name
                         << ", " << group << std::flush;

            stream_id = transport->createStream(transport_context_id, true);
            delivery_context.stream_id_per_group[group] = stream_id;
        }
        stream_id = delivery_context.stream_id_per_group[group];
    } else if (delivery_mode == ObjectDeliveryMode::Track) {
        if (!delivery_context.stream_id_per_name.contains(quicr_name)) {
            logger->info << "Adding delivery context for new Name " << quicr_name
                         << std::flush;
            stream_id = transport->createStream(transport_context_id, true);
            delivery_context.stream_id_per_name[quicr_name] = stream_id;
        }
        stream_id = delivery_context.stream_id_per_name[quicr_name];
    } else if (delivery_mode == ObjectDeliveryMode::Datagram) {
        stream_id = transport_dgram_stream_id;
        reliable_transport = false;
    } else {
        logger->warning << "Unsupported delivery mode " << (int) delivery_mode << ", for name "
                        << quicr_name << std::flush;
        return;
    }


    messages::PublishDatagram datagram;
    datagram.header.name = quicr_name;
    datagram.header.forwarding_preference = (uint8_t )delivery_mode;
    datagram.header.offset_and_fin = 1ULL;

    if (data.size() <= quicr::MAX_TRANSPORT_DATA_SIZE || reliable_transport) {
        // no fragmentation
        messages::MessageBuffer msg;
        datagram.media_data_length = data.size();
        datagram.media_data = std::move(data);
        msg << datagram;
        logger->info << "Sending data over stream_id " << stream_id << std::flush;
        transport->enqueue(transport_context_id,
                           stream_id,
                           msg.take(),
                           priority,
                           expiry_age_ms);
        return;
    }

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

        if (transport->enqueue(transport_context_id,
                               stream_id,
                               msg.take(),
                               priority,
                               expiry_age_ms) !=
            qtransport::TransportError::None) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            // No point in finishing fragment if one is dropped
            return;
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

        if (auto err = transport->enqueue(transport_context_id,
                                          stream_id,
                                          msg.take(),
                                          priority,
                                          expiry_age_ms);
                err != qtransport::TransportError::None) {
            logger->warning << "Published object delayed due to enqueue error "
                            << static_cast<unsigned>(err) << std::flush;
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }


}



/*===========================================================================*/
// Internal Helper Methods
/*===========================================================================*/

void
QuicRClientRawSession::removeSubscription(
  const quicr::Namespace& quicr_namespace,
  const SubscribeResult::SubscribeStatus& reason)
{
  auto state_it = subscribe_state.find(quicr_namespace);
  if (state_it != subscribe_state.end()) {
    if (state_it->second.transport_stream_id > 1) {
      transport->closeStream(transport_context_id, state_it->second.transport_stream_id);
    }

    subscribe_state.erase(state_it);
  }

  if (!!sub_delegates.count(quicr_namespace)) {
    if (auto sub_delegate = sub_delegates[quicr_namespace])
      sub_delegate->onSubscriptionEnded(quicr_namespace, reason);

    sub_delegates.erase(quicr_namespace);
  }
}

bool
QuicRClientRawSession::notify_pub_fragment(
  const messages::PublishDatagram& datagram,
  const std::weak_ptr<SubscriberDelegate>& delegate,
  const std::map<int, bytes>& buffer)
{
  if ((buffer.rbegin()->first & 0x1) != 0x1) {
    return false; // Not complete, return false that this can NOT be deleted
  }

  bytes reassembled;
  int seq_bytes = 0;
  for (const auto& [sequence_num, data] : buffer) {
    if ((sequence_num >> 1) - seq_bytes != 0) {
      // Gap in offsets, missing data, return false that this can NOT be deleted
      return false;
    }

    reassembled.insert(reassembled.end(),
                       std::make_move_iterator(data.begin()),
                       std::make_move_iterator(data.end()));

    seq_bytes += data.size();
  }

  if (auto sub_delegate = delegate.lock())
    sub_delegate->onSubscribedObject(
      datagram.header.name, 0x0, 0x0, false, std::move(reassembled));

  return true;
}

void
QuicRClientRawSession::handle_pub_fragment(
  messages::PublishDatagram&& datagram,
  const std::weak_ptr<SubscriberDelegate>& delegate)
{
  static unsigned int cindex = 1;

  // Check the current index first considering it's likely in the current buffer
  const auto& msg_iter = fragments[cindex].find(datagram.header.name);
  if (msg_iter != fragments[cindex].end()) {
    // Found
    auto& [_, buffer] = *msg_iter;
    buffer.emplace(datagram.header.offset_and_fin,
                   std::move(datagram.media_data));
    if (notify_pub_fragment(datagram, delegate, buffer))
      fragments[cindex].erase(msg_iter);
  } else {
    // Not in current buffer, search all buffers
    for (auto& buf : fragments) {
      const auto& msg_iter = buf.second.find(datagram.header.name);
      if (msg_iter == buf.second.end()) {
        // If not found in any buffer, then add to current buffer
        fragments[cindex][datagram.header.name].emplace(
          datagram.header.offset_and_fin, std::move(datagram.media_data));
        continue;
      }

      // Found
      msg_iter->second.emplace(datagram.header.offset_and_fin,
                               std::move(datagram.media_data));
      if (notify_pub_fragment(datagram, delegate, msg_iter->second)) {
        buf.second.erase(msg_iter);
      }
      break;
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
QuicRClientRawSession::handle(messages::MessageBuffer&& msg)
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
        auto& context = subscribe_state[response.quicr_namespace];
        context.state = SubscribeContext::State::Ready;

        if (auto sub_delegate = sub_delegates[response.quicr_namespace])
          sub_delegate->onSubscribeResponse(response.quicr_namespace, result);
      } else {
        std::cout << "Got SubscribeResponse: No delegate found for namespace"
                  << response.quicr_namespace << std::endl;
      }

      break;
    }

    case messages::MessageType::SubscribeEnd: {
      messages::SubscribeEnd subEnd;
      msg >> subEnd;

      std::lock_guard<std::mutex> _(session_mutex);
      removeSubscription(subEnd.quicr_namespace, subEnd.reason);
      break;
    }

    case messages::MessageType::Publish: {
      messages::PublishDatagram datagram;
      msg >> datagram;

      if (auto found = sub_delegates.find(datagram.header.name);
          found != sub_delegates.end()) {
        const auto& [ns, delegate] = *found;

        auto& context = subscribe_state[ns];

        auto gap_log = gap_check(false, datagram.header.name,
                                 context.last_group_id, context.last_object_id);

        if (!gap_log.empty()) {
          logger->Log(gap_log);
        }

        if (datagram.header.offset_and_fin == uintVar_t(0x1)) {
          // No-fragment, process as single object

          if (auto sub_delegate = delegate) {
            sub_delegate->onSubscribedObject(datagram.header.name,
                                             0x0,
                                             0x0,
                                             false,
                                             std::move(datagram.media_data));
          }
        } else { // is a fragment
          handle_pub_fragment(std::move(datagram), delegate);
        }
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
      std::cout << "onPublishIntentResponse" << std::endl;
      if (auto delegate = pub_delegates[response.quicr_namespace]) {
        PublishIntentResult result{ .status = response.response };
          std::cout << "onPublishIntentResponse: Status " <<  (int) result.status << std::endl;
          delegate->onPublishIntentResponse(response.quicr_namespace, result);
      }

      break;
    }

    default:
      break;
  }
}


void
QuicRClientRawSession::handle_moq(messages::MessageBuffer&& msg)
{
    if (msg.empty()) {
        std::cout << "Transport Reported Empty Data" << std::endl;
        return;
    }

    auto msg_type = msg.front();
    switch (msg_type) {
        case messages::MESSAGE_TYPE_SUBSCRIBE_OK: {
            messages::MoqSubscribeOk sub_ok;
            msg >> sub_ok;
            auto str_qn = sub_ok.track.substr(6, sub_ok.track.length());
            auto qn = quicr::Name(str_qn);
            auto qns = name_to_ns[qn];
            ftn_tid_map[sub_ok.track] = sub_ok.track_id;
            tid_ftn_map[sub_ok.track_id] = sub_ok.track;
            if (sub_delegates.count(qns)) {
                auto &context = subscribe_state[qns];
                context.state = SubscribeContext::State::Ready;
                if (auto sub_delegate = sub_delegates[qns]) {
                    sub_delegate->onSubscribeResponse(qns, {});
                }
            } else {
                std::cout << "Got SubscribeResponse: No delegate found for namespace"
                          << qns << std::endl;
            }

            break;
        }
        default:
            break;
    }

}


} // namespace quicr
