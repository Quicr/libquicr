/*
 *  quicr_client_h3_connection.cpp
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      This file implements a H3ClientConnection object that is used for
 *      communication over a single QUIC connection.
 *
 *      NOTES: Client might abuse SUBSCRIBE as a keep-alive, which is not
 *             needed for H3 since there is a transport-level keep-alive.
 *             Just suppress those redundant SUBSCRIBE requests.
 *
 *  Portability Issues:
 *      None.
 *
 */

#include <chrono>
#include <iostream>
#include <limits>
#include <new>
#include <string_view>
#include "quicr_client_h3_connection.h"
#include "cantina/logger_macros.h"
#include "quiche_api_lock.h"
#include "quicr/encode.h"
#include "quicr/message_buffer.h"
#include "h3_common.h"

namespace quicr::h3 {

/*
 *  H3ClientConnection::H3ClientConnection()
 *
 *  Description:
 *      Constructor for the H3ClientConnection object.
 *
 *  Parameters:
 *      parent_logger [in]
 *          Parent logger object.
 *
 *      timer_manager [in]
 *          A pointer to the TimerManager object.
 *
 *      transport [in]
 *          Transport object used to send data.
 *
 *      stream_context [in]
 *          Transport context and stream identifier.
 *
 *      pub_sub_registry [in]
 *          A pointer to the Pub/Sub registry to record publications and
 *          subscription data.
 *
 *      max_send_size [in]
 *          Maximum size of data packets to transmit.
 *
 *      max_recv_size [in]
 *          Maximum size of data packets to receive.
 *
 *      use_datagrams [in]
 *          Use datagrams if peer supports them.
 *
 *      local_cid [in]
 *          Local connection ID.
 *
 *      local_address [in]
 *          Local client address.
 *
 *      remote_address [in]
 *          Remote client address.
 *
 *      quiche_connection [in]
 *          Quiche connection data; this object will take ownership of this
 *          data and destroy it in the destructor.
 *
 *      heartbeat_interval [in]
 *          Period in milliseconds with which ping messages should be sent so
 *          that connections remain up without actual traffic.
 *
 *      closure_callback [in]
 *          Function to call when the connection is closing.
 *
 *      hostname [in]
 *          Name of remote host (used in https://[hostname]/).
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
H3ClientConnection::H3ClientConnection(
  const cantina::LoggerPointer& parent_logger,
  const cantina::TimerManagerPointer& timer_manager,
  const cantina::AsyncRequestsPointer& async_requests,
  const TransportPointer& transport,
  const StreamContext& stream_context,
  const PubSubRegistryPointer& pub_sub_registry,
  std::size_t max_send_size,
  std::size_t max_recv_size,
  bool use_datagrams,
  const QUICConnectionID& local_cid,
  const cantina::NetworkAddress& local_address,
  const cantina::NetworkAddress& remote_address,
  quiche_conn* quiche_connection,
  std::uint64_t heartbeat_interval,
  const ClosureCallback closure_callback,
  const std::string& hostname)
  : H3ConnectionBase(parent_logger,
                     timer_manager,
                     async_requests,
                     transport,
                     stream_context,
                     pub_sub_registry,
                     max_send_size,
                     max_recv_size,
                     use_datagrams,
                     local_cid,
                     local_address,
                     remote_address,
                     quiche_connection,
                     heartbeat_interval,
                     closure_callback)
  , hostname{ hostname }
{
}
/*
 *  H3ClientConnection::~H3ClientConnection()
 *
 *  Description:
 *      Destructor for the H3ClientConnection object.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
H3ClientConnection::~H3ClientConnection()
{
  // Lock the client mutex
  std::unique_lock<std::mutex> lock(connection_lock);

  // Set the terminate flag
  terminate = true;

  // Make a copy of the Pub/Sub Registry to safely empty it below
  auto registrations = pub_sub_registry->FindRegistrations(local_cid);

  // Unlock the client mutex
  lock.unlock();

  // If there are publishers or subscribers remaining, let the app know they
  // are gone since the connection is terminating
  for (auto& registration : registrations) {
    if (registration.publisher) {
      PublishEndNotify(registration);
    } else {
      UnsubscribeNotify(registration);
    }
  }
}

////////////////////////////////////////////////////////////////////////////
// Functions to satisfy the QuicRClient interface
////////////////////////////////////////////////////////////////////////////

/**
 * @brief Publish intent to publish on a QUICR Namespace
 *
 * @param pub_delegate          : Publisher delegate reference
 * @param quicr_namespace       : Identifies QUICR namespace
 * @param origin_url            : Origin serving the QUICR Session
 * @param auth_token            : Auth Token to validate the Subscribe Request
 * @param payload               : Opaque payload to be forwarded to the Origin
 */
bool
H3ClientConnection::PublishIntent(
  std::shared_ptr<PublisherDelegate>& pub_delegate,
  const quicr::Namespace& quicr_namespace,
  [[maybe_unused]] const std::string& origin_url,
  [[maybe_unused]] const std::string& auth_token,
  quicr::bytes&& payload)
{
  // Lock the client mutex
  std::unique_lock<std::mutex> lock(connection_lock);

  // If the connection is not established, reject the request
  if (connection_state != H3ConnectionState::Connected) {
    logger->error << "Rejecting request since connection is not established"
                  << std::flush;
    return false;
  }

  // If already publishing under this namespace, return false
  if (pub_sub_registry->FindPublisher(quicr_namespace).has_value()) {
    return false;
  }

  try {
    auto payload_size = payload.size();
    messages::PublishIntent intent{ messages::MessageType::PublishIntent,
                                    messages::create_transaction_id(),
                                    quicr_namespace,
                                    std::move(payload),
                                    0, // TODO: What is the point of this?
                                    (using_datagrams ? 1 : 0) };

    messages::MessageBuffer msg{ sizeof(messages::PublishIntent) +
                                 payload_size };
    msg << intent;

    // Submit the HTTP request
    auto [result, stream_id] = InitiateRequest(
      "POST", std::string("/pub/") + std::string(quicr_namespace), msg);

    // If successful, record this publisher
    if (result) {
      // Record the publisher
      pub_sub_registry->Publish(
        local_cid, stream_id, quicr_namespace, stream_id, pub_delegate);

      // Emit Quiche messages
      DispatchMessages(lock);
    }

    return result;

  } catch (const std::exception& e) {
    logger->error << "Exception in publishIntent: " << e.what() << std::flush;
  } catch (...) {
    logger->error << "Exception in publishIntent" << std::flush;
  }

  return false;
}

/**
 * @brief Stop publishing on the given QUICR namespace
 *
 * @param quicr_namespace        : Identifies QUICR namespace
 * @param origin_url             : Origin serving the QUICR Session
 * @param auth_token             : Auth Token to valiadate the Subscribe
 * Request
 * @param payload                : Opaque payload to be forwarded to the
 * Origin
 */
void
H3ClientConnection::PublishIntentEnd(
  const quicr::Namespace& quicr_namespace,
  [[maybe_unused]] const std::string& auth_token)
{
  // Lock the client mutex
  std::unique_lock<std::mutex> lock(connection_lock);

  // If the connection is not established, reject the request
  if (connection_state != H3ConnectionState::Connected) {
    logger->error << "Rejecting request since connection is not established"
                  << std::flush;
    return;
  }

  // Locate the publisher record
  auto publisher = pub_sub_registry->FindPublisher(quicr_namespace);

  // If no publisher exists, log the error and return
  if (!publisher.has_value()) {
    logger->warning << "PublishIntentEnd could not find publisher"
                    << std::flush;
    return;
  }

  try {
    messages::PublishIntentEnd intent_end{
      messages::MessageType::PublishIntentEnd,
      quicr_namespace,
      {} // TODO: Figure out payload.
    };

    messages::MessageBuffer msg;
    msg << intent_end;

    // Submit the HTTP request
    InitiateRequest(
      "DELETE", std::string("/pub/") + std::string(quicr_namespace), msg);

    // Remove the publisher record
    pub_sub_registry->Expunge(publisher->identifier);

    // Emit Quiche messages
    DispatchMessages(lock);
  } catch (const std::exception& e) {
    logger->error << "Exception in publishIntentEnd: " << e.what()
                  << std::flush;
    // Remove the publisher record
    pub_sub_registry->Expunge(publisher->identifier);
  } catch (...) {
    logger->error << "Exception in PublishIntentEnd" << std::flush;
    // Remove the publisher record
    pub_sub_registry->Expunge(publisher->identifier);
  }
}

/**
 * @brief Perform subscription operation a given QUICR namespace
 *
 * @param subscriber_delegate   : Reference to receive callback for subscriber
 *                                ooperations
 * @param quicr_namespace       : Identifies QUICR namespace
 * @param subscribe_intent      : Subscribe intent to determine the start
 * point for serving the matched objects. The application may choose a
 * different intent mode, but must be aware of the effects.
 * @param origin_url            : Origin serving the QUICR Session
 * @param use_reliable_transport: Reliable or Unreliable transport
 * @param auth_token            : Auth Token to validate the Subscribe Request
 * @parm e2e_token              : Opaque token to be forwarded to the Origin
 *
 * @details Entities processing the Subscribe Request MUST validate the
 * request against the token, verify if the Origin specified in the origin_url
 *          is trusted and forward the request to the next hop Relay for that
 *          Origin or to the Origin (if it is the next hop) unless the entity
 *          itself the Origin server.
 *          It is expected for the Relays to store the subscriber state
 * mapping the subscribe context, namespaces and other relation information.
 */
void
H3ClientConnection::Subscribe(
  std::shared_ptr<SubscriberDelegate>& subscriber_delegate,
  const quicr::Namespace& quicr_namespace,
  const SubscribeIntent& intent,
  [[maybe_unused]] const std::string& origin_url,
  [[maybe_unused]] bool use_reliable_transport,
  [[maybe_unused]] const std::string& auth_token,
  [[maybe_unused]] quicr::bytes&& e2e_token)
{
  RegistryID subscriber_id = 0; // Subscriber Identifier

  // Lock the client mutex
  std::unique_lock<std::mutex> lock(connection_lock);

  // If the connection is not established, reject the request
  if (connection_state != H3ConnectionState::Connected) {
    logger->error << "Rejecting request since connection is not established"
                  << std::flush;
    return;
  }

  // See if this subscription already exists
  auto subscriber =
    pub_sub_registry->FindSubscriber(local_cid, quicr_namespace);
  if (subscriber.has_value()) {
    // Subscription already exists, so just return
    return;
  }

  try {
    // Create a subscriber record
    auto subscriber_id = pub_sub_registry->Subscribe(local_cid,
                                                     Invalid_QUIC_Stream_ID,
                                                     quicr_namespace,
                                                     0,
                                                     subscriber_delegate);

    if (subscriber_id == 0) {
      logger->warning << "Unable to create a subscriber record" << std::flush;
      return;
    }

    // encode subscribe
    messages::MessageBuffer msg{};
    messages::Subscribe subscribe{
      0x1, subscriber_id, quicr_namespace, intent
    };
    msg << subscribe;

    // Submit the HTTP request
    auto [result, stream_id] = InitiateRequest(
      "GET", std::string("/sub/") + std::string(quicr_namespace), msg);

    // If successful, record this publisher
    if (result) {
      // Update the subscriber record's stream ID
      pub_sub_registry->UpdateStreamID(subscriber_id, stream_id);

      // Emit Quiche messages
      DispatchMessages(lock);
    } else {
      pub_sub_registry->Expunge(subscriber_id);
    }
  } catch (const std::exception& e) {
    logger->error << "Exception in subscribe: " << e.what() << std::flush;
    // Remove the subscriber record
    pub_sub_registry->Expunge(subscriber_id);
  } catch (...) {
    logger->error << "Exception in subscribe" << std::flush;
    // Remove the subscriber record
    pub_sub_registry->Expunge(subscriber_id);
  }
}

/**
 * @brief Stop subscription on the given QUICR namespace
 *
 * @param quicr_namespace       : Identifies QUICR namespace
 * @param origin_url            : Origin serving the QUICR Session
 * @param auth_token            : Auth Token to validate the Subscribe
 *                                Request
 */
void
H3ClientConnection::Unsubscribe(const quicr::Namespace& quicr_namespace,
                                [[maybe_unused]] const std::string& origin_url,
                                [[maybe_unused]] const std::string& auth_token)
{
  // Lock the client mutex
  std::unique_lock<std::mutex> lock(connection_lock);

  // If the connection is not established, reject the request
  if (connection_state != H3ConnectionState::Connected) {
    logger->error << "Rejecting request since connection is not established"
                  << std::flush;
  }

  // Locate the subscriber record
  auto subscriber =
    pub_sub_registry->FindSubscriber(local_cid, quicr_namespace);

  // If no subscriber exists, log the error and return
  if (!subscriber.has_value()) {
    logger->warning << "Unsubscribe could not find subscriber" << std::flush;
    return;
  }

  try {
    messages::MessageBuffer msg{};
    messages::Unsubscribe unsub{ 0x1, quicr_namespace };
    msg << unsub;

    // Submit the HTTP request
    InitiateRequest(
      "DELETE", std::string("/sub/") + std::string(quicr_namespace), msg);

    // Remove the subscriber record
    pub_sub_registry->Expunge(subscriber->identifier);

    // Emit Quiche messages
    DispatchMessages(lock);
  } catch (const std::exception& e) {
    logger->error << "Exception in Unsubscribe: " << e.what() << std::flush;
    // Remove the subscriber record
    pub_sub_registry->Expunge(subscriber->identifier);
  } catch (...) {
    logger->error << "Exception in Unsubscribe" << std::flush;
    // Remove the subscriber record
    pub_sub_registry->Expunge(subscriber->identifier);
  }
}

/**
 * @brief Publish Named object
 *
 * @param quicr_name               : Identifies the QUICR Name for the object
 * @param priority                 : Identifies the relative priority of the
 *                                   current object
 * @param expiry_age_ms            : Time hint for the object to be in cache
 *                                      before being purged after reception
 * @param use_reliable_transport   : Indicates the preference for the object's
 *                                   transport, if forwarded.
 * @param data                     : Opaque payload
 *
 */
void
H3ClientConnection::PublishNamedObject(const quicr::Name& quicr_name,
                                       [[maybe_unused]] uint8_t priority,
                                       [[maybe_unused]] uint16_t expiry_age_ms,
                                       bool use_reliable_transport,
                                       quicr::bytes&& data)
{
  messages::MessageBuffer msg;

  // If the data is empty, don't waste time
  if (data.empty()) {
    logger->warning << "Attempt to send an empty named object" << std::flush;
    return;
  }

  // Lock the client mutex
  std::unique_lock<std::mutex> lock(connection_lock);

  // If the connection is not established, reject the request
  if (connection_state != H3ConnectionState::Connected) {
    logger->error << "Rejecting request since connection is not established"
                  << std::flush;
    return;
  }

  // Locate the publisher record, returning if not found
  auto publisher = pub_sub_registry->FindPublisher(quicr_name);
  if (!publisher.has_value()) {
    logger->warning << "PublishNamedObject could not find publisher for name "
                    << quicr_name << std::flush;
    return;
  }

  // If using datagrams and the message is large, send it in pieces
  if (!use_reliable_transport && using_datagrams &&
      (data.size() > max_send_size)) {
    PublishNamedObjectFragmented(lock, *publisher, quicr_name, std::move(data));
    return;
  }

  try {
    // Construct the datagram
    messages::PublishDatagram datagram;
    datagram.header.name = quicr_name;
    datagram.header.media_id = static_cast<uintVar_t>(publisher->identifier);
    datagram.header.group_id = static_cast<uintVar_t>(0);
    datagram.header.object_id = static_cast<uintVar_t>(0);
    datagram.header.flags = 0x0;
    datagram.header.offset_and_fin = static_cast<uintVar_t>(1);
    datagram.media_type = messages::MediaType::RealtimeMedia;

    datagram.media_data_length = static_cast<uintVar_t>(data.size());
    datagram.media_data = std::move(data);

    // Serialize the datagram
    msg << datagram;
  } catch (const std::exception& e) {
    logger->error << "PublishNamedObject exception encoding message: "
                  << e.what() << std::flush;
    return;
  } catch (...) {
    logger->error << "PublishNamedObject exception encoding message"
                  << std::flush;
    return;
  }

  // Send as a datagram?
  if (!use_reliable_transport && using_datagrams) {
    auto result = QuicheCall(quiche_h3_send_dgram,
                             http3_connection,
                             quiche_connection,
                             publisher->stream_id,
                             msg.data(),
                             msg.size());

    if (result < 0) {
      logger->warning << "Failed to send datagram" << std::flush;
      return;
    }

    // Dispatch quiche messages
    DispatchMessages(lock);

    return;
  }

  // Send the message reliably via PUT

  // Submit the HTTP PUT request
  auto [result, stream_id] = InitiateRequest(
    "PUT", std::string("/pub/") + std::string(quicr_name), msg);

  if (result) {
    // Dispatch Quiche messages
    DispatchMessages(lock);
  } else {
    logger->warning << "Failed to send published message reliably on stream "
                    << stream_id << std::flush;
  }
}

/*
 *  H3ClientConnection::PublishNamedObjectFragmented
 *
 *  Description:
 *      Publish messages by sending datagrams in fragments.
 *
 *  Parameters:
 *      lock [in]
 *          Unique lock object holding a lock to the connection mutex.  This
 *          may be unlocked by this function to facilitate dispatching messages.
 *
 *      publisher [in]
 *          The publisher record for this request.
 *
 *      quicr_name [in]
 *          The QUICR name for the published object.
 *
 *      data [in]
 *          The data to be published.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex MUST be locked by the caller.
 */
void
H3ClientConnection::PublishNamedObjectFragmented(
  std::unique_lock<std::mutex>& lock,
  const PubSubRecord& publisher,
  const quicr::Name& quicr_name,
  quicr::bytes&& data)
{
  try {
    // Construct the datagram
    messages::PublishDatagram datagram;
    datagram.header.name = quicr_name;
    datagram.header.media_id = static_cast<uintVar_t>(publisher.identifier);
    datagram.header.group_id = static_cast<uintVar_t>(0);
    datagram.header.object_id = static_cast<uintVar_t>(0);
    datagram.header.flags = 0x0;
    datagram.header.offset_and_fin = static_cast<uintVar_t>(1);
    datagram.media_type = messages::MediaType::RealtimeMedia;

    // Determine the number of fragments
    std::size_t frag_num = (data.size() / max_send_size) + 1;
    std::size_t frag_remaining_bytes = data.size() % max_send_size;

    std::size_t offset = 0;

    while (--frag_num > 0) {
      messages::MessageBuffer msg;

      if (frag_num == 1 && !frag_remaining_bytes) {
        datagram.header.offset_and_fin = (offset << 1) + 1;
      } else {
        datagram.header.offset_and_fin = offset << 1;
      }

      quicr::bytes frag_data(data.begin() + offset,
                             data.begin() + offset + max_send_size);

      datagram.media_data_length = frag_data.size();
      datagram.media_data = std::move(frag_data);

      msg << datagram;

      offset += max_send_size;

      // Send the packet to quiche
      auto result = QuicheCall(quiche_h3_send_dgram,
                               http3_connection,
                               quiche_connection,
                               publisher.stream_id,
                               msg.data(),
                               msg.size());

      if (result < 0) {
        logger->warning << "Failed to send datagram for fragment" << std::flush;
        return;
      }

      // Dispatch quiche messages
      DispatchMessages(lock);
    }

    // Send last fragment, which will be less than max_send_size
    if (frag_remaining_bytes) {
      messages::MessageBuffer msg;
      datagram.header.offset_and_fin = uintVar_t((offset << 1) + 1);

      quicr::bytes frag_data(data.begin() + offset, data.end());
      datagram.media_data_length = static_cast<uintVar_t>(frag_data.size());
      datagram.media_data = std::move(frag_data);

      msg << datagram;

      // Send the packet to quiche
      auto result = QuicheCall(quiche_h3_send_dgram,
                               http3_connection,
                               quiche_connection,
                               publisher.stream_id,
                               msg.data(),
                               msg.size());

      if (result < 0) {
        logger->warning << "Failed to send datagram for fragment" << std::flush;
        return;
      }

      // Dispatch quiche messages
      DispatchMessages(lock);
    }
  } catch (const std::exception& e) {
    logger->error << "PublishNamedObjectFragmented exception encoding message: "
                  << e.what() << std::flush;
    return;
  } catch (...) {
    logger->error << "PublishNamedObjectFragmented exception encoding message"
                  << std::flush;
    return;
  }
}

/**
 * @brief Publish Named object fragment
 *
 * @param quicr_name               : Identifies the QUICR Name for the object
 * @param priority                 : Identifies the relative priority of the
 *                                   current object
 * @param expiry_age_ms            : Time hint for the object to be in cache
                                     before being purged after reception
  * @param use_reliable_transport   : Indicates the preference for the object's
  *                                   transport, if forwarded.
  * @param offset                   : Current fragment offset
  * @param is_last_fragment         : Indicates if the current fragment is the
  * @param data                     : Opaque payload of the fragment
  */
void
H3ClientConnection::PublishNamedObjectFragment(
  [[maybe_unused]] const quicr::Name& quicr_name,
  [[maybe_unused]] uint8_t priority,
  [[maybe_unused]] uint16_t expiry_age_ms,
  [[maybe_unused]] bool use_reliable_transport,
  [[maybe_unused]] const uint64_t& offset,
  [[maybe_unused]] bool is_last_fragment,
  [[maybe_unused]] quicr::bytes&& data)
{
  // Lock the client mutex
  std::unique_lock<std::mutex> lock(connection_lock);

  // If the connection is not established, reject the request
  if (connection_state != H3ConnectionState::Connected) {
    logger->error << "Rejecting request since connection is not established"
                  << std::flush;
    return;
  }

  // TODO: Not implemented
}

////////////////////////////////////////////////////////////////////////////
// End of functions to satisfy the QuicRClient interface
////////////////////////////////////////////////////////////////////////////

/*
 *  H3ClientConnection::PublishEndNotify()
 *
 *  Description:
 *      This function notify the application that a publish has ended.
 *      This is generally due to connection termination.
 *
 *  Parameters:
 *      publisher [in]
 *          The publisher record in the pub/sub registry that is no longer
 *          publishing.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      TODO: There is really no data to be passed to the application, except
 *            the namespace.  Perhaps the server delegate needs changes?
 */
void
H3ClientConnection::PublishEndNotify(const PubSubRecord& publisher)
{
  // Remove the registry entry since there was a failure
  pub_sub_registry->Expunge(publisher.identifier);

  // Get a shared pointer to the publisher delegate
  std::shared_ptr<PublisherDelegate> publisher_delegate =
    publisher.pub_delegate.lock();
  if (!publisher_delegate) return;

  try {
    // Create an async event to issue the call to the delegate
    async_requests->Perform(
      [publisher_delegate, quicr_namespace = publisher.quicr_namespace]() {
        PublishIntentResult result{ .status = messages::Response::Fail };
        publisher_delegate->onPublishIntentResponse(quicr_namespace, result);
      });
  } catch (const std::exception& e) {
    logger->error << "PublishEndNotify failed: " << e.what() << std::flush;
  } catch (...) {
    logger->error << "PublishEndNotify failed" << std::flush;
  }
}

/*
 *  H3ClientConnection::UnsubscribeNotify()
 *
 *  Description:
 *      This function notify the application that a subscription has ended.
 *      This is generally due to connection termination.
 *
 *  Parameters:
 *      subscriber [in]
 *          The subscriber record in the pub/sub registry that is unsubscribing.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      TODO: The security token is empty and it would not be available
 *            if the connection closed abruptly.
 */
void
H3ClientConnection::UnsubscribeNotify(const PubSubRecord& subscriber)
{
  // Remove the registry entry since there was a failure
  pub_sub_registry->Expunge(subscriber.identifier);

  // Get a shared pointer to the subscriber delegate
  std::shared_ptr<SubscriberDelegate> subscriber_delegate =
    subscriber.sub_delegate.lock();
  if (!subscriber_delegate) return;

  try {
    // Create an async event to issue the call to the delegate
    async_requests->Perform(
      [subscriber_delegate, quicr_namespace = subscriber.quicr_namespace]() {
        subscriber_delegate->onSubscriptionEnded(
          quicr_namespace, SubscribeResult::SubscribeStatus::ConnectionClosed);
      });
  } catch (const std::exception& e) {
    logger->error << "UnsubscribeNotify failed: " << e.what() << std::flush;
  } catch (...) {
    logger->error << "UnsubscribeNotify failed" << std::flush;
  }
}

/*
 *  H3ClientConnection::HandleIncrementalRequestData()
 *
 *  Description:
 *      This function is called as payload data is received for a request.
 *      For most requests, the payload is handled once the request is complete.
 *      Subscriptions are different, as those might be long-lived with data
 *      trickling from the server.  This function handles such subscription
 *      data.
 *
 *  Parameters:
 *      stream_id [in]
 *          The QUIC stream ID associated with this request.
 *
 *      request [in]
 *          The RequestData object corresponding to this request.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ClientConnection::HandleIncrementalRequestData(QUICStreamID stream_id,
                                                 RequestData* request)
{
  // We're only interested in data from successful GET requests
  if (!IsSuccess(request->status_code) || (request->method != "GET")) return;

  // Create a reference to the request_body for convenience
  auto& request_body = request->request_body;

  // Process message(s)
  while (request_body.size() >= sizeof(std::uint64_t)) {
    std::uint64_t message_length = messages::swap_bytes(
      *reinterpret_cast<std::uint64_t*>(request_body.data()));

    if ((request_body.size() - sizeof(std::uint64_t)) >= message_length) {

      // Copy the full message into "message"
      quicr::messages::MessageBuffer message;
      message.push(
        { request_body.data() + sizeof(std::uint64_t), message_length });

      // Now remove the length + message from the respose_body
      request_body.erase(request_body.begin(),
                         request_body.begin() + sizeof(std::uint64_t) +
                           message_length);

      // Is this the first message for this subscription?
      if (request->state == H3RequestState::Initiated) {
        // Change request state to indicate data processing has started
        request->state = H3RequestState::Active;
        HandleSubscribeResponse(stream_id, message);
      } else {
        HandleSubscribedObject(stream_id, true, message);
      }
    } else {
      // Exit the loop, as we do not have a complete message
      break;
    }
  }
}

/*
 *  H3ClientConnection::HandleCompletedRequest()
 *
 *  Description:
 *      This function is called when once an HTTP request is "complete."
 *      For a client, this means the request is complete and no additional
 *      data is coming.
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream on which this event occurred.
 *
 *      request [in]
 *          The RequestData object corresponding to this request.
 *
 *  Returns:
 *      True to indicate the Request data should be destroyed on return.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
bool
H3ClientConnection::HandleCompletedRequest(QUICStreamID stream_id,
                                           RequestData* request)
{
  // If it's a POST, it is a PublishIntent
  if (request->method == "POST") {
    HandlePublishIntentResponse(stream_id, request);
  } else if (request->method == "GET") {
    // If the subscribe response returned an error, notify the application
    if (!IsSuccess(request->status_code)) {
      quicr::messages::MessageBuffer message(std::move(request->request_body));
      HandleSubscribeResponse(stream_id, message);
    } else {
      // This was a good subscription that has now ended
      HandleSubscribeEnded(stream_id, SubscribeResult::SubscribeStatus::Ok);
    }
  }

  return true;
}

/*
 *  H3ClientConnection::HandleH3DatagramEvent()
 *
 *  Description:
 *      This function is called when handling the QUICHE_H3_EVENT_DATAGRAM
 *      event.
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream on which this event occurred.
 *
 *      datagram [in]
 *          The received datagram.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ClientConnection::HandleReceivedDatagram(
  QUICStreamID stream_id,
  quicr::messages::MessageBuffer& datagram)
{
  // This datagram should correspond to an existing GET request
  auto request = FindRequest(stream_id);

  // Should be a stream corresponding to the GET request (i.e., subscriber)
  if (request->method != "GET") {
    logger->warning << "Received a datagram on stream " << stream_id
                    << " having flow ID " << stream_id
                    << " that does not appear to be related to a subscription"
                    << std::flush;
    return;
  }

  // Forward the published object
  HandleSubscribedObject(stream_id, false, datagram);
}

/*
 *  H3ClientConnection::InitiateRequest()
 *
 *  Description:
 *      Initiate an HTTP request.
 *
 *  Parameters:
 *      method [in]
 *          The method to employ
 *
 *      path [in]
 *          The request path associated with this request.
 *
 *      request_body [in]
 *          The request body, if any, associated with this request.
 *
 *  Returns:
 *      A pair indicating the result and associated stream for the request.
 *      The result is true if successfully sent, false if there was an error.
 *      The stream identifier is valid only if the result is successful.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
std::pair<bool, QUICStreamID>
H3ClientConnection::InitiateRequest(
  const std::string& method,
  const std::string path,
  quicr::messages::MessageBuffer& request_body)
{
  // Headers to send back in the reply
  HTTPHeaders header_map = { { ":method", method },
                             { ":scheme", "https" },
                             { ":authority", hostname },
                             { ":path", path },
                             { "user-agent", "QoH3Client" } };

  quiche_h3_header* headers =
    new (std::nothrow) quiche_h3_header[header_map.size()];
  if (headers == nullptr) {
    logger->critical << "Failed to allocate memory" << std::flush;
    return { false, {} };
  }

  // Put contents of header map into allocated structure array
  {
    std::size_t i = 0;
    for (auto const& item : header_map) {
      headers[i].name =
        reinterpret_cast<const std::uint8_t*>(item.first.c_str());
      headers[i].name_len = item.first.size();

      headers[i].value =
        reinterpret_cast<const std::uint8_t*>(item.second.c_str());
      headers[i].value_len = item.second.size();

      i++;
    }
  }

  // Send the HTTP request
  std::int64_t request_stream = QuicheCall(quiche_h3_send_request,
                                           http3_connection,
                                           quiche_connection,
                                           headers,
                                           header_map.size(),
                                           request_body.empty());
  if (request_stream < 0) {
    logger->error << "Failed to form HTTP request" << std::flush;
  } else {
    if (!request_body.empty()) {
      SendMessageBody(request_stream, request_body, true);
    }
  }

  // If successful, record the request
  if (request_stream >= 0) {
    LOGGER_DEBUG(logger,
                 "[stream " << request_stream << "] Sending request: " << method
                            << " " << path);

    // Record the request
    requests[request_stream] = {
      H3RequestState::Initiated, method, path, 0, {}, {}
    };
  } else {
    // Tear down the connection if there was an error
    InitiateConnectionClosure();
  }

  delete[] headers;

  return { request_stream >= 0, request_stream };
}

/*
 *  H3ClientConnection::HandleSubscribeResponse()
 *
 *  Description:
 *      This function is called to handle the response to a subscription
 *      request.
 *
 *  Parameters:
 *      stream_id [in]
 *          The QUIC stream ID associated with this response.
 *
 *      message [in]
 *          This is a buffer containing the subscribe response message.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ClientConnection::HandleSubscribeResponse(
  QUICStreamID stream_id,
  quicr::messages::MessageBuffer& message)
{
  try {
    messages::SubscribeResponse response;
    message >> response;

    SubscribeResult result{ .status = response.response };

    // Locate the subscriber record
    auto subscriber = pub_sub_registry->FindSubscriber(local_cid, stream_id);

    // If no subscriber exists, log the error and return
    if (!subscriber.has_value()) {
      logger->warning << "Received a subscription response, but could not find "
                         "the subscriber"
                      << std::flush;
      return;
    }

    // Verify that the stream identifier aligns
    if (subscriber->quicr_namespace != response.quicr_namespace) {
      logger->warning
        << "Received a subscribe response having the wrong quicr namespace"
        << std::flush;
      return;
    }

    // Get a shared pointer to the subscriber delegate
    std::shared_ptr<SubscriberDelegate> subscriber_delegate =
      subscriber->sub_delegate.lock();
    if (!subscriber_delegate) {
      logger->warning << "Unable to get the subscriber delegate" << std::flush;
      return;
    }

    // Place a callback to deliver the publish intent response
    async_requests->Perform([subscriber_delegate,
                             result,
                             quicr_namespace = response.quicr_namespace]() {
      subscriber_delegate->onSubscribeResponse(quicr_namespace, result);
    });

    // If the response is not OK, then expunge the subscriber record
    if (result.status != SubscribeResult::SubscribeStatus::Ok) {
      pub_sub_registry->Expunge(subscriber->identifier);
    }
  } catch (const std::exception& e) {
    logger->error << "Exception in HandleSubscribeResponse: " << e.what()
                  << std::flush;
  } catch (...) {
    logger->error << "Exception in HandleSubscribeResponse" << std::flush;
  }
}

/*
 *  H3ClientConnection::HandleSubscribedObject()
 *
 *  Description:
 *      This function is called to handle the receipt of a published object.
 *
 *  Parameters:
 *      stream_id [in]
 *          The QUIC stream ID associated with the subscription.  Note that this
 *          is not necessarily the stream on which data arrives.  For datagrams,
 *          the stream is always 0.  However, the Quiche datagrams will be
 *          prefixed with a "flow ID" that does represent the correct stream ID
 *          of the subscription.
 *
 *      reliable_transport [in]
 *          Did this object arrive over a reliable transport?
 *
 *      object [in]
 *          This is a message buffer containing the published object.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ClientConnection::HandleSubscribedObject(
  QUICStreamID stream_id,
  bool reliable_transport,
  quicr::messages::MessageBuffer& object)
{
  try {
    // Locate the subscriber record
    auto subscriber = pub_sub_registry->FindSubscriber(local_cid, stream_id);

    // If no subscriber exists, log the error and return
    if (!subscriber.has_value()) {
      logger->warning << "Received a published object, but could not find "
                         "the subscriber"
                      << std::flush;
      return;
    }

    // Get a shared pointer to the subscriber delegate
    std::shared_ptr<SubscriberDelegate> subscriber_delegate =
      subscriber->sub_delegate.lock();
    if (!subscriber_delegate) {
      logger->warning << "Unable to get the subscriber delegate" << std::flush;
      return;
    }

    // Process the received message
    messages::PublishDatagram datagram;
    object >> datagram;

    // Ensure there is some data
    if (datagram.media_data.empty()) {
      logger->warning << "HandleSubscribedObject received an empty object"
                      << std::flush;
      return;
    }

    // If this is a fragment received via an unreliable transport, feed it to
    // the fragment assembler
    if (!reliable_transport &&
        (datagram.header.offset_and_fin != uintVar_t(0x1))) {
      datagram.media_data = fragment_assembler.ConsumeFragment(datagram);
      if (datagram.media_data.empty()) return;
    }

    // Place a callback to deliver the publish intent response
    async_requests->Perform([subscriber_delegate,
                             reliable_transport,
                             datagram = std::move(datagram)]() mutable {
      subscriber_delegate->onSubscribedObject(datagram.header.name,
                                              0x0,
                                              0x0,
                                              reliable_transport,
                                              std::move(datagram.media_data));
    });
  } catch (const std::exception& e) {
    logger->error << "Exception in HandleSubscribedObject: " << e.what()
                  << std::flush;
  } catch (...) {
    logger->error << "Exception in HandleSubscribedObject" << std::flush;
  }
}

/*
 *  H3ClientConnection::HandleSubscribeEnded()
 *
 *  Description:
 *      This function is called to deliver notifications to the client that
 *      a subscription has ended.
 *
 *  Parameters:
 *      stream_id [in]
 *          The QUIC stream ID associated with this response.
 *
 *      reason [in]
 *          The reason the subscription ended
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ClientConnection::HandleSubscribeEnded(
  QUICStreamID stream_id,
  SubscribeResult::SubscribeStatus reason)
{
  try {
    // Locate the subscriber record
    auto subscriber = pub_sub_registry->FindSubscriber(local_cid, stream_id);

    // If no subscriber exists, log the error and return
    if (!subscriber.has_value()) {
      logger->warning << "Received a subscription ended, but could not "
                         "find the subscriber"
                      << std::flush;
      return;
    }

    // Verify that the stream identifier aligns
    if (subscriber->stream_id != stream_id) {
      logger->warning << "Received a subscribe ended on the wrong stream"
                      << std::flush;
      return;
    }

    // Get a shared pointer to the subscriber delegate
    std::shared_ptr<SubscriberDelegate> subscriber_delegate =
      subscriber->sub_delegate.lock();
    if (!subscriber_delegate) {
      logger->warning << "Unable to get the subscriber delegate" << std::flush;
      return;
    }

    // Place a callback to deliver the publish intent response
    async_requests->Perform([subscriber_delegate,
                             reason,
                             quicr_namespace = subscriber->quicr_namespace]() {
      subscriber_delegate->onSubscriptionEnded(quicr_namespace, reason);
    });

    // Remove the subscription
    pub_sub_registry->Expunge(subscriber->identifier);

  } catch (const std::exception& e) {
    logger->error << "Exception in HandleSubscribeEnded: " << e.what()
                  << std::flush;
  } catch (...) {
    logger->error << "Exception in HandleSubscribeEnded" << std::flush;
  }
}

/*
 *  H3ClientConnection::HandlePublishIntentResponse()
 *
 *  Description:
 *      This function is called for a response to the publish intent.
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream on which this event occurred.
 *
 *      request [in]
 *          The RequestData object corresponding to this request.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ClientConnection::HandlePublishIntentResponse(QUICStreamID stream_id,
                                                RequestData* request)
{
  try {
    // Move the message body into msg
    messages::MessageBuffer msg(std::move(request->request_body));

    // Formulate a response back to the client
    messages::PublishIntentResponse response;
    msg >> response;

    // Locate the publisher record
    auto publisher = pub_sub_registry->FindPublisher(response.quicr_namespace);

    // If no publisher exists, log the error and return
    if (!publisher.has_value()) {
      logger->warning << "Received a publish intent response, but could not "
                         "match the quicr_namespace with a publisher"
                      << std::flush;
      return;
    }

    // Verify that the stream identifier aligns
    if (publisher->stream_id != stream_id) {
      logger->warning
        << "Received a publish intent response received on the wrong stream"
        << std::flush;
      return;
    }

    // Get a shared pointer to the publisher delegate
    std::shared_ptr<PublisherDelegate> publisher_delegate =
      publisher->pub_delegate.lock();
    if (!publisher_delegate) {
      logger->warning << "Unable to get the publisher delegate" << std::flush;
      return;
    }

    // Place a callback to deliver the publish intent response
    async_requests->Perform(
      [publisher_delegate, response = std::move(response)]() {
        PublishIntentResult result{ .status = response.response };
        publisher_delegate->onPublishIntentResponse(response.quicr_namespace,
                                                    result);
      });

    // If the result is not successful, remove the publisher record
    if (!IsSuccess(request->status_code)) {
      pub_sub_registry->Expunge(publisher->identifier);
    }
  } catch (const std::exception& e) {
    logger->error << "Exception in HandlePublishIntentResponse: " << e.what()
                  << std::flush;
  } catch (...) {
    logger->error << "Exception in HandlePublishIntentResponse" << std::flush;
  }
}

/*
 *  H3ClientConnection::InitiateConnectionClosure()
 *
 *  Description:
 *      This function will initiate a graceful connection closure.
 *
 *  Parameters:
 *      Nome.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
bool
H3ClientConnection::InitiateConnectionClosure()
{
  logger->info << "Initiating connection closure" << std::flush;

  connection_state = H3ConnectionState::Disconnected;

  return (
    QuicheCall(quiche_conn_close, quiche_connection, true, 0, nullptr, 0) == 0);
}

} // namespace quicr::h3
