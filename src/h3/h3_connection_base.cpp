/*
 *  h3_connection.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      This file defines a base class for common connection logic for
 *      communication over a single QUIC connection.
 *
 *  Portability Issues:
 *      None.
 *
 */

#include <algorithm>
#include <cctype>
#include <chrono>
#include <utility>
#include "h3_connection_base.h"
#include "cantina/data_buffer.h"
#include "cantina/logger_macros.h"
#include "quiche_api_lock.h"

namespace quicr
{

/*
 *  H3ConnectionBase::H3ConnectionBase()
 *
 *  Description:
 *      Constructor for the H3ConnectionBase object.
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
 *          Local address.
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
H3ConnectionBase::H3ConnectionBase(
  const cantina::LoggerPointer& parent_logger,
  const cantina::TimerManagerPointer& timer_manager,
  const cantina::AsyncRequestsPointer& async_requests,
  const cantina::NetworkPointer& network,
  const PubSubRegistryPointer& pub_sub_registry,
  socket_t data_socket,
  std::size_t max_send_size,
  std::size_t max_recv_size,
  bool use_datagrams,
  const QUICConnectionID& local_cid,
  const cantina::NetworkAddress& local_address,
  quiche_conn* quiche_connection,
  std::uint64_t heartbeat_interval,
  const ClosureCallback closure_callback)
  : terminate{ false }
  , logger{ std::make_shared<cantina::Logger>(std::string("CNCT:") +
                                                local_cid.SuffixString(),
                                              parent_logger) }
  , timer_manager{ timer_manager }
  , async_requests{ async_requests }
  , network{ network }
  , pub_sub_registry{ pub_sub_registry }
  , data_socket{ data_socket }
  , max_send_size{ max_send_size }
  , max_recv_size{ max_recv_size }
  , use_datagrams { use_datagrams }
  , using_datagrams{ false }
  , local_cid{ local_cid }
  , local_address{ local_address }
  , quiche_connection{ quiche_connection }
  , connection_state{ H3ConnectionState::ConnectPending }
  , closure_callback{ closure_callback }
  , closure_notification{ false }
  , http3_config{ nullptr }
  , http3_connection{ nullptr }
  , settings_received{ false }
  , timer_create_pending{ false }
  , heartbeat_timer{ 0 }
{
  logger->info << "Created connection " << local_cid << std::flush;

  // Create the HTTP/3 configuration structure
  if ((http3_config = quiche_h3_config_new()) == nullptr) {
    throw H3ConnectionException("Failed to create HTTP/3 configuration");
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
 *  H3ConnectionBase::~H3ConnectionBase()
 *
 *  Description:
 *      Destructor for the H3ConnectionBase object.
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
H3ConnectionBase::~H3ConnectionBase()
{
  logger->info << "Deleting connection " << local_cid << std::flush;

  // Lock the client mutex
  std::unique_lock<std::mutex> lock(connection_lock);

  // Set the terminate flag
  terminate = true;

  // Unlock the client mutex
  lock.unlock();

  // Terminate any running timers
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
 *  H3ClientConnection::GetConnectionState()
 *
 *  Description:
 *      Return the connection object's current state.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      The current connection state.
 *
 *  Comments:
 *      None.
 */
H3ConnectionState
H3ConnectionBase::GetConnectionState()
{
  std::unique_lock<std::mutex> lock(connection_lock);

  return connection_state;
}

/*
 *  H3ConnectionBase::IsConnected()
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
H3ConnectionBase::IsConnectionClosed()
{
  return QuicheCall(quiche_conn_is_closed, quiche_connection);
}

/*
 *  H3ConnectionBase::IsConnectionEstablished()
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
H3ConnectionBase::IsConnectionEstablished()
{
  return QuicheCall(quiche_conn_is_established, quiche_connection);
}

/*
 *  H3ConnectionBase::GetConnectionID()
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
H3ConnectionBase::GetConnectionID()
{
  return local_cid;
}

/*
 *  H3ConnectionBase::ProcessPacket()
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
H3ConnectionBase::ProcessPacket(cantina::DataPacket& data_packet)
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
 *  H3ConnectionBase::ConsumePacket()
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
H3ConnectionBase::ConsumePacket(cantina::DataPacket& data_packet)
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
      logger->critical << "Exception thrown processing events; "
                          "closing connection: "
                       << e.what() << std::flush;
    } catch (...) {
      connection_error = true;
      logger->critical << "Exception thrown processing events; "
                          "closing connection"
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
 *  H3ConnectionBase::QuicheConsumeData()
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
 *      The connection mutex must be locked by the caller.
 */
bool
H3ConnectionBase::QuicheConsumeData(cantina::DataPacket& data_packet)
{
  ssize_t quiche_result;

  quiche_recv_info recv_info = {
    const_cast<sockaddr*>(reinterpret_cast<const sockaddr*>(
      data_packet.GetAddress().GetAddressStorage())),
    data_packet.GetAddress().GetAddressStorageSize(),
    const_cast<sockaddr*>(
      reinterpret_cast<const sockaddr*>(local_address.GetAddressStorage())),
    local_address.GetAddressStorageSize()
  };

  // Process the data packet
  quiche_result = QuicheCall(quiche_conn_recv,
                             quiche_connection,
                             data_packet.GetBufferPointer(),
                             data_packet.GetDataLength(),
                             &recv_info);
  if (quiche_result < 0) {
    if (quiche_result == QUICHE_ERR_TLS_FAIL) {
      if (!IsConnectionEstablished()) {
        logger->error << "TLS error trying to establish connection"
                      << std::flush;
      } else {
        logger->error << "TLS error processing packet" << std::flush;
      }
    } else {
      logger->error << "Failure to read the data packet" << std::flush;
    }

    return false;
  }

  return true;
}

/*
 *  H3ConnectionBase::DispatchMessages()
 *
 *  Description:
 *      Process an incoming data packet.
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
H3ConnectionBase::DispatchMessages(std::unique_lock<std::mutex>& lock,
                                   cantina::TimerID timer_id)
{
  // Send queued messages
  SendQueuedMessages();

  // Emit QUIC messages Quiche has prepared
  QuicheEmitQUICMessages(lock);

  // Notify if the connection is closed
  NotifyOnClosure();

  // Ensure we have a (freshened) timeout timer running
  CreateOrRefreshTimer(lock, timer_id);
}

/*
 *  H3ConnectionBase::SendMessageBody()
 *
 *  Description:
 *      This will send a message body or queue it for later transmission
 *      as required.
 *
 *  Parameters:
 *      stream_id [in]
 *          The stream on which this message should be sent.
 *
 *      message [in]
 *          The HTTP request or response body part to send.  This buffer may
 *          be moved and the contents should be considered invalid on return.
 *
 *      final [in]
 *          Is this the final message?  Once all messages for this stream
 *          are sent and seeing this flag at least once set to true,
 *          Quiche will be told this is the final message.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ConnectionBase::SendMessageBody(QUICStreamID stream_id,
                                  std::vector<std::uint8_t>& message,
                                  bool final)
{
  // Ensure the message is not empty unless final is true
  if (message.empty() && !final)
  {
    logger->error << "Attempt to send empty non-final message body on stream "
                  << stream_id << std::flush;
    return;
  }

  // If this stream already has queued messages, just append this one
  auto it = queued_messages.find(stream_id);
  if (it != queued_messages.end())
  {
    // Ensure the final flag was not previously set
    if (it->second.final)
    {
      logger->error << "Final flag was previously set on messages for stream "
                    << stream_id << std::flush;
      return;
    }

    // Only queue it if there is actual data to send
    if (!message.empty())
    {
      it->second.messages.emplace_back(std::make_pair(0, std::move(message)));
    }

    // Take note of the final flag
    if (final) it->second.final = true;
    return;
  }

  // Attempt to send the message immediately
  auto octets_sent = QuicheCall(quiche_h3_send_body,
                                http3_connection,
                                quiche_connection,
                                stream_id,
                                message.data(),
                                message.size(),
                                final);

  // If all octets were successfully transmitted, then return
  if (static_cast<std::size_t>(octets_sent) == message.size()) return;

  // If the stream is blocked, queue the message with octets 0 sent
  if (octets_sent == QUICHE_ERR_DONE) octets_sent = 0;

  // A negative result indicates an error; cannot recover from this
  if (octets_sent < 0)
  {
    logger->error << "Unrecoverable error sending message body on stream "
                  << stream_id << " (error=" << octets_sent << ")"
                  << std::flush;
    return;
  }

  auto [iit, success] = queued_messages.insert(
    std::make_pair(stream_id, MessageQueue{ {}, final }));
  if (!success)
  {
    logger->error << "Unable to insert message into queue on steam "
                  << stream_id << std::flush;
    return;
  }

  // Enqueue the message
  iit->second.messages.emplace_back(
    std::make_pair(octets_sent, std::move(message)));
}

/*
 *  H3ConnectionBase::SendQueuedMessages()
 *
 *  Description:
 *      This will attempt to send any messages that are presently queued.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The connection mutex must be locked by the caller.
 */
void
H3ConnectionBase::SendQueuedMessages()
{
  // Iterate over the queued messages map to send queued messages
  for (auto it = queued_messages.begin(); it != queued_messages.end(); /**/)
  {
    // Set the final flag?
    bool final = false;

    // Process the deque for this stream until empty
    while (!it->second.messages.empty())
    {
      // Make it convenient to reference the data buffer
      auto &message = it->second.messages.front();

      // If this is the last message in the queue, update the final flag
      if (it->second.messages.size() == 1) final = it->second.final;

      // Attempt to send the message
      auto octets_sent =
        QuicheCall(quiche_h3_send_body,
                   http3_connection,
                   quiche_connection,
                   it->first,
                   message.second.data() + message.first,
                   message.second.size() - message.first,
                   final);

      // If the stream is blocked, just move on to the next stream
      if (octets_sent == QUICHE_ERR_DONE) break;

      // If there was an unrecoverable error, flush messages
      if (octets_sent < 0)
      {
        logger->error << "Unrecoverable error sending message body on stream "
                      << it->first << " (error=" << octets_sent << ")"
                      << std::flush;
        it->second.messages.clear();
        break;
      }

      // If all octets were successfully transmitted, remove this buffer
      if ((message.first +
           static_cast<std::size_t>(octets_sent)) ==
          message.second.size()) {

        // Remove this message, as it's complete
        it->second.messages.pop_front();

        // Attempt to send the next message
        continue;
      }

      // If only part of the data was sent, update the sent value
      message.first += octets_sent;

      // Since the stream buffer must be full, move on to the next stream
      break;
    }

    // If the messages for this stream are all sent, remove the map entry
    if (it->second.messages.empty())
    {
      it = queued_messages.erase(it);
    }
    else
    {
      // Move forward to the next stream
      it++;
    }
  }
}

/*
 *  H3ConnectionBase::QuicheEmitQUICMessages()
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
H3ConnectionBase::QuicheEmitQUICMessages(std::unique_lock<std::mutex>& lock)
{
  // Create the data packet for sending messages
  cantina::DataPacket data_packet(max_recv_size);

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
 *  H3ConnectionBase::NotifyOnClosure()
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
H3ConnectionBase::NotifyOnClosure(bool force_close)
{
  if ((force_close || IsConnectionClosed()) && !closure_notification) {
    logger->info << "Connection was closed" << std::flush;

    // Note the callback was made
    closure_notification = true;

    // Set the connection state to closed
    connection_state = H3ConnectionState::Disconnected;

    // Issue closure notification callback
    closure_callback();
  }
}

/*
 *  H3ConnectionBase::CreateOrRefreshTimer()
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
H3ConnectionBase::CreateOrRefreshTimer(std::unique_lock<std::mutex>& lock,
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
 *  H3ConnectionBase::TimeoutHandler()
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
H3ConnectionBase::TimeoutHandler(cantina::TimerID timer_id)
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
 *  H3ConnectionBase::QuicheHTTP3EventHandler()
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
H3ConnectionBase::QuicheHTTP3EventHandler(std::unique_lock<std::mutex>& lock)
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
    auto max_quiche_datagram =
      quiche_conn_dgram_max_writable_len(quiche_connection);
    if (max_quiche_datagram > 0) {
      max_send_size =
        std::min(static_cast<std::size_t>(max_quiche_datagram), max_send_size);
      logger->info << "Max quiche datagram size is " << max_quiche_datagram
                   << ", using " << max_send_size << std::flush;
    }
  }

  // If the connection is pending, transition to connected state
  if (connection_state == H3ConnectionState::ConnectPending) {
    std::string protocol;
    const std::uint8_t* app_proto;
    std::size_t app_proto_len;

    // Get the negotiated ALPN value and log it
    QuicheCall(quiche_conn_application_proto,
               quiche_connection,
               &app_proto,
               &app_proto_len);
    protocol.assign(reinterpret_cast<const char*>(app_proto), app_proto_len);

    logger->info << "Connection established: " << protocol << std::flush;

    // Update the client state
    connection_state = H3ConnectionState::Connected;
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
                     H3ConnectionBase::SettingsCallback,
                     this) == 0) {
        settings_received = true;

        // If requested to use datagrams, check peer support
        if (use_datagrams && QuicheCall(quiche_h3_dgram_enabled_by_peer,
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
 *  H3ConnectionBase::HandleH3HeadersEvent()
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
H3ConnectionBase::HandleH3HeadersEvent(QUICStreamID stream_id,
                                       quiche_h3_event* event)
{
  LOGGER_DEBUG(logger, "[stream " << stream_id << "] H3 - incoming headers");

  std::pair<H3ConnectionBase*, QUICStreamID> stream_data{ this, stream_id };

  if (QuicheCall(quiche_h3_event_for_each_header,
                 event,
                 H3ConnectionBase::HeaderCallback,
                 &stream_data)) {
    logger->warning << "[stream " << stream_id << "] Failed to process headers"
                    << std::flush;
    return;
  }
}

/*
 *  H3ConnectionBase::HandleH3DataEvent()
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
H3ConnectionBase::HandleH3DataEvent(QUICStreamID stream_id,
                                    [[maybe_unused]] quiche_h3_event* event)
{
  std::vector<std::uint8_t> buffer(max_recv_size);

  LOGGER_DEBUG(logger, "[stream " << stream_id << "] H3 - data event");

  // Find the associated request data
  auto request = FindRequest(stream_id);
  if (request == nullptr) {
    logger->warning << "Data event for unknown stream " << stream_id
                    << std::flush;
    return;
  }

  // Create a reference to the request_body for convenience
  auto& request_body = request->request_body;

  while (true) {
    ssize_t length = QuicheCall(quiche_h3_recv_body,
                                http3_connection,
                                quiche_connection,
                                stream_id,
                                buffer.data(),
                                max_recv_size);

    if (length <= 0) break;

    // Append the octets in the request to the request body
    request_body.insert(
      request_body.end(), buffer.begin(), buffer.begin() + length);

    LOGGER_DEBUG(logger,
                 "[stream " << stream_id << "] Received " << length
                            << " octets in body");
  }

  // Call function in derived class to handle any new data
  HandleIncrementalRequestData(stream_id, request);
}

/*
 *  H3ConnectionBase::HandleH3FinishedEvent()
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
H3ConnectionBase::HandleH3FinishedEvent(QUICStreamID stream_id,
                                        [[maybe_unused]] quiche_h3_event* event)
{
  LOGGER_DEBUG(
    logger, "[stream " << stream_id << "] H3 - Finished, closing connection");

  // Locate the associated request
  auto request = FindRequest(stream_id);
  if (request == nullptr) {
    logger->warning << "Finished event for unknown stream " << stream_id
                    << std::flush;
    return;
  }

  // Call routine to handle completed request
  auto result = HandleCompletedRequest(stream_id, request);

  // If this request is complete, remove it
  if (result) ExpungeRequest(stream_id);
}

/*
 *  H3ConnectionBase::HandleH3ResetEvent()
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
H3ConnectionBase::HandleH3ResetEvent(QUICStreamID stream_id,
                                     [[maybe_unused]] quiche_h3_event* event)
{
  logger->info << "[stream " << stream_id << "] H3 - reset" << std::flush;

  ExpungeRequest(stream_id);
}

/*
 *  H3ConnectionBase::HandleH3PriorityUpdateEvent()
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
H3ConnectionBase::HandleH3PriorityUpdateEvent(
  QUICStreamID stream_id,
  [[maybe_unused]] quiche_h3_event* event)
{
  LOGGER_DEBUG(logger, "[stream " << stream_id << "] H3 - priority update");
}

/*
 *  H3ConnectionBase::HandleH3DatagramEvent()
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
H3ConnectionBase::HandleH3DatagramEvent(
  QUICStreamID stream_id,
  [[maybe_unused]] quiche_h3_event* event)
{
  std::uint64_t flow_id{};
  std::size_t flow_id_length{};

  LOGGER_DEBUG(logger, "[stream " << stream_id << "] H3 - event datagram");

  // It's important to exhaust the queue when this event is triggered; see
  // https://docs.quic.tech/quiche/h3/enum.Event.html#variant.Datagram
  while (true) {
    cantina::DataBuffer buffer(max_recv_size);

    // Attempt to read the datagrams
    auto octets_received = QuicheCall(quiche_h3_recv_dgram,
                                      http3_connection,
                                      quiche_connection,
                                      &flow_id,
                                      &flow_id_length,
                                      buffer.GetBufferPointer(),
                                      buffer.GetBufferSize());

    // Break out of the loop once done processing the queue
    if (octets_received == QUICHE_ERR_DONE) break;

    // Check if there was an error
    if (octets_received < 0) {
      logger->warning << "Unable to read datagram on stream " << stream_id
                      << ": " << octets_received << std::flush;
      continue;
    }

    // Set the DataBuffer length
    buffer.SetDataLength(octets_received);

    // Ensure there is some actual data to consider
    if (flow_id_length >= buffer.GetDataLength()) {
      logger->warning << "Received empty or malformed datagram on stream "
                      << stream_id << " having flow ID " << flow_id
                      << std::flush;
      continue;
    }

    // Create an vector to contain the message without the leading length
    std::vector<std::uint8_t> message(
      buffer.GetBufferPointer() + flow_id_length,
      buffer.GetBufferPointer() + buffer.GetDataLength());

    // Call function to handle the datagram
    HandleReceivedDatagram(flow_id, message);
  }
}

/*
 *  H3ConnectionBase::HandleH3GoAwayEvent()
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
H3ConnectionBase::HandleH3GoAwayEvent(QUICStreamID stream_id,
                                      [[maybe_unused]] quiche_h3_event* event)
{
  LOGGER_DEBUG(logger, "[stream " << stream_id << "] H3 - event go away");
}

/*
 *  H3ConnectionBase::ProcessHeader()
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
H3ConnectionBase::ProcessHeader(QUICStreamID stream_id,
                                std::string& name,
                                std::string& value)
{
  LOGGER_DEBUG(logger,
               "[stream " << stream_id << "] H3 header => " << name << " : "
                          << value);

  // Find the request structure or create it if it doesn't exist
  RequestData* request = FindRequest(stream_id, true);
  if (request == nullptr) {
    logger->warning << "Headers received for unknown stream " << stream_id
                    << std::flush;
    return;
  }

  // Update the request headers for this request
  request->request_headers[name] = value;

  // Set the status code as soon as it is received
  if (name == ":status") {
    try {
      request->status_code = std::stoi(value);
    } catch (...) {
      // Note the error
      logger->error << "Invalid status code received from the server"
                    << std::flush;
    }
  }
}

/*
 *  H3ConnectionBase::ProcessSetting()
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
H3ConnectionBase::ProcessSetting(std::uint64_t identifier, std::uint64_t value)
{
  std::stringstream oss;

  oss << "H3 setting => " << std::hex << "0x" << identifier << " : " << std::dec
      << value;

  logger->info << oss.str() << std::flush;

  connection_settings[identifier] = value;
}

/*
 *  H3ConnectionBase::FindRequest()
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
H3ConnectionBase::RequestData*
H3ConnectionBase::FindRequest(QUICStreamID stream_id, bool create)
{
  // Locate the associated request
  auto iter = requests.find(stream_id);

  // Not found?
  if (iter == requests.end()) {
    // If not creating, return nullptr
    if (!create) return nullptr;

    // Create a new request entry
    auto result = requests.insert(
      { stream_id, { H3RequestState::Initiated, {}, {}, {}, {}, {} } });

    // Return nullptr if there is an error
    if (!result.second) return nullptr;

    // Return the request data pointer
    return &(result.first->second);
  }

  // Return the request data pointer
  return &(iter->second);
}

/*
 *  H3ConnectionBase::ExpungeRequest()
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
H3ConnectionBase::ExpungeRequest(QUICStreamID stream_id)
{
  // Locate the associated request
  auto iter = requests.find(stream_id);
  if (iter == requests.end()) return;

  // Remove the request
  requests.erase(iter);
}

} // namespace quicr
