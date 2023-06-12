/*
 *  h3_server_connection.cpp
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      This file implements a H3ServerConnection object that is used for
 *      communication over a single QUIC connection.
 *
 *      There are three types of valid requests:
 *          - POST /path - Create a new space (space name is /path)
 *          - PUT /path - Post a message to a space
 *          - DELETE /path - Destroy a space
 *          - GET /path - Subscribe to a space
 *
 *      Response codes:
 *          POST:   201 if successful
 *                  419 if request expired
 *                  405 if request failed
 *                  303 if redirected (not implemented)
 *                  400 if the request was unacceptable
 *          PUT:    200 if successful
 *                  400 if the request was unacceptable
 *                  404 if space does not exist
 *          DELETE: 200 if the publish was cancelled
 *                  400 if the request was unacceptable
 *                  404 if the publisher was not found
 *          GET:    200 if successful
 *                  400 if the request was unacceptable
 *                  404 if the space does not exist
 *
 *      And other request type will result in 501 being returned.
 *      Server errors will result in a 500 status code being returned.
 *
 *      TODO: Stream closures for long-lived connections (e.g., closing the
 *            GET request) is not presently acted up. That should result in
 *            cancelling the subscription. Further, if a stream is closed
 *            before a response is delivered, that can also cause client/server
 *            sync issues.
 *
 *  Portability Issues:
 *      None.
 */

#include <algorithm>
#include <cctype>
#include <chrono>
#include "cantina/data_buffer.h"
#include "cantina/logger_macros.h"
#include "quicr_server_h3_connection.h"
#include "quiche_api_lock.h"
#include "quicr/quicr_name.h"
#include "quicr/quicr_common.h"
#include "quicr/encode.h"

namespace quicr {

/*
 *  H3ServerConnection::H3ServerConnection()
 *
 *  Description:
 *      Constructor for the H3ServerConnection object.
 *
 *  Parameters:
 *      parent_logger [in]
 *          Parent logger object.
 *
 *      timer_manager [in]
 *          A pointer to the TimerManager object.
 *
 *      network [in]
 *          Network object used to send data.
 *
 *      pub_sub_registry [in]
 *          A pointer to the Pub/Sub registry to record publications and
 *          subscription data.
 *
 *      server_delegate [in]
 *          A reference to the server delegate to which callbacks are made.
 *
 *      max_packet_size [in]
 *          Maximum size of data packets to transmit.
 *
 *      local_cid [in]
 *          Local (server) connection ID.
 *
 *      remote_cid [in]
 *          Remote (client) connection ID.
 *
 *      local_address [in]
 *          Local server address.
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
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
H3ServerConnection::H3ServerConnection(
  const cantina::LoggerPointer& parent_logger,
  const cantina::TimerManagerPointer& timer_manager,
  const cantina::AsyncRequestsPointer& async_requests,
  const cantina::NetworkPointer& network,
  const PubSubRegistryPointer& pub_sub_registry,
  ServerDelegate& server_delegate,
  socket_t data_socket,
  std::size_t max_packet_size,
  const QUICConnectionID& local_cid,
  const QUICConnectionID& remote_cid,
  const cantina::NetworkAddress& local_address,
  quiche_conn* quiche_connection,
  std::uint64_t heartbeat_interval,
  const ClosureCallback& closure_callback)
  : terminate{ false }
  , logger{ std::make_shared<cantina::Logger>(std::string("CNCT:") +
                                                local_cid.SuffixString(),
                                              parent_logger) }
  , timer_manager{ timer_manager }
  , async_requests{ async_requests }
  , network{ network }
  , pub_sub_registry{ pub_sub_registry }
  , server_delegate{ server_delegate }
  , data_socket{ data_socket }
  , max_packet_size{ max_packet_size }
  , using_datagrams{ false }
  , local_cid{ local_cid }
  , remote_cid{ remote_cid }
  , local_address{ local_address }
  , quiche_connection{ quiche_connection }
  , closure_callback{ closure_callback }
  , closure_notification{ false }
  , http3_config{ nullptr }
  , http3_connection{ nullptr }
  , settings_received{ false }
  , timer_create_pending{ false }
  , heartbeat_timer{ 0 }
{
  logger->info << "Created connection " << local_cid << std::flush;

  // Lock the connection object to prevent threads from entering until ready
  std::lock_guard<std::mutex> lock(connection_lock);

  // Create the HTTP/3 configuration structure
  if ((http3_config = quiche_h3_config_new()) == nullptr) {
    throw H3ServerConnectionException("Failed to create HTTP/3 "
                                      "configuration");
  }

  // Create a timer to kick off message processing and send PINGs
  heartbeat_timer = timer_manager->CreateTimer(
    std::chrono::milliseconds(0),
    [&](cantina::TimerID) {
      QuicheCall(quiche_conn_send_ack_eliciting, this->quiche_connection);
      std::unique_lock<std::mutex> lock(connection_lock);
      DispatchMessages(lock);
    },
    std::chrono::milliseconds(heartbeat_interval));
}

/*
 *  H3ServerConnection::~H3ServerConnection()
 *
 *  Description:
 *      Destructor for the H3ServerConnection object.
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
H3ServerConnection::~H3ServerConnection()
{
  logger->info << "Deleting connection: " << local_cid << std::flush;

  // Set the terminate flag
  std::unique_lock<std::mutex> lock(connection_lock);
  terminate = true;
  auto registrations = pub_sub_registry->FindRegistrations(local_cid);
  lock.unlock();

  // If there are publishers or subscribers remaining, let the app know they
  // are gone since the connection is terminating
  for (const auto& registration : registrations)
  {
    if (registration.publisher)
    {
      PublishEndNotify(registration);
    }
    else
    {
      UnsubscribeNotify(registration);
    }
  }

  // Terminate the timer(s), if any are running
  timer_manager->CancelTimer(heartbeat_timer);
  for (auto timer : timeout_timers) timer_manager->CancelTimer(timer);

  // If the connection is not closed, close it
  if (!IsConnectionClosed()) {
    QuicheCall(quiche_conn_close, quiche_connection, false, 0x01, nullptr, 0);
  }

  // Free the HTTP3 connection structure
  if (http3_connection) QuicheCall(quiche_h3_conn_free, http3_connection);

  // Free the HTTP/3 configuration structure
  QuicheCall(quiche_h3_config_free, http3_config);

  // Destroy the Quiche connection
  QuicheCall(quiche_conn_free, quiche_connection);

  // Perhaps redundant, but ensure all records for this connection are expunged
  pub_sub_registry->Expunge(local_cid);

  logger->info << "Deleted connection " << local_cid << std::flush;
}

/*
 *  H3ServerConnection::ProcessPacket()
 *
 *  Description:
 *      Process an incoming data packet.
 *
 *  Parameters:
 *      data_packet [in]
 *          Data packet received for this connection.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
H3ServerConnection::ProcessPacket(cantina::DataPacket& data_packet)
{
  static bool processing = false;

  // Grab the connection lock
  std::unique_lock<std::mutex> lock(connection_lock);

  // Place the packet on the incoming packet deque
  incoming_packets.emplace_back(std::move(data_packet));

  // Just return if there is a worker thread here
  if (processing == true) return;

  // Indicate this thread is processing
  processing = true;

  // Loop until the deque is exhausted
  while (!incoming_packets.empty()) {
    // Move the packet contents into a new DataPacket object
    cantina::DataPacket current_packet(std::move(incoming_packets.front()));

    // Pop the DataPacket from the deque
    incoming_packets.pop_front();

    // Unlock the mutex
    lock.unlock();

    try {
      // Process the packet
      ConsumePacket(current_packet);
    } catch (const std::exception& e) {
      logger->error << "Error processing incoming packet: " << e.what()
                    << std::flush;
    } catch (...) {
      logger->error << "Error processing incoming packet" << std::flush;
    }

    // Re-lock the mutex
    lock.lock();
  }

  // Indicate that no thread is currently processing
  processing = false;
}

/*
 *  H3ServerConnection::ConsumePacket()
 *
 *  Description:
 *      Consume the next incoming data packet.
 *
 *  Parameters:
 *      data_packet [in]
 *          Data packet received for this connection.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      This function must be called serially since Quiche does not tolerate
 *      feeding new incoming packets while also processing packets.
 */
void
H3ServerConnection::ConsumePacket(cantina::DataPacket& data_packet)
{
  // Have Quiche consume the data packet
  if (!QuicheConsumeData(data_packet)) return;

  // Grab the connection lock
  std::unique_lock<std::mutex> lock(connection_lock);

  // Handle HTTP/3 data (if the connection is established)
  if (IsConnectionEstablished() && !closure_notification) {
    bool connection_error = false;

    // Utilize just one thread to process events serially
    try {
      // A connection error occurs, tear the connection down
      connection_error = !QuicheHTTP3EventHandler(lock);
    } catch (const std::exception& e) {
      connection_error = true;
      logger->error
        << "Exception thrown processing events; closing connection: "
        << e.what() << std::flush;
    } catch (...) {
      connection_error = true;
      logger->error << "Exception thrown processing events; closing connection"
                    << std::flush;
    }

    if (connection_error) {
      logger->error << "Connection error; closing connection" << std::flush;

      NotifyOnClosure(true);
    }
  }

  // Dispatch Quiche messages
  DispatchMessages(lock);
}

/*
 *  H3ServerConnection::IsConnected()
 *
 *  Description:
 *      Returns true if the connection is closed, false if not.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      True if the connection is closed, false if not.
 *
 *  Comments:
 *      None.
 */
bool
H3ServerConnection::IsConnectionClosed()
{
  return QuicheCall(quiche_conn_is_closed, quiche_connection);
}

/*
 *  H3ServerConnection::IsConnectionEstablished()
 *
 *  Description:
 *      Returns true if the connection handshake is complete and ready
 *      for sending and receiving data.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      True if the connection is handshake is complete, false if not.
 *
 *  Comments:
 *      None.
 */
bool
H3ServerConnection::IsConnectionEstablished()
{
  return QuicheCall(quiche_conn_is_established, quiche_connection);
}

/*
 *  H3ServerConnection::GetConnectionID()
 *
 *  Description:
 *      Get the local (server) connection ID for this connection.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      The server's connection ID for this QUIC connection.
 *
 *  Comments:
 *      None.
 */
QUICConnectionID
H3ServerConnection::GetConnectionID()
{
  return local_cid;
}

/*
 *  QUICConnection::QuicheConsumeData()
 *
 *  Description:
 *      This function will feed the data packet to the Quiche library.
 *
 *  Parameters:
 *      data_packet [in]
 *
 *  Returns:
 *      True if the data packet was properly consumed, false if not.
 *
 *  Comments:
 *      None.
 */
bool
H3ServerConnection::QuicheConsumeData(cantina::DataPacket& data_packet)
{
  quiche_recv_info recv_info = {
    const_cast<sockaddr*>(reinterpret_cast<const sockaddr*>(
      data_packet.GetAddress().GetAddressStorage())),
    data_packet.GetAddress().GetAddressStorageSize(),
    const_cast<sockaddr*>(
      reinterpret_cast<const sockaddr*>(local_address.GetAddressStorage())),
    local_address.GetAddressStorageSize()
  };

  // Process the data packet
  if (QuicheCall(quiche_conn_recv,
                 quiche_connection,
                 data_packet.GetBufferPointer(),
                 data_packet.GetDataLength(),
                 &recv_info) < 0) {
    logger->error << "Failure to read the data packet" << std::flush;
    return false;
  }

  return true;
}

/*
 *  QUICClientConnection::DispatchMessages()
 *
 *  Description:
 *      Handle dispatching Quiche messages.
 *
 *  Parameters:
 *      lock [in]
 *          Unique lock object holding a lock to the connection mutex.  This
 *          may be unlocked by this function to clear old timers.
 *
 *      timer_id [in]
 *          If called by an expiring timer, this will be the timer ID of the
 *          expiring timer.  Otherwise, this will have a value of zero.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
H3ServerConnection::DispatchMessages(std::unique_lock<std::mutex>& lock,
                                     cantina::TimerID timer_id)
{
  // Emit QUIC messages Quiche has prepared
  QuicheEmitQUICMessages(lock);

  // Notify if the connection is closed
  NotifyOnClosure();

  // Ensure we have a (freshened) timeout timer running
  CreateOrRefreshTimer(lock, timer_id);
}

/*
 *  H3ServerConnection::QuicheEmitQUICMessages()
 *
 *  Description:
 *      This function will emit any QUIC messages that the Quiche
 *      library has queued for transmission for this connection.
 *
 *  Parameters:
 *      lock [in]
 *          Locked mutex that will be unlocked while emitting messages.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ServerConnection::QuicheEmitQUICMessages(std::unique_lock<std::mutex>& lock)
{
  // Create the data packet for sending messages
  cantina::DataPacket data_packet(max_packet_size);

  // Unlock the mutex
  lock.unlock();

  // Loop until there are no more packets to send
  while (true) {
    quiche_send_info send_info{};

    auto size = QuicheCall(quiche_conn_send,
                           quiche_connection,
                           data_packet.GetBufferPointer(),
                           data_packet.GetBufferSize(),
                           &send_info);

    // Exit if done sending packets
    if (size == QUICHE_ERR_DONE) break;

    // Report errors and exit
    if (size < 0) {
      logger->error << "Quiche failed to form data packet" << std::flush;
      break;
    }

    // Set the packet length
    data_packet.SetDataLength(size);

    // Set the destination address
    cantina::NetworkAddress destination(&send_info.to, send_info.to_len);
    data_packet.SetAddress(destination);

    // Transmit the data packet
    if (network->SendData(data_socket, data_packet) ==
        cantina::Network::Socket_Error) {
      logger->error << "Failed to send data packet" << std::flush;
    }
  }

  // Re-lock the mutex
  lock.lock();
}

/*
 *  H3ServerConnection::NotifyOnClosure()
 *
 *  Description:
 *      Check to see if the connection was closed and, if so, call the
 *      notification callback.  It is also possible to force the connection
 *      to be closed by passing true as the only parameter.  This forces
 *      the connection to be torn down via closure_callback(), which may not
 *      result in a graceful closure.
 *
 *  Parameters:
 *      force_close [in]
 *          Force closure of the connection, even if Quiche still has it open.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ServerConnection::NotifyOnClosure(bool force_close)
{
  if ((force_close || IsConnectionClosed()) && !closure_notification) {
    // Note the callback was made
    closure_notification = true;

    // Issue closure notification callback
    closure_callback();
  }
}

/*
 *  H3ServerConnection::CreateOrRefreshTimer()
 *
 *  Description:
 *      This function is called after messages are dispatched to update
 *      the Quiche timeout timer.
 *
 *  Parameters:
 *      lock [in]
 *          Unique lock object holding a lock to the connection mutex.  This
 *          may be unlocked by this function to clear old timers.
 *
 *      timer_id [in]
 *          If called by an expiring timer, this will be the timer ID of the
 *          expiring timer.  Otherwise, this will have a value of zero.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ServerConnection::CreateOrRefreshTimer(std::unique_lock<std::mutex>& lock,
                                         cantina::TimerID timer_id)
{
  std::deque<cantina::TimerID> timeouts_to_cancel;

  // If terminating, connection closed, or timer being created, return
  if (terminate || closure_notification || timer_create_pending) return;

  // Swap the containers so that this function can cleanup timers
  std::swap(timeouts_to_cancel, timeout_timers);

  // If this thread is the current timer thread, put it back on the deque
  if (timer_id > 0) timeout_timers.push_back(timer_id);

  // Are there old timers to cancel or remove?
  if (!timeouts_to_cancel.empty()) {
    // Indicate a new timer is being created
    timer_create_pending = true;

    // Unlock the connection lock while cancelling the timer
    lock.unlock();

    // This will cleanup the timeout deque and cancel any unfired timers
    for (auto timer : timeouts_to_cancel) {
      // Cancel all timers except the current timer thread
      if (timer != timer_id) timer_manager->CancelTimer(timer);
    }

    // Re-lock the connection lock before attempting to create a new timer
    lock.lock();

    // Now indicate that no other timer creation is pending
    timer_create_pending = false;
  }

  // Just return if the object is terminating
  if (terminate) return;

  // Update connection timeout
  std::uint64_t timeout =
    QuicheCall(quiche_conn_timeout_as_nanos, quiche_connection);

  // Create a new timer
  cantina::TimerID timeout_timer = timer_manager->CreateTimer(
    std::chrono::nanoseconds(timeout),
    [&](cantina::TimerID timer_id) { TimeoutHandler(timer_id); });

  LOGGER_DEBUG(logger,
               "Created timer: " << timeout_timer << " firing in "
                                 << timeout / 1e9 << " s");

  timeout_timers.push_back(timeout_timer);
}

/*
 *  H3ServerConnection::TimeoutHandler()
 *
 *  Description:
 *      This function will handle connection-related timeouts.  Timeouts are
 *      used in to interact with the Quiche library so that it will emit
 *      packets at the desired time.
 *
 *  Parameters:
 *      time_id [in]
 *          The timer ID for this timer.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
H3ServerConnection::TimeoutHandler(cantina::TimerID timer_id)
{
  // Grab the connection lock to check for the terminate flag
  std::unique_lock<std::mutex> lock(connection_lock);

  // Just return if the connection object is in a terminating state or
  // a new timer is being created
  if (terminate || timer_create_pending) return;

  // Indicate a timeout happened
  QuicheCall(quiche_conn_on_timeout, quiche_connection);

  // Dispatch any messages Quiche has queued
  DispatchMessages(lock, timer_id);
}

/*
 *  H3ServerConnection::QuicheHTTP3EventHandler()
 *
 *  Description:
 *      This function will respond to any HTTP/3 related event.
 *
 *  Parameters:
 *      lock [in]
 *          Locked mutex that will be unlocked at times when it is safe to
 *          release the lock.
 *
 *  Returns:
 *      True if successful, false if there is a connection error
 *      that necessitates tearing down the connection.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
bool
H3ServerConnection::QuicheHTTP3EventHandler(std::unique_lock<std::mutex>& lock)
{
  LOGGER_DEBUG(logger, "Processing HTTP/3 events");

  // Create an HTTP/3 connection structure if one does not exist
  if (http3_connection == nullptr) {
    http3_connection = QuicheCall(
      quiche_h3_conn_new_with_transport, quiche_connection, http3_config);
    if (http3_connection == nullptr) {
      logger->error << "Failed to create HTTP/3 connection" << std::flush;
      return false;
    }
  }

  // Process the HTTP/3 request
  while (true) {
    quiche_h3_event* event = nullptr;

    // Attempt to read the HTTP/3 request
    auto stream = QuicheCall(
      quiche_h3_conn_poll, http3_connection, quiche_connection, &event);

    // Done processing data?
    if (stream < 0) {
      if (event != nullptr) QuicheCall(quiche_h3_event_free, event);
      break;
    }

    LOGGER_DEBUG(logger, "[stream " << stream << "] Processing event");

    // Attempt to get the connection settings
    if (!settings_received) {
      if (QuicheCall(quiche_h3_for_each_setting,
                     http3_connection,
                     H3ServerConnection::SettingsCallback,
                     this) == 0) {
        settings_received = true;

        // If requested to use datagrams, check peer support
        if (QuicheCall(quiche_h3_dgram_enabled_by_peer,
                       http3_connection,
                       quiche_connection)) {
          using_datagrams = true;
          logger->info << "Peer supports datagrams" << std::flush;
        }
      }
    }

    // What kind of event was received?
    switch (quiche_h3_event_type(event)) {
      case QUICHE_H3_EVENT_HEADERS:
        HandleH3HeadersEvent(stream, event);
        break;

      case QUICHE_H3_EVENT_DATA:
        HandleH3DataEvent(stream, event);
        break;

      case QUICHE_H3_EVENT_FINISHED:
        HandleH3FinishedEvent(stream, event);
        break;

      case QUICHE_H3_EVENT_RESET:
        HandleH3ResetEvent(stream, event);
        break;

      case QUICHE_H3_EVENT_PRIORITY_UPDATE:
        HandleH3PriorityUpdateEvent(stream, event);
        break;

      case QUICHE_H3_EVENT_DATAGRAM:
        HandleH3DatagramEvent(stream, event);
        break;

      case QUICHE_H3_EVENT_GOAWAY:
        HandleH3GoAwayEvent(stream, event);
        break;

      default:
        logger->warning << "Unexpected H3 event type: "
                        << quiche_h3_event_type(event) << std::flush;
        break;
    }

    // Unlock the mutex so as to not starve threads
    lock.unlock();

    // Free this event
    QuicheCall(quiche_h3_event_free, event);

    // Re-lock the mutex
    lock.lock();
  }

  return true;
}

/*
 *  H3ServerConnection::HandleH3HeadersEvent()
 *
 *  Description:
 *      This function is called when handling the QUICHE_H3_EVENT_HEADERS event.
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream on which this event occurred.
 *
 *      event [in]
 *          A pointer to an quiche_h3_event structure for this event.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ServerConnection::HandleH3HeadersEvent(QUICStreamID stream_id,
                                         quiche_h3_event* event)
{
  LOGGER_DEBUG(logger, "[stream " << stream_id << "] H3 - incoming headers");

  std::pair<H3ServerConnection*, QUICStreamID> stream_data{ this, stream_id };

  if (QuicheCall(quiche_h3_event_for_each_header,
                 event,
                 H3ServerConnection::HeaderCallback,
                 &stream_data)) {
    logger->error << "Failed to process headers" << std::flush;
    return;
  }
}

/*
 *  H3ServerConnection::HandleH3DataEvent()
 *
 *  Description:
 *      This function is called when handling the QUICHE_H3_EVENT_DATA event.
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream on which this event occurred.
 *
 *      event [in]
 *          A pointer to an quiche_h3_event structure for this event.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ServerConnection::HandleH3DataEvent(QUICStreamID stream_id,
                                      [[maybe_unused]] quiche_h3_event* event)
{
  std::vector<std::uint8_t> buffer(max_packet_size);

  LOGGER_DEBUG(logger, "[stream " << stream_id << "] H3 - data event");

  // Locate the request object
  auto request = FindRequest(stream_id);
  if (!request) return;

  while (true) {
    ssize_t length = QuicheCall(quiche_h3_recv_body,
                                http3_connection,
                                quiche_connection,
                                stream_id,
                                buffer.data(),
                                max_packet_size);

    if (length <= 0) break;

    // Append the octets in the request to the request body
    request->request_body.insert(
      request->request_body.end(), buffer.begin(), buffer.begin() + length);
  }

  LOGGER_DEBUG(logger,
               "[stream " << stream_id << "] Received "
                          << request->request_body.size()
                          << " octets in body");
}

/*
 *  H3ServerConnection::HandleH3FinishedEvent()
 *
 *  Description:
 *      This function is called when handling the QUICHE_H3_EVENT_FINISHED
 *      event.
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream on which this event occurred.
 *
 *      event [in]
 *          A pointer to an quiche_h3_event structure for this event.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ServerConnection::HandleH3FinishedEvent(
  QUICStreamID stream_id,
  [[maybe_unused]] quiche_h3_event* event)
{
  /*
   * Note: this event means the peer terminated the request
   *       stream. It is assumed this is a safe place to begin
   *       sending data.  However, one could start in
   *       QUICHE_H3_EVENT_HEADERS.  But what if the client sends
   *       data, too?  This seems to generally be the better
   *       place if this event always happens.
   */

  LOGGER_DEBUG(logger, "[stream " << stream_id << "] H3 - Finished");

  // Process the HTTP request
  auto [final_response, status] = ProcessRequest(stream_id);

  // Status code 100 indicates the response is pending, but nothing is signaled
  // to the client; just return for now
  if (status == 100) return;

  // Send a response back to the client
  SendHTTPResponse(stream_id, status, {}, {}, false, final_response);

  // Expunge the request if this response is final
  if (final_response) ExpungeRequest(stream_id);
}

/*
 *  H3ServerConnection::SendHTTPResponse()
 *
 *  Description:
 *      This function will emit an HTTP response to the client.  Once the
 *      response is sent, this function will also remove data from the
 *      internal maps related to the request if the stream is closed.
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream on which to send an HTTP response.
 *
 *      response_headers [in]
 *          Any HTTP headers that should be included in the response.
 *
 *      status_code [in]
 *          The status code to return
 *
 *      prefix_length [in]
 *          Prefix the message to send with the message length.  This is used
 *          for responses that may result in multiple, concatenated messages.
 *          Namely, a subscription will result in a stream of named objects.
 *
 *      close_stream [in]
 *          Indicates whether the stream should be closed after the message
 *          is delivered.  The default is to close the stream.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ServerConnection::SendHTTPResponse(QUICStreamID stream_id,
                                     unsigned status_code,
                                     const HTTPHeaders& response_headers,
                                     const cantina::OctetString& response_body,
                                     bool prefix_length,
                                     bool close_stream)
{
  // Headers to send back in every reply
  HTTPHeaders header_map = { { ":status", std::to_string(status_code) },
                             { "server", "QoH3Server" } };

  // Add additional headers provided by the caller
  for (auto& [header, value] : response_headers) header_map[header] = value;

  // Allocate memory for the headers
  quiche_h3_header* headers =
    new (std::nothrow) quiche_h3_header[header_map.size()];
  if (headers == nullptr) {
    logger->critical << "Failed to allocate memory" << std::flush;
    NotifyOnClosure(true);
    return;
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

  // Send the HTTP headers in response
  QuicheCall(quiche_h3_send_response,
             http3_connection,
             quiche_connection,
             stream_id,
             headers,
             header_map.size(),
             (close_stream ? response_body.empty() : false));

  LOGGER_DEBUG(logger, "[stream " << stream_id << "] Sending response to client");

  // Delete the headers array
  delete[] headers;

  // If there is a response body, send it
  if (!response_body.empty()) {
    if (prefix_length) {
      cantina::DataBuffer data_buffer(sizeof(std::uint64_t) +
                                      response_body.size());

      // Insert the message length and the message
      data_buffer.AppendValue(static_cast<std::uint64_t>(response_body.size()));
      data_buffer.AppendValue(response_body);

      // Transmit the response body
      QuicheCall(quiche_h3_send_body,
                 http3_connection,
                 quiche_connection,
                 stream_id,
                 data_buffer.GetBufferPointer(),
                 data_buffer.GetDataLength(),
                 close_stream);
    } else {
      // Transmit the response body
      QuicheCall(quiche_h3_send_body,
                 http3_connection,
                 quiche_connection,
                 stream_id,
                 const_cast<std::uint8_t*>(response_body.data()),
                 response_body.size(),
                 close_stream);
    }
  }
}

/*
 *  H3ServerConnection::ProcessRequest()
 *
 *  Description:
 *      This function is called from HandleH3FinishedEvent() to handle the
 *      details of the incoming request.  The valid requests are enumerated
 *      at the top of this module.
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream to be processed.
 *
 *  Returns:
 *      A pair having these components:
 *          bool - Final response?
 *          unsigned - status code to return to client
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
std::pair<bool, unsigned>
H3ServerConnection::ProcessRequest(QUICStreamID stream_id)
{
  // If terminating, short-circuit processing
  if (terminate) return { true, 503 };

  // Try to find the requested headers
  auto request = FindRequest(stream_id);

  // If not found, return failure code
  if (!request) return { true, 400 };

  // Locate the request method and path, updating the request data structure
  request->method = request->request_headers[":method"];
  std::transform(request->method.begin(),
                 request->method.end(),
                 request->method.begin(),
                 [](char c) { return std::toupper(c); });
  request->path = request->request_headers[":path"];

  // Assign the request state
  request->state = H3RequestState::Initiated;

  // If either the method or path are empty, return 400
  if (request->method.empty() || request->path.empty()) return { true, 400 };

  // Ensure the URI leads with what is expected; this should be revised later
  if ((!request->path.starts_with("/pub/0x")) &&
      (!request->path.starts_with("/sub/0x"))) {
    logger->warning << "Invalid URI in path: " << request->path << std::flush;
    return { true, 400 };
  }

  // Process the request based on the type of request
  if (request->method == "POST") {
    logger->info << "Received POST to " << request->path << std::flush;

    // Since a POST is a PublishIntent, process it accordingly
    if (request->request_body.empty()) return { true, 400 };

    // Process the PublishIntent
    if (HandlePublishIntent(stream_id, request)) return { false, 100 };

    return { true, 400 };
  }

  if (request->method == "PUT") {
    LOGGER_DEBUG(logger, "Received message for " << request->path);

    // Since a PUT is a "publish named object", process it accordingly
    if (request->request_body.empty()) return { true, 400 };

    // Forward the published message
    return { true, HandlePublishNamedObject(stream_id, request) };
  }

  if (request->method == "DELETE") {
    logger->info << "Received DELETE for " << request->path << std::flush;

    // Assumption this signals Publication Intent ends
    if (!request->path.starts_with("/pub/0x"))
    {
      return { true, HandlePublishIntentEnd(request) };
    }

    // Assumption this signals Publication Intent ends
    if (!request->path.starts_with("/sub/0x"))
    {
      return { true, HandleUnsubscribe(request) };
    }
  }

  if (request->method == "GET") {
    logger->info << "Received GET for " << request->path << std::flush;

    // Assumption is this signals a subscription request
    if (HandleSubscribe(stream_id, request)) return { false, 100 };

    return { true, 400 };
  }

  return { true, 501 };
}

/*
 *  H3ServerConnection::HandlePublishIntent()
 *
 *  Description:
 *      This function is called to handle publication requests (HTTP POST)
 *      messages.
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream on which a POST request was received.
 *
 *      request [in]
 *          A pointer to the associated RequestData object.
 *
 *  Returns:
 *      True is successful, false if there was a problem.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
bool
H3ServerConnection::HandlePublishIntent(QUICStreamID stream_id,
                                        RequestData* request)
{
  RegistryID publisher_id = 0;                  // Registry ID for publisher

  try {
    // Get the contents of the POST body
    messages::MessageBuffer msg(std::move(request->request_body));

    // Deserialize the message
    messages::PublishIntent publish_intent;
    msg >> publish_intent;

    // Record the publish intent
    publisher_id = pub_sub_registry->Publish(local_cid,
                                             stream_id,
                                             publish_intent.quicr_namespace,
                                             publish_intent.transaction_id);

    // If the registration failed, return 0
    if (publisher_id == 0)
    {
      logger->warning << "Failed to create publisher for "
                      << publish_intent.quicr_namespace << std::flush;
      return false;
    }

    logger->info << "PublishIntent for namespace "
                 << publish_intent.quicr_namespace << std::flush;

    // Create an async event to issue the call to the delegate
    async_requests->Perform([&, intent = std::move(publish_intent)]() mutable {
      server_delegate.onPublishIntent(intent.quicr_namespace,
                                      "" /* intent.origin_url */,
                                      false,
                                      "" /* intent.relay_token */,
                                      std::move(intent.payload));
    });

    return true;
  } catch (const std::exception &e) {
    logger->error << "Failed to decode Publish Intent: " << e.what()
                  << std::flush;
  } catch (...) {
    logger->error << "Failed to decode Publish Intent" << std::flush;
  }

  // Remove the registry entry since there was a failure
  if (publisher_id) pub_sub_registry->Expunge(publisher_id);

  return false;
}

/*
 *  H3ServerConnection::HandlePublishIntentEnd()
 *
 *  Description:
 *      This function is called to handle publication end requests (HTTP DELETE)
 *      messages.
 *
 *  Parameters:
 *      request [in]
 *          A pointer to the associated RequestData object.
 *
 *  Returns:
 *      HTTP status code indicating the result.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 *
 *      TODO: This is not complete.  The parameters for onPublishIntentEnd()
 *            were not fully defined as this was written, so it needs updating.
 */
unsigned
H3ServerConnection::HandlePublishIntentEnd(RequestData* request)
{
  try {
    messages::MessageBuffer msg(std::move(request->request_body));

    // Deserialize the message
    messages::PublishIntentEnd publish_intent_end;
    msg >> publish_intent_end;

    // Try to find the publisher
    auto publisher =
      pub_sub_registry->FindPublisher(publish_intent_end.quicr_namespace);
    if (!publisher.has_value()) {
      logger->warning << "Unable to find publisher for name "
                      << publish_intent_end.quicr_namespace << std::flush;
      return 404;
    }

    // Create an async event to issue the call to the delegate
    async_requests->Perform([&, pie = std::move(publish_intent_end)]() mutable {
      server_delegate.onPublishIntentEnd(
        pie.quicr_namespace, {}, std::move(pie.payload));
    });

    // Remove the publisher record
    pub_sub_registry->Expunge(publisher->identifier);

    return 200;
  } catch (const std::exception &e) {
    logger->error << "Exception in PublishIntentEnd: " << e.what()
                  << std::flush;
  } catch (...) {
    logger->error << "Exception in PublishIntentEnd" << std::flush;
  }

  return 500;
}

/*
 *  H3ServerConnection::PublishIntentResponse()
 *
 *  Description:
 *      This function is called by the application in response to the Publish
 *      Intent request delivered.  This will deliver an appropriate HTTP
 *      response back to the client.
 *
 *  Parameters:
 *      publisher [in]
 *          The publisher record from the pub/sub registry for this response.
 *
 *      result [in]
 *          The result to send to the client as delivered by the application.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
H3ServerConnection::PublishIntentResponse(const PubSubRecord& publisher,
                                          const PublishIntentResult& result)
{
  unsigned status_code;                         // HTTP status code to return

  try {
    messages::PublishIntentResponse response{
      messages::MessageType::PublishIntentResponse,
      publisher.quicr_namespace,
      result.status,
      publisher.transaction_id
    };

    messages::MessageBuffer msg(sizeof(response));
    msg << response;

    // Lock the connection object
    std::unique_lock<std::mutex> lock(connection_lock);

    // Update the response record
    RequestData* request = FindRequest(publisher.stream_id);
    if (!request) {
      logger->warning << "Request for publish intent response not found";
      return;
    }

    // Ensure the state is "Initiated"
    if (request->state != H3RequestState::Initiated) {
      logger->warning << "PublishIntentResponse found unexpected request state";
      return;
    }

    // Update the response state
    request->state = H3RequestState::Complete;

    // Determine the appropriate status code
    switch (result.status) {
      case messages::Response::Ok:
        status_code = 201;
        break;
      case messages::Response::Expired:
        status_code = 419;
        break;
      case messages::Response::Fail:
        status_code = 405;
        break;
      case messages::Response::Redirect:
        status_code = 303;
        break;
      default:
        status_code = 500;
        break;
    }

    // Send the response to the client
    SendHTTPResponse(publisher.stream_id, status_code, {}, msg.get(), false);

    // This request is now complete, so remove it
    ExpungeRequest(publisher.stream_id);

    // If the request was rejected, we remove the publisher record
    if (result.status != messages::Response::Ok)
    {
      pub_sub_registry->Expunge(publisher.identifier);
    }

    // Dispatch Quiche messages
    DispatchMessages(lock);
  } catch (const std::exception &e) {
    logger->error
      << "Unexpected error trying to process publish response message: "
      << e.what() << std::flush;
  } catch (...) {
    logger->error
      << "Unexpected error trying to process publish response message"
      << std::flush;
  }
}

/*
 *  H3ServerConnection::HandlePublishNamedObject()
 *
 *  Description:
 *      This function is called to handle objects sent via PUT.
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream on which the PUT request was received.
 *
 *      request [in]
 *          A pointer to the associated RequestData object.
 *
 *  Returns:
 *      An HTTP status code to return for the request.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 *
 *      TODO: What stream ID is the application expecting? The one for which
 *            the original publication intent was given or the one for the
 *            PUT request.  At the moment, the code sends the former since
 *            there is no other stream ID to use for datagrams.
 */
unsigned
H3ServerConnection::HandlePublishNamedObject(
  [[maybe_unused]] QUICStreamID stream_id,
  RequestData* request)
{
  try {
    // Get the contents of the PUT body
    messages::MessageBuffer msg(std::move(request->request_body));

    // Deserialize the message
    messages::PublishDatagram datagram;
    msg >> datagram;

    // Try to find the publisher
    auto publisher = pub_sub_registry->FindPublisher(datagram.header.name);
    if (!publisher.has_value())
    {
      logger->warning << "Unable to find publisher for name "
                      << datagram.header.name << std::flush;
      return 404;
    }

    // Create an async event to issue the call to the delegate
    async_requests->Perform([&, datagram = std::move(datagram)]() mutable {
      server_delegate.onPublisherObject(publisher->identifier,
                                        publisher->stream_id,
                                        true,
                                        std::move(datagram));
    });

    return 200;
  } catch (const std::exception& e) {
    logger->error << "Failed to decode published object: " << e.what()
                  << std::flush;
  } catch (...) {
    logger->error << "Failed to decode published object" << std::flush;
  }

  return 500;
}

/*
 *  H3ServerConnection::HandleSubscribe()
 *
 *  Description:
 *      This function will handle subscription requests (GET requests).
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream on which the GET request was received.
 *
 *      request [in]
 *          A pointer to the associated RequestData object.
 *
 *  Returns:
 *      True is successful, false if there was a problem.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
bool
H3ServerConnection::HandleSubscribe(QUICStreamID stream_id,
                                    RequestData* request)
{
  RegistryID subscriber_id = 0;                 // Registry ID for subscriber

  try {
    // Get the contents of the GET body
    messages::MessageBuffer msg(std::move(request->request_body));

    messages::Subscribe subscribe;
    msg >> subscribe;

    // Ensure there isn't a subscriber on this connection for this namespace
    // TODO: We should not have to perform this step.  However, since the
    //       unsubscribe message does not specify the subscriber ID, all
    //       that can be done is match on the connection ID and namespace.
    //       This would be ambiguous if multiple were allowed.
    if (pub_sub_registry->FindSubscriber(local_cid, subscribe.quicr_namespace)
          .has_value()) {
      logger->warning << "Duplicate subscription request on connection for: "
                      << subscribe.quicr_namespace << std::flush;
      return false;
    }

    // Record the subscription
    subscriber_id = pub_sub_registry->Subscribe(local_cid,
                                                stream_id,
                                                subscribe.quicr_namespace,
                                                subscribe.transaction_id);

    // If the registration failed, return 0
    if (subscriber_id == 0)
    {
      logger->warning << "Failed to create subscription for "
                      << subscribe.quicr_namespace << std::flush;
      return false;
    }

    logger->info << "Subscription for namespace " << subscribe.quicr_namespace
                 << std::flush;

    // Create an async event to issue the call to the delegate
    async_requests->Perform([&, subscribe = std::move(subscribe)]() mutable {
      server_delegate.onSubscribe(subscribe.quicr_namespace,
                                  subscriber_id,
                                  subscriber_id,
                                  stream_id,
                                  subscribe.intent,
                                  "",
                                  false, // Use reliable transport?
                                  "",
                                  {});
    });

    return true;
  } catch (const std::exception& e) {
    logger->error << "Failed to decode subscription request: " << e.what()
                  << std::flush;
  } catch (...) {
    logger->error << "Failed to decode subscription request" << std::flush;
  }

  // Remove the registry entry since there was a failure
  if (subscriber_id) pub_sub_registry->Expunge(subscriber_id);

  return false;
}

/*
 *  H3ServerConnection::HandleUnsubscribe()
 *
 *  Description:
 *      Handle requests from the client to terminate subscriptions.
 *
 *  Parameters:
 *      request [in]
 *          A pointer to the associated RequestData object.
 *
 *  Returns:
 *      True in all cases. There is no value in signaling a failed unsubscribe.
 *
 *  Comments:
 *      TODO: The security token is empty and it would not be available
 *            if the connection closed abruptly.
 */
bool
H3ServerConnection::HandleUnsubscribe(RequestData* request)
{
  PubSubRecord subscriber{};

  try {
    // Get the contents of the DELETE body
    messages::MessageBuffer msg(std::move(request->request_body));

    messages::Unsubscribe unsub;
    msg >> unsub;

    // Locate the subscriber record
    auto subscribers = pub_sub_registry->FindSubscribers(unsub.quicr_namespace);
    for (auto &item : subscribers)
    {
      if (item.connection_id == local_cid)
      {
        subscriber = item;
        break;
      }
    }

    // If the subscriber was not found, return
    if (subscriber.identifier == 0) return true;

    // Create an async event to issue the call to the delegate
    async_requests->Perform([&,
                             subscriber_id = subscriber.identifier,
                             quicr_namespace = unsub.quicr_namespace]() {
      server_delegate.onUnsubscribe(quicr_namespace, subscriber_id, {});
    });

    // Remove the subscriber from the registry
    pub_sub_registry->Expunge(subscriber.identifier);

    // Terminate the QUIC stream associated with this subscription
    QuicheCall(quiche_h3_send_body,
               http3_connection,
               quiche_connection,
               subscriber.stream_id,
               nullptr,
               0,
               true);

  } catch (const std::exception& e) {
    logger->error << "Failed to handle unsubscribe: " << e.what()
                  << std::flush;
  } catch (...) {
    logger->error << "Failed to handle unsubscribe" << std::flush;
  }

  // This should never fail
  return true;
}

/*
 *  H3ServerConnection::PublishEndNotify()
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
H3ServerConnection::PublishEndNotify(const PubSubRecord& publisher)
{
  try {
    // Create an async event to issue the call to the delegate
    async_requests->Perform([&, publisher = publisher]() {
      server_delegate.onPublishIntentEnd(
        publisher.quicr_namespace, {}, {});
    });
  } catch (const std::exception& e) {
    logger->error << "Failed to remove publisher: " << e.what()
                  << std::flush;
  } catch (...) {
    logger->error << "Failed to remove publisher" << std::flush;
  }

  // Remove the registry entry since there was a failure
  pub_sub_registry->Expunge(publisher.identifier);
}

/*
 *  H3ServerConnection::UnsubscribeNotify()
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
H3ServerConnection::UnsubscribeNotify(const PubSubRecord& subscriber)
{
  try {
    // Create an async event to issue the call to the delegate
    async_requests->Perform([&, subscriber = subscriber]() {
      server_delegate.onUnsubscribe(
        subscriber.quicr_namespace, subscriber.identifier, {});
    });
  } catch (const std::exception& e) {
    logger->error << "Failed to perform unsubscribe: " << e.what()
                  << std::flush;
  } catch (...) {
    logger->error << "Failed to perform unsubscribe" << std::flush;
  }

  // Remove the registry entry since there was a failure
  pub_sub_registry->Expunge(subscriber.identifier);
}

/*
 *  H3ServerConnection::SubscribeResponse()
 *
 *  Description:
 *      This function is called by the application in response to a subscription
 *      request.  This will deliver an appropriate HTTP response back to the
 *      client.
 *
 *  Parameters:
 *      subscriber [in]
 *          The subscriber record from the pub/sub registry for this response.
 *
 *      result [in]
 *          The result to send to the client as delivered by the application.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
H3ServerConnection::SubscribeResponse(const PubSubRecord& subscriber,
                                      const SubscribeResult& result)
{
  unsigned status_code;                         // HTTP status code to return

  try {
    messages::SubscribeResponse response;
    response.transaction_id = subscriber.transaction_id;
    response.quicr_namespace = subscriber.quicr_namespace;
    response.response = result.status;

    messages::MessageBuffer msg;
    msg << response;

    // Lock the connection object
    std::unique_lock<std::mutex> lock(connection_lock);

    // Update the response record
    RequestData* request = FindRequest(subscriber.stream_id);
    if (!request) {
      logger->warning << "Request for subscribe response not found";
      return;
    }

    // Ensure the state is "Initiated"
    if (request->state != H3RequestState::Initiated) {
      logger->warning << "SubscribeResponse found unexpected request state";
      return;
    }

    // Determine the appropriate status code
    switch (result.status) {
      case SubscribeResult::SubscribeStatus::Ok:
        request->state = H3RequestState::Active;
        status_code = 200;
        break;
      case SubscribeResult::SubscribeStatus::Expired:
        request->state = H3RequestState::Complete;
        status_code = 419;
        break;
      case SubscribeResult::SubscribeStatus::Redirect:
        request->state = H3RequestState::Complete;
        status_code = 303;
        break;
      case SubscribeResult::SubscribeStatus::FailedError:
        request->state = H3RequestState::Complete;
        status_code = 405;
        break;
      case SubscribeResult::SubscribeStatus::FailedAuthz:
        request->state = H3RequestState::Complete;
        status_code = 401;
        break;
      case SubscribeResult::SubscribeStatus::TimeOut:
        request->state = H3RequestState::Complete;
        status_code = 408;
        break;
      default:
        request->state = H3RequestState::Complete;
        status_code = 500;
        break;
    }

    // Send the response to the client, keeping this stream open if subscribing
    SendHTTPResponse(subscriber.stream_id,
                     status_code,
                     {},
                     msg.get(),
                     true,
                     (result.status != SubscribeResult::SubscribeStatus::Ok));

    // If the request is complete, expunge it
    if (request->state == H3RequestState::Complete)
    {
      ExpungeRequest(subscriber.stream_id);
    }

    // If the request was rejected, we remove the subscriber record
    if (result.status != SubscribeResult::SubscribeStatus::Ok)
    {
      pub_sub_registry->Expunge(subscriber.identifier);
    }

    // Dispatch Quiche messages
    DispatchMessages(lock);
  } catch (const std::exception &e) {
    logger->error
      << "Unexpected error trying to process subscribe response message: "
      << e.what() << std::flush;
  } catch (...) {
    logger->error
      << "Unexpected error trying to process subscribe response message"
      << std::flush;
  }
}

/*
 *  H3ServerConnection::SubscriptionEnded()
 *
 *  Description:
 *      Notify the remote client that the subscription has ended.
 *
 *  Parameters:
 *      subscriber [in]
 *          The subscriber record from the pub/sub registry for this response.
 *
 *      quicr_namespace [in]
 *          The namespace for this subscription.
 *
 *      result [in]
 *          The reason the subscription has ended.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
H3ServerConnection::SubscriptionEnded(
  const PubSubRecord& subscriber,
  const quicr::Namespace& quicr_namespace,
  const SubscribeResult::SubscribeStatus& reason)
{
  try {
    // Lock the connection object
    std::unique_lock<std::mutex> lock(connection_lock);

    // Update the response record
    RequestData* request = FindRequest(subscriber.stream_id);
    if (!request) {
      logger->warning << "Request for subscribe response not found";
      return;
    }

    // Ensure the request is in the expected state
    if (request->state != H3RequestState::Active)
    {
      logger->warning << "Subscriber not in an active state; cannot unsubscribe"
                      << std::flush;
    }

    messages::SubscribeEnd subEnd;
    subEnd.quicr_namespace = quicr_namespace;
    subEnd.reason = reason;

    messages::MessageBuffer msg;
    msg << subEnd;

    // Move the buffer and create a DataBuffer object
    cantina::OctetString message_buffer = msg.get();
    cantina::DataBuffer data_buffer(message_buffer.size() +
                                    sizeof(std::uint64_t));

    // Insert the message length
    data_buffer.AppendValue(static_cast<std::uint64_t>(message_buffer.size()));
    data_buffer.AppendValue(message_buffer);

    // Send the message indicating the subscription has ended
    QuicheCall(quiche_h3_send_body,
               http3_connection,
               quiche_connection,
               subscriber.stream_id,
               data_buffer.GetBufferPointer(),
               data_buffer.GetDataLength(),
               true);

    // Remove the subscription record
    ExpungeRequest(subscriber.stream_id);

    // Remove the subscription record
    pub_sub_registry->Expunge(subscriber.identifier);

    // Dispatch Quiche messages
    DispatchMessages(lock);
  } catch (const std::exception &e) {
    logger->error
      << "Unexpected error trying send subscription ended message: "
      << e.what() << std::flush;
  } catch (...) {
    logger->error
      << "Unexpected error trying send subscription ended message"
      << std::flush;
  }
}

/*
 *  H3ServerConnection::SendNamedObject()
 *
 *  Description:
 *      This function will send an object to the subscriber.
 *
 *  Parameters:
 *      subscriber [in]
 *          The subscriber record from the pub/sub registry for this response.
 *
 *      use_reliable_transport [in]
 *          If true, this datagram will be sent as an amendment to the GET
 *          request for this subscription.  If false, it will be sent as an
 *          HTTP datagram.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
H3ServerConnection::SendNamedObject(const PubSubRecord& subscriber,
                                    bool use_reliable_transport,
                                    const messages::PublishDatagram& datagram)
{
  try
  {
    messages::MessageBuffer msg;
    msg << datagram;
    cantina::OctetString message = msg.get();

    // Lock the connection object
    std::unique_lock<std::mutex> lock(connection_lock);

    // If told to use a reliable transport of if the peer does not support
    // datagrams, the message will be sent reliably
    if (use_reliable_transport || !using_datagrams)
    {
      cantina::DataBuffer data_buffer(message.size() + sizeof(std::uint64_t));

      // Insert the message length and message
      data_buffer.AppendValue(static_cast<std::uint64_t>(message.size()));
      data_buffer.AppendValue(message);

      auto result = QuicheCall(quiche_h3_send_body,
                               http3_connection,
                               quiche_connection,
                               subscriber.stream_id,
                               data_buffer.GetBufferPointer(),
                               data_buffer.GetDataLength(),
                               false);
      if (result < 0) {
        logger->warning << "Failed to send named object" << std::flush;
      }
    } else {
      auto result =
        QuicheCall(quiche_h3_send_dgram,
                   http3_connection,
                   quiche_connection,
                   subscriber.stream_id,
                   const_cast<std::uint8_t*>(message.data()),
                   message.size());

      if (result < 0) {
        logger->warning << "Failed to send datagram" << std::flush;
      }
    }

    // Dispatch Quiche messages
    DispatchMessages(lock);
  } catch (const std::exception &e) {
    logger->error
      << "Unexpected error trying to send named object: "
      << e.what() << std::flush;
  } catch (...) {
    logger->error
      << "Unexpected error trying to send named object"
      << std::flush;
  }
}

/*
 *  H3ServerConnection::HandleH3ResetEvent()
 *
 *  Description:
 *      This function is called when handling the QUICHE_H3_EVENT_RESET event.
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream on which this event occurred.
 *
 *      event [in]
 *          A pointer to an quiche_h3_event structure for this event.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ServerConnection::HandleH3ResetEvent(QUICStreamID stream_id,
                                       [[maybe_unused]] quiche_h3_event* event)
{
  LOGGER_DEBUG(logger, "[stream " << stream_id << "] H3 - reset");
}

/*
 *  H3ServerConnection::HandleH3PriorityUpdateEvent()
 *
 *  Description:
 *      This function is called when handling the
 *      QUICHE_H3_EVENT_PRIORITY_UPDATE event.
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream on which this event occurred.
 *
 *      event [in]
 *          A pointer to an quiche_h3_event structure for this event.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ServerConnection::HandleH3PriorityUpdateEvent(
  QUICStreamID stream_id,
  [[maybe_unused]] quiche_h3_event* event)
{
  LOGGER_DEBUG(logger, "[stream " << stream_id << "] H3 - priority update");
}

/*
 *  H3ServerConnection::HandleH3DatagramEvent()
 *
 *  Description:
 *      This function is called when handling the QUICHE_H3_EVENT_DATAGRAM
 *      event.
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream on which this event occurred.
 *
 *      event [in]
 *          A pointer to an quiche_h3_event structure for this event.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ServerConnection::HandleH3DatagramEvent(
  QUICStreamID stream_id,
  [[maybe_unused]] quiche_h3_event* event)
{
  std::uint64_t flow_id{};
  std::size_t flow_id_length{};
  cantina::DataBuffer buffer(max_packet_size);

  LOGGER_DEBUG(logger, "[stream " << stream_id << "] H3 - event datagram");

  // It's important to exhaust the queue when this event is triggered; see
  // https://docs.quic.tech/quiche/h3/enum.Event.html#variant.Datagram
  while (true) {
    // Attempt to read the datagrams
    auto bytes = QuicheCall(quiche_h3_recv_dgram,
                            http3_connection,
                            quiche_connection,
                            &flow_id,
                            &flow_id_length,
                            buffer.GetBufferPointer(),
                            buffer.GetBufferSize());

    // Break out of the loop once done processing the queue
    if (bytes == QUICHE_ERR_DONE) break;

    // Check if there was an error
    if (bytes < 0) {
      logger->warning << "Unable to read datagram on stream " << stream_id << ": "
                      << bytes << std::flush;
      continue;
    }

    // Set the DataBuffer length
    buffer.SetDataLength(bytes);

    // Ensure there is some actual data to consider
    if (flow_id_length >= buffer.GetDataLength()) {
      logger->warning << "Received empty or malformed datagram on stream "
                      << stream_id << " having flow ID " << flow_id << std::flush;
      continue;
    }

    try {
      // Create an OctetString to contain the message without the leading length
      cantina::OctetString message(buffer.GetBufferPointer() + flow_id_length,
                                   buffer.GetBufferPointer() +
                                     buffer.GetDataLength() - flow_id_length);

      // Move the octet string data into a MessageBuffer
      messages::MessageBuffer msg(std::move(message));

      // Deserialize the message
      messages::PublishDatagram datagram;
      msg >> datagram;

      // Try to find the publisher
      auto publisher = pub_sub_registry->FindPublisher(datagram.header.name);
      if (!publisher.has_value())
      {
        logger->warning << "Unable to find publisher for name "
                        << datagram.header.name << std::flush;
        return;
      }

      // Create an async event to issue the call to the delegate
      async_requests->Perform([&, datagram = std::move(datagram)]() mutable {
        server_delegate.onPublisherObject(publisher->identifier,
                                          stream_id,
                                          false,
                                          std::move(datagram));
      });
    } catch (const std::exception& e) {
      logger->error << "Error processing datagram: " << e.what() << std::flush;
    } catch (...) {
      logger->error << "Error processing datagram" << std::flush;
    }
  }
}

/*
 *  H3ServerConnection::HandleH3GoAwayEvent()
 *
 *  Description:
 *      This function is called when handling the QUICHE_H3_EVENT_GOAWAY
 *      event.
 *
 *  Parameters:
 *      stream_id [in]
 *          QUIC stream on which this event occurred.
 *
 *      event [in]
 *          A pointer to an quiche_h3_event structure for this event.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ServerConnection::HandleH3GoAwayEvent(QUICStreamID stream_id,
                                        [[maybe_unused]] quiche_h3_event* event)
{
  LOGGER_DEBUG(logger, "[stream " << stream_id << "] H3 - event go away");
}

/*
 *  H3ServerConnection::ProcessHeader()
 *
 *  Description:
 *      This function is called repeatedly when HTTP/3 headers are received.
 *
 *  Parameters:
 *      stream_id [in]
 *          Stream on which these headers are received.
 *
 *      name [in]
 *          The name of the HTTP header.
 *
 *      value [in]
 *          The value of the HTTP header.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ServerConnection::ProcessHeader(QUICStreamID stream_id,
                                  std::string& name,
                                  std::string& value)
{
  LOGGER_DEBUG(logger,
               "[stream " << stream_id << "] H3 header => " << name << " : "
                          << value);

  // Find the request structure or create it if it doesn't exist
  RequestData *request = FindRequest(stream_id, true);

  // Update the request headers for this request
  request->request_headers[name] = value;
}

/*
 *  H3ServerConnection::ProcessSetting()
 *
 *  Description:
 *      This function is called repeatedly when HTTP/3 settings are received.
 *
 *  Parameters:
 *      identifier [in]
 *          The setting identifier.
 *
 *      value [in]
 *          The value of the setting.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ServerConnection::ProcessSetting(std::uint64_t identifier,
                                   std::uint64_t value)
{
  std::stringstream oss;

  oss << "H3 setting => " << std::hex << "0x" << identifier << " : " << std::dec
      << value;

  logger->info << oss.str() << std::flush;

  connection_settings[identifier] = value;
}

/*
 *  H3ServerConnection::FindRequest()
 *
 *  Description:
 *      Find the HTTP request that is associated with the given stream.
 *
 *  Parameters:
 *      stream_id [in]
 *          The stream identifier associated with the sought request.
 *
 *      create [in]
 *          Create the request object if it doesn't exist.
 *
 *  Returns:
 *      A pointer to the RequestData structure found or nullptr if the
 *      request structure was not found.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
H3ServerConnection::RequestData*
H3ServerConnection::FindRequest(QUICStreamID stream_id, bool create)
{
  // Locate the associated request
  auto iter = requests.find(stream_id);

  // Not found?
  if (iter == requests.end())
  {
    // If not creating, return nullptr
    if (!create) return nullptr;

    // Create a new request entry
    auto result = requests.insert(
      { stream_id, { H3RequestState::Initiated, {}, {}, {}, {} } });

    // Return nullptr if there is an error
    if (!result.second) return nullptr;

    // Return the request data pointer
    return &(result.first->second);
  }

  // Return the request data pointer
  return &(iter->second);
}

/*
 *  H3ServerConnection::ExpungeRequest()
 *
 *  Description:
 *      This function will expunge the request from the requests map.
 *
 *  Parameters:
 *      stream_id [in]
 *          The stream identifier associated with the request.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ServerConnection::ExpungeRequest(QUICStreamID stream_id)
{
  // Locate the associated request
  auto iter = requests.find(stream_id);
  if (iter == requests.end()) return;

  // Remove the request
  requests.erase(iter);
}

} // namespace quicr
