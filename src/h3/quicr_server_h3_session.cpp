/*
 *  quicr_server_h3_session.cpp
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      This file implements the QuicRServerH3Session object.
 *
 *  Portability Issues:
 *      None.
 *
 */

#include "quicr_server_h3_session.h"
#include "cantina/logger_macros.h"
#include "quic_identifier.h"
#include "quiche_api_lock.h"
#include "quiche_types.h"
#include "transport_api_lock.h"
#include <chrono>
#include <functional>
#include <memory>
#include <sstream>

namespace quicr::h3 {

/*
 *  QuicRServerH3Session::QuicRServerH3Session()
 *
 *  Description:
 *      Constructor for the QuicRServerH3Session object.
 *
 *  Parameters:
 *      relay_info [in]
 *          Relay information.
 *
 *      transport_config [in]
 *          Transport configuration information.
 *
 *      server_delegate [in]
 *          A reference to the server delegate to which callbacks are made.
 *
 *      transport_logger [in]
 *          qtransport logging object.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
QuicRServerH3Session::QuicRServerH3Session(
  RelayInfo& relay_info,
  qtransport::TransportConfig transport_config,
  ServerDelegate& server_delegate,
  qtransport::LogHandler& transport_logger)
  : terminate{ false }
  , logger{ nullptr }
  , timer_manager{ nullptr }
  , transport{ nullptr }
  , transport_context{ 0 }
  , transport_delegate{ this }
  , server_delegate{ server_delegate }
  , certificate{ (transport_config.tls_cert_filename
                    ? transport_config.tls_cert_filename
                    : "") }
  , certificate_key{ (transport_config.tls_key_filename
                        ? transport_config.tls_key_filename
                        : "") }
  , use_datagrams{ true } // TODO: We should signal this
  , server_config{ nullptr }
  , data_socket{ 0 }
  , generator{ std::random_device()() }
  , pub_sub_registry{ std::make_shared<PubSubRegistry>() }
{
  // Lock the mutex to exclude threads until fully constructed
  std::lock_guard<std::mutex> lock(server_lock);

  // Define custom logging function
  auto logging_function = [&](cantina::LogLevel level,
                              const std::string& message,
                              [[maybe_unused]] bool console) {
    qtransport::LogLevel log_level;
    switch (level) {
      case cantina::LogLevel::Critical:
        log_level = qtransport::LogLevel::fatal;
        break;
      case cantina::LogLevel::Error:
        log_level = qtransport::LogLevel::error;
        break;
      case cantina::LogLevel::Warning:
        log_level = qtransport::LogLevel::warn;
        break;
      case cantina::LogLevel::Info:
        log_level = qtransport::LogLevel::info;
        break;
      case cantina::LogLevel::Debug:
        log_level = qtransport::LogLevel::debug;
        break;
      default:
        log_level = qtransport::LogLevel::debug;
        break;
    }
    transport_logger.log(log_level, message);
  };

  // Create a custom logging object to interface with the libquicr logger
  logger = std::make_shared<cantina::CustomLogger>("H3", logging_function);

  logger->info << "QuicRServerH3Session starting" << std::flush;

  // Configure Quiche
  try {
    ConfigureQuiche();
  } catch (...) {
    if (server_config) quiche_config_free(server_config);
    throw;
  }

  // Populate the server token prefix
  for (std::size_t i = 0; i < 7; i++) token_prefix.push_back(GetRandomOctet());

  // Create a thread pool for use by the TimerManager
  auto thread_pool = std::make_shared<cantina::ThreadPool>(logger, 5, 10);

  // Create the TimerManager object
  timer_manager = std::make_shared<cantina::TimerManager>(logger, thread_pool);

  // Create an AsyncRequests object to facilitate asynchronous callbacks
  async_requests = std::make_shared<cantina::AsyncRequests>(thread_pool);

  // Attempt to resolve the given address
  local_address = cantina::FindIPv4Address(relay_info.hostname,
                                           std::to_string(relay_info.port));
  if (!local_address) {
    local_address = cantina::NetworkAddress("127.0.0.1", relay_info.port);
  }

  // Store the local address information
  local_address.SetAddress(relay_info.hostname, relay_info.port);

  // Set up transport info
  qtransport::TransportRemote transport_info;
  transport_info.host_or_ip = relay_info.hostname;
  transport_info.port = relay_info.port;
  transport_info.proto = qtransport::TransportProtocol::UDP;

  transport = qtransport::ITransport::make_server_transport(
    transport_info, transport_config, transport_delegate, transport_logger);
  transport_context = transport->start();

  // Create the worker thread to handle packets and connection cleanup
  worker_thread = std::thread([&]() { WorkerIdleLoop(); });

  logger->info << "QuicRServerH3Session started" << std::flush;
}

/*
 *  QuicRServerH3Session::~QuicRServerH3Session()
 *
 *  Description:
 *      Destructor for the QuicRServerH3Session object.
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
QuicRServerH3Session::~QuicRServerH3Session()
{
  logger->info << "QuicRServerH3Session terminating" << std::flush;

  // Lock the mutex, set terminate to true, and unlock
  std::unique_lock<std::mutex> lock(server_lock);
  terminate = true;
  lock.unlock();

  // Stop the worker thread
  cv.notify_one();
  worker_thread.join();

  // Explicitly destroy all connection objects
  connections.clear();
  closed_connections.clear();

  // Terminate the transport context
  TransportCall([&]() { transport->close(transport_context); });

  // Destroy the transport (preventing further callbacks)
  transport.reset();

  // Wait for asynchronous requests to complete
  async_requests.reset();

  // Destroy the timer manager
  timer_manager.reset();

  // Release the configuration
  quiche_config_free(server_config);

  logger->info << "QuicRServerH3Session terminated" << std::flush;
}

/*
 *  QuicRServerH3Session::ConfigureQuiche()
 *
 *  Description:
 *      Destructor for the QuicRServerH3Session object.
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
void
QuicRServerH3Session::ConfigureQuiche()
{
  // Initialize the Quiche code
  if ((server_config = quiche_config_new(QUICHE_PROTOCOL_VERSION)) == nullptr) {
    throw QuicRServerH3SessionException("Unable to initialize Quiche module");
  }

  // Enable debug logging for debugging
  if (logger->GetLogLevel() == cantina::LogLevel::Debug) {
    quiche_enable_debug_logging(&QuicRServerH3Session::quiche_log, this);
  }

  // Specify the certificate files to use
  if (quiche_config_load_cert_chain_from_pem_file(server_config,
                                                  certificate.c_str())) {
    throw QuicRServerH3SessionException(
      std::string("Failed to load certificate file: ") + certificate);
  }
  if (quiche_config_load_priv_key_from_pem_file(server_config,
                                                certificate_key.c_str())) {
    throw QuicRServerH3SessionException(
      std::string("Failed to load certificate key file: ") + certificate_key);
  }

  // Set the application protocols
  if (quiche_config_set_application_protos(
        server_config,
        reinterpret_cast<const uint8_t*>(QUICHE_H3_APPLICATION_PROTOCOL),
        sizeof(QUICHE_H3_APPLICATION_PROTOCOL) - 1)) {
    throw QuicRServerH3SessionException(
      "Failed to set TLS application protocols");
  }

  // Specify connection timeout in ms
  quiche_config_set_max_idle_timeout(server_config, Connection_Timeout);

  // Define max send/receive UDP buffer sizes
  quiche_config_set_max_recv_udp_payload_size(server_config, Max_Recv_Size);
  quiche_config_set_max_send_udp_payload_size(server_config, Max_Recv_Size);

  // Set the size of the incoming buffer stream
  quiche_config_set_initial_max_data(server_config, 10'000'000);

  // Buffer for each locally-initiated bidirectional stream (needed here?)
  quiche_config_set_initial_max_stream_data_bidi_local(server_config,
                                                       1'000'000);

  // Buffer for each remote-initiated bidirectional stream
  quiche_config_set_initial_max_stream_data_bidi_remote(server_config,
                                                        1'000'000);

  // Initial buffer for remote-initiated unidirectional streams
  quiche_config_set_initial_max_stream_data_uni(server_config, 1'000'000);

  // Max number of remote bidirectional streams initiated
  quiche_config_set_initial_max_streams_bidi(server_config, 1000);

  // Max number of remote unidirectional streams initiated
  quiche_config_set_initial_max_streams_uni(server_config, 1000);

  // Disable active migration (See section 9 of RFC 9000)
  quiche_config_set_disable_active_migration(server_config, true);

  // Congestion control algorithm (RFC 9002)
  quiche_config_set_cc_algorithm(server_config, QUICHE_CC_RENO);

  // Enable datagram support
  quiche_config_enable_dgram(server_config, true, 1'000, 1'000);
}

/*
 *  QuicRServerH3Session::IncomingPacketNotification()
 *
 *  Description:
 *      This function is called by the transport to notify us about the fact
 *      a new packet has arrived for the specified context and streamId.
 *      It should be noted that, since this is a UDP transport, these
 *      are not QUIC identifiers.  They're used only by the underlying
 *      transport library.
 *
 *  Parameters:
 *      context_id [in]
 *          The transport's context ID for this packet notification.
 *
 *      stream_id [in]
 *          The stream ID associated with this packet.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void QuicRServerH3Session::IncomingPacketNotification(
    const qtransport::TransportContextId& context_id,
    const qtransport::StreamId& stream_id)
{
  // Protect the shared server data
  std::lock_guard<std::mutex> server_protect(server_lock);

  // Notify the worker thread of new packet notifications
  packet_queue.emplace_back(context_id, stream_id);

  // Notify the worker thread of the new packet
  cv.notify_one();
}

/*
 *  QuicRServerH3Session::ProcessPackets()
 *
 *  Description:
 *      This function is called by the worker thread to pull packets from the
 *      packet notification deque, call the transport to retrieve packets,
 *      and then finally feed those packets to Quiche.
 *
 *  Parameters:
 *      lock [in]
 *          The server lock that should be in a locked state.  It will be
 *          unlocked while it is processing the packet queue, then locked
 *          again before returning.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
QuicRServerH3Session::ProcessPackets(std::unique_lock<std::mutex> &lock)
{
  std::deque<StreamContext> new_packet_queue;

  // Swap the notification deques so as to have a local copy to operate on
  std::swap(new_packet_queue, packet_queue);

  // Unlock the mutex while processing the deque
  lock.unlock();

  try
  {
    for (auto &stream_context : new_packet_queue)
    {
      std::vector<std::vector<std::uint8_t>> packets_to_process;
      bool item_exhausted = false;

      // Pull all of the available packets off the queue
      while(!item_exhausted)
      {
        auto data = TransportCall([&]() {
          return transport->dequeue(stream_context.first,
                                    stream_context.second);
        });

        if (data.has_value()) {
          packets_to_process.push_back(std::move(*data));
        }
        else
        {
          item_exhausted = true;
        }

        // If there are an excessive number of packets, schedule this
        // stream to be serviced again
        if (packets_to_process.size() >= Max_Stream_Reads_Per_Notification)
        {
          lock.lock();
          packet_queue.push_back(stream_context);
          lock.unlock();
          break;
        }
      }

      // Process the packets in turn
      for (auto& packet : packets_to_process)
      {
        PacketHandler(stream_context, packet);
      }
    }
  }
  catch (const std::exception &e)
  {
    logger->critical << "Exception caught cleaning up connections: " << e.what()
                     << std::flush;
  }
  catch (...)
  {
    logger->critical << "Unknown exception caught will cleaning up connections"
                     << std::flush;
  }

  // Re-lock the mutex
  lock.lock();

}

/*
 *  QuicRServerH3Session::PacketHandler()
 *
 *  Description:
 *      This function handles packets received from the network.
 *
 *  Parameters:
 *      stream_context [in]
 *          The transport context / stream ID information
 *
 *      packet [in]
 *          The packet received from the network.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
QuicRServerH3Session::PacketHandler(const StreamContext& stream_context,
                                    quicr::bytes& packet)
{
  H3ServerConnectionPointer connection;

  LOGGER_DEBUG(logger,
               "Received datagram of size " << packet.size() << " octets");

  // Process the QUIC header
  connection = ProcessQUICHeader(stream_context, packet);

  // If we have a connection, the process the packet
  if (connection) {
    connection->ProcessPacket(packet);

    // Remove this connection from the map if the connection is closed
    if (connection->IsConnectionClosed()) {
      ConnectionClosed(connection->GetConnectionID());
    }
  }
}

/*
 *  QuicRServerH3Session::ProcessQUICHeader()
 *
 *  Description:
 *      This function will process the QUIC header, issuing a "Retry" for
 *      new connections and returning a reference to the connection object
 *      for this data packet.
 *
 *  Parameters:
 *      stream_context [in]
 *          The transport context / stream ID information
 *
 *      packet [in]
 *          The packet received from the network.
 *
 *  Returns:
 *      Connection object associated with this data packet.
 *
 *  Comments:
 *      None.
 */
H3ServerConnectionPointer
QuicRServerH3Session::ProcessQUICHeader(const StreamContext& stream_context,
                                        quicr::bytes& packet)
{
  std::uint8_t type;
  std::uint32_t version;
  QUICConnectionID scid;
  std::size_t scid_length = scid.GetDataLength();
  QUICConnectionID dcid;
  std::size_t dcid_length = dcid.GetDataLength();
  QUICToken token;
  std::size_t token_length = token.GetDataLength();
  int result;

  result = QuicheCall(quiche_header_info,
                      packet.data(),
                      packet.size(),
                      Connection_ID_Len,
                      &version,
                      &type,
                      scid.GetDataBuffer(),
                      &scid_length,
                      dcid.GetDataBuffer(),
                      &dcid_length,
                      token.GetDataBuffer(),
                      &token_length);

  // Log any error and return
  if (result < 0) {
    logger->warning << "Failed to process QUIC header: " << result
                    << " (could be an invalid packet)" << std::flush;

    // Connection will point to nothing
    return {};
  }

  // Adjust the identifier and token data lengths
  scid.SetDataLength(scid_length);
  dcid.SetDataLength(dcid_length);
  token.SetDataLength(token_length);

  LOGGER_DEBUG(logger,
               "Received message type " << QuicheQUICMsgTypeString(type)
                                        << " with SCID " << scid << " and DCID "
                                        << dcid);

  // Protect the shared server data
  std::unique_lock<std::mutex> server_protect(server_lock);

  // Locate the connection given the connection ID
  auto item = connections.find(dcid);

  // If found, return the connection object
  if (item != connections.end()) return item->second;

  // If not found, unlock the mutex and proceed
  server_protect.unlock();

  // Given this is a new connection, the only two message types we
  // should receive is Initial or Retry
  if ((type != QuicheQUICMsgInitial) && (type != QuicheQUICMsgRetry)) {
    logger->warning << "Invalid message received: "
                    << QuicheQUICMsgTypeString(type) << std::flush;
    return {};
  }

  // Handle the new incoming connection
  return HandleNewConnection(version, scid, dcid, token, stream_context);
}

/*
 *  QuicRServerH3Session::HandleNewConnection()
 *
 *  Description:
 *      This function will handle data packets that appear to be for new
 *      connections.
 *
 *  Parameters:
 *      version [in]
 *          The version number found in the QUIC header.
 *
 *      scid [in]
 *          Source connection ID from the QUIC header.
 *
 *      dcid [in]
 *          Destination connection ID from the QUIC header.
 *
 *      token [in]
 *          Token received in the QUIC header.
 *
 *      stream_context [in]
 *          The transport context / stream ID information
 *
 *  Returns:
 *      Connection object associated with this data packet.
 *
 *  Comments:
 *      None.
 */
H3ServerConnectionPointer
QuicRServerH3Session::HandleNewConnection(std::uint32_t version,
                                          const QUICConnectionID& scid,
                                          const QUICConnectionID& dcid,
                                          const QUICToken& token,
                                          const StreamContext& stream_context)
{
  H3ServerConnectionPointer connection;
  QUICConnectionID original_dcid;

  // Check the version and renegotiate if necessary
  if (!QuicheCall(quiche_version_is_supported, version)) {
    logger->info << "Received version " << version << " with SCID " << scid
                 << " and DCID " << dcid << "; renegotiating" << std::flush;
    NegotiateVersion(scid, dcid, stream_context);
    return connection;
  }

  // If there is no token, then request the client to retry (send a token)
  if (token.GetDataLength() == 0) {
    logger->info << "Requesting a retry; with SCID " << scid << " and DCID "
                 << dcid << std::flush;
    RequestRetry(version, scid, dcid, stream_context);
    return connection;
  }

  // Validate that the token is correct
  if (!ValidateToken(token, stream_context, original_dcid)) {
    logger->warning << "Token received from the client failed to validate"
                    << std::flush;
    return connection;
  }

  sockaddr_storage temp_address;
  auto result = TransportCall([&]() {
      return transport->getPeerAddrInfo(stream_context.first, &temp_address);
  });
  if (!result)
  {
    logger->warning << "Unable to get peer connection address" << std::flush;
    return connection;
  }
  cantina::NetworkAddress remote_address(&temp_address,
                                         sizeof(sockaddr_storage));

  // Accept the connection
  quiche_conn* quiche_connection = QuicheCall(
    quiche_accept,
    dcid.GetDataBuffer(),
    dcid.GetDataLength(),
    original_dcid.GetDataBuffer(),
    original_dcid.GetDataLength(),
    reinterpret_cast<const sockaddr*>(local_address.GetAddressStorage()),
    local_address.GetAddressStorageSize(),
    reinterpret_cast<const sockaddr*>(remote_address.GetAddressStorage()),
    remote_address.GetAddressStorageSize(),
    server_config);

  if (quiche_connection == nullptr) {
    logger->error << "Failed to accept incoming connection" << std::flush;
    return connection;
  }

  logger->info << "Connection accepted from " << remote_address << std::flush;

  try {
    connection = std::make_shared<H3ServerConnection>(
      logger,
      timer_manager,
      async_requests,
      transport,
      stream_context,
      pub_sub_registry,
      Max_Send_Size,
      Max_Recv_Size,
      use_datagrams,
      dcid,
      local_address,
      remote_address,
      quiche_connection,
      Heartbeat_Interval,
      [&, dcid, stream_context]() {
        CloseNetworkStream(stream_context);
        ConnectionClosed(dcid);
      },
      server_delegate);
  } catch (const QuicRServerH3SessionException& e) {
    logger->error << "Failed to create a QUIC Connection: " << e.what()
                  << std::flush;

    // Error code from RFC 9000 Section 20.1 ("Internal error")
    QuicheCall(quiche_conn_close, quiche_connection, false, 0x01, nullptr, 0);

    connection.reset();
  }

  // Protect the shared server data
  std::lock_guard<std::mutex> server_protect(server_lock);

  // Add the connection to the connections map, if created
  if (connection) connections[dcid] = connection;

  return connection;
}

/*
 *  QuicRServerH3Session::ConnectionClosed()
 *
 *  Description:
 *      This function is called when a QUIC connection is closed.  This is not
 *      called to close a connection, but rather is called when a connection
 *      is already closed.  This function moves the associated connection object
 *      to a list for later disposal by the cleanup thread.
 *
 *  Parameters:
 *      connection_id [in]
 *          The connection ID that is connection that closed.
 *
 *  Returns:
 *      Nothing
 *
 *  Comments:
 *      None.
 */
void
QuicRServerH3Session::ConnectionClosed(const QUICConnectionID& connection_id)
{
  // Lock the server mutex
  std::lock_guard<std::mutex> lock(server_lock);

  // Just return if terminating
  if (terminate) return;

  // Locate the connection given the connection ID
  auto item = connections.find(connection_id);

  // If found, we move that to a separate vector
  if (item != connections.end()) {
    closed_connections.push_back(item->second);
    connections.erase(item);
    cv.notify_one();
  }
}

/*
 *  QuicRServerH3Session::CloseNetworkStream()
 *
 *  Description:
 *      Close the given stream under the associated transport context.
 *
 *  Parameters:
 *      stream_context [in]
 *          The transport context Id / stream ID to clean up.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The client mutex should be locked when calling this function.
 */
void
QuicRServerH3Session::CloseNetworkStream(
  const StreamContext& stream_context)
{
  TransportCall([&]() {
    transport->closeStream(stream_context.first, stream_context.second);
  });
}

/*
 *  QuicRServerH3Session::WorkerIdleLoop()
 *
 *  Description:
 *      This function is called by a worker thread.  It sits here until notified
 *      of an incoming packet or request to clean up stale connections.
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
void
QuicRServerH3Session::WorkerIdleLoop()
{
  std::unique_lock<std::mutex> lock(server_lock);

  logger->info << "Starting worker thread" << std::flush;

  while (true) {
    // Wait for new packets, connection closure, or termination
    cv.wait(lock, [&]() {
      return (terminate == true) || (!closed_connections.empty()) ||
             (!packet_queue.empty());
    });

    // Stop if terminating
    if (terminate) break;

    // Are there packets to handle
    if (!packet_queue.empty()) ProcessPackets(lock);

    // Are there connections to close?
    if (!closed_connections.empty()) ConnectionCleanup(lock);
  }

  logger->info << "Worker thread exiting" << std::flush;
}

/*
 *  QuicRServerH3Session::ConnectionCleanup()
 *
 *  Description:
 *      This function is called by the worker thread to clean up connections
 *      after they are moved onto the closed connections list.
 *
 *  Parameters:
 *      lock [in]
 *          A mutex locked in a locked state.  It will be unlocked in this
 *          function temporarily in order to destroy connections.  It is passed
 *          here just to avoid excess locking and unlocking.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
QuicRServerH3Session::ConnectionCleanup(std::unique_lock<std::mutex> &lock)
{
  std::vector<H3ServerConnectionPointer> connection_list;

  // Swap the closed connections list so they can be removed
  std::swap(connection_list, closed_connections);

  // Unlock the mutex, just in case the connection object has a thread extended
  // into this object while it is destroyed in the next line of code.
  lock.unlock();

  try
  {
    // Empty the local list (destroying connections)
    connection_list.clear();
  }
  catch (const std::exception &e)
  {
    logger->critical << "Exception caught cleaning up connections: " << e.what()
                     << std::flush;
  }
  catch (...)
  {
    logger->critical << "Unknown exception caught will cleaning up connections"
                     << std::flush;
  }

  // Re-lock the mutex
  lock.lock();
}

/*
 *  QuicRServerH3Session::NegotiateVersion()
 *
 *  Description:
 *      This function will send a version negotiation packet to a remote client.
 *
 *  Parameters:
 *      scid [in]
 *          Source connection ID from the QUIC header.
 *
 *      dcid [in]
 *          Destination connection ID from the QUIC header.
 *
 *      stream_context [in]
 *          The transport context / stream ID information.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
QuicRServerH3Session::NegotiateVersion(const QUICConnectionID& scid,
                                       const QUICConnectionID& dcid,
                                       const StreamContext& stream_context)
{
  quicr::bytes version_packet(Max_Recv_Size);
  ssize_t size;

  // Negotiate the version by creating a version negotiation packet
  size = QuicheCall(quiche_negotiate_version,
                    scid.GetDataBuffer(),
                    scid.GetDataLength(),
                    dcid.GetDataBuffer(),
                    dcid.GetDataLength(),
                    version_packet.data(),
                    version_packet.size());

  // If we have data to send, send it
  if (size > 0) {
    version_packet.resize(size);
    auto result = TransportCall([&]() {
      return transport->enqueue(
        stream_context.first, stream_context.second, std::move(version_packet));
    });
    if (result != qtransport::TransportError::None) {
      logger->error << "Failed to send version negotiation packet: "
                    << unsigned(result) << std::flush;
    }
  } else {
    logger->error << "Unable to create version negotiation packet"
                  << std::flush;
  }
}

/*
 *  QuicRServerH3Session::RequestRetry()
 *
 *  Description:
 *      This function will respond to a client's initial message and request
 *      a Retry packet.
 *
 *  Parameters:
 *      version [in]
 *          Protocol version to advertise in the Retry.
 *
 *      scid [in]
 *          Source connection ID from the QUIC header.
 *
 *      dcid [in]
 *          Destination connection ID from the QUIC header.
 *
 *      stream_context [in]
 *          The transport context / stream ID information.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
QuicRServerH3Session::RequestRetry(std::uint32_t version,
                                   const QUICConnectionID& scid,
                                   const QUICConnectionID& dcid,
                                   const StreamContext& stream_context)
{
  quicr::bytes retry_packet(Max_Recv_Size);
  ssize_t size;

  // Create a token
  QUICToken token = NewToken(dcid, stream_context);

  // Create a new connection ID for the server connection
  QUICConnectionID our_cid = CreateConnectionID();

  // Create a Retry packet
  size = QuicheCall(quiche_retry,
                    scid.GetDataBuffer(),
                    scid.GetDataLength(),
                    dcid.GetDataBuffer(),
                    dcid.GetDataLength(),
                    our_cid.GetDataBuffer(),
                    our_cid.GetDataLength(),
                    token.GetDataBuffer(),
                    token.GetDataLength(),
                    version,
                    retry_packet.data(),
                    retry_packet.size());

  // If we have data to send, send it
  if (size > 0) {
    retry_packet.resize(size);
    auto result = TransportCall([&]() {
      return transport->enqueue(
        stream_context.first, stream_context.second, std::move(retry_packet));
    });
    if (result != qtransport::TransportError::None) {
      logger->error << "Failed to send Retry packet" << std::flush;
    }
  } else {
    logger->error << "Unable to create Retry packet" << std::flush;
  }
}

/*
 *  QuicRServerH3Session::NewToken()
 *
 *  Description:
 *      This function will create a new token to send to the client.
 *
 *  Parameters:
 *      dcid [in]
 *          Destination connection ID from the QUIC header.
 *
 *      stream_context [in]
 *          The transport context / stream ID information.
 *
 *  Returns:
 *      A newly created QUIC token.
 *
 *  Comments:
 *      The token has this form:
 *          [SERVER PREFIX] [STREAM CONTEXT] [CONNECTION ID]
 */
QUICToken
QuicRServerH3Session::NewToken(const QUICConnectionID& dcid,
                               const StreamContext& stream_context)
{
  // Create a token with the random token prefix
  QUICToken token(token_prefix);

  // Append the stream context values
  token.Append(reinterpret_cast<const std::uint8_t*>(&stream_context.first),
               sizeof(stream_context.first));
  token.Append(reinterpret_cast<const std::uint8_t*>(&stream_context.second),
               sizeof(stream_context.second));

  // Append the dcid value
  token.Append(dcid.GetData());

  return token;
}

/*
 *  QuicRServerH3Session::ValidateToken()
 *
 *  Description:
 *      This function will validate the token received from the client.
 *
 *  Parameters:
 *      token [in]
 *          The token to validate.
 *
 *      stream_context [in]
 *          The transport context / stream ID information.
 *
 *      dcid [out]
 *          Destination connection ID inserted into the token. For Initial
 *          packets received from a client, this would be the dcid value
 *          the client used in that Initial packet.
 *
 *  Returns:
 *      True if the token validation passes, false if it does not.  If true,
 *      the dcid will contain the Connection ID extracted from the token.
 *
 *  Comments:
 *      None.
 */
bool
QuicRServerH3Session::ValidateToken(const QUICToken& token,
                                    const StreamContext& stream_context,
                                    QUICConnectionID& dcid)
{
  // Create a token without a connection ID
  QUICToken generated_token = NewToken(QUICConnectionID(0), stream_context);

  // The received token should be larger than this generated token, as the
  // generated token doesn't have a connection ID
  if (generated_token.GetDataLength() >= token.GetDataLength()) return false;

  // Ensure all of the octets in the generated token match the same octet
  // positions of the received token
  for (std::size_t i = 0; i < generated_token.GetDataLength(); i++) {
    if (generated_token[i] != token[i]) return false;
  }

  // The original connection ID should be octets in the received token beyond
  // the octets validated above
  dcid.Assign(token.GetDataBuffer() + generated_token.GetDataLength(),
              token.GetDataLength() - generated_token.GetDataLength());

  return true;
}

/*
 *  QuicRServerH3Session::CreateConnectionID()
 *
 *  Description:
 *      This function will create a new QUIC connection identifier.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      A newly created QUIC connection ID value.
 *
 *  Comments:
 *      None.
 */
QUICConnectionID
QuicRServerH3Session::CreateConnectionID()
{
  QUICConnectionID connection_id;

  // Fill the connection ID buffer with random octets
  for (std::size_t i = 0; i < connection_id.GetDataLength(); i++) {
    connection_id[i] = GetRandomOctet();
  }

  return connection_id;
}

/*
 * QuicRServerH3Session::GetRandomOctet()
 *
 *  Description:
 *      This function will return a single random octet.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Random character value in the range of 0..255.
 *
 *  Comments:
 *      None.
 */
inline std::uint8_t
QuicRServerH3Session::GetRandomOctet()
{
  std::uniform_int_distribution<int> random_octet(0, 255);

  return random_octet(generator);
}

////////////////////////////////////////////////////////////////////////////
// Functions to satisfy the Transport & QuicRServer interface
////////////////////////////////////////////////////////////////////////////

// Transport API
bool
QuicRServerH3Session::is_transport_ready()
{
  if (TransportCall([&]() { return transport->status(); }) ==
      qtransport::TransportStatus::Ready) {
    return true;
  } else {
    return false;
  }
}

/**
 * @brief Run Server API event loop
 *
 * @details This method will open listening sockets and run an event loop
 *    for callbacks.
 *
 * @returns true if error, false if no error
 *          (Note: laps expects a true response, so this comment may be wrong)
 */
bool
QuicRServerH3Session::run()
{
  while (TransportCall([&]() { return transport->status(); }) ==
         qtransport::TransportStatus::Connecting) {
    logger->info << "Waiting for server to be ready" << std::flush;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return TransportCall([&]() { return transport->status(); }) ==
             qtransport::TransportStatus::Ready
           ? true
           : false;
}

/**
 * @brief Send publish intent response
 *
 * @param quicr_namespace       : Identifies QUICR namespace
 * @param result                : Status of Publish Intetn
 *
 * @details Entities processing the Subscribe Request MUST validate the
 * request
 * @todo: Add payload with origin signed blob
 */
void
QuicRServerH3Session::publishIntentResponse(
  const quicr::Namespace& quicr_namespace,
  const PublishIntentResult& result)
{
  // Locate the connection associated with this namespace
  auto publisher = pub_sub_registry->FindPublisher(quicr_namespace);
  if (!publisher.has_value()) {
    logger->warning << "Could not find publisher record" << std::flush;
    return;
  }
  if (publisher->publisher == false) {
    logger->error << "publishIntentResponse publisher ID is not a publisher";
    return;
  }
  QUICConnectionID dcid = (*publisher).connection_id;

  // Protect the shared server data
  std::unique_lock<std::mutex> server_protect(server_lock);

  // Locate the connection given the connection ID
  auto item = connections.find(dcid);

  // If connection not found, just return
  if (item == connections.end()) {
    logger->warning << "Could not find connection in publishIntentResponse"
                    << std::flush;
    return;
  }

  // Get the shared pointer to the connection
  auto connection = item->second;

  // Release the lock
  server_protect.unlock();

  // Forward the response to the connection, passing the publisher record to
  // avoid a second query by the connection object
  connection->PublishIntentResponse(*publisher, result);
}

/**
 * @brief Send subscribe response
 *
 * @details Entities processing the Subscribe Request MUST validate the
 * request
 *
 * @param subscriber_id         : Subscriber ID to send the message to
 * @param quicr_namespace       : Identifies QUICR namespace // TODO: Why!?
 * @param result                : Status of Subscribe operation
 *
 */
void
QuicRServerH3Session::subscribeResponse(
  const uint64_t& subscriber_id,
  [[maybe_unused]] const quicr::Namespace& quicr_namespace,
  const SubscribeResult& result)
{
  // Locate the connection associated with this namespace
  auto subscriber = pub_sub_registry->FindRecord(subscriber_id);
  if (!subscriber.has_value()) {
    logger->warning << "subscribeResponse could not find subscriber record"
                    << std::flush;
    return;
  }
  if (subscriber->publisher == true) {
    logger->error << "subscriptionEnded subscriber ID is not a subscriber";
    return;
  }
  QUICConnectionID dcid = (*subscriber).connection_id;

  // Protect the shared server data
  std::unique_lock<std::mutex> server_protect(server_lock);

  // Locate the connection given the connection ID
  auto item = connections.find(dcid);

  // If connection not found, just return
  if (item == connections.end()) {
    logger->warning << "subscribeResponse could not find connection"
                    << std::flush;
    return;
  }

  // Get the shared pointer to the connection
  auto connection = item->second;

  // Release the lock
  server_protect.unlock();

  // Forward the response to the connection, passing the subscriber record to
  // avoid a second query by the connection object
  connection->SubscribeResponse(*subscriber, result);
}

/**
 * @brief Send subscription end message
 *
 * @details  Subscription can terminate when a publisher terminated
 *           the stream or subscription timeout, upon unsubscribe,
 *           or other application reasons
 *
 * @param subscriber_id         : Subscriber ID to send the message to
 * @param quicr_namespace       : Identifies QUICR namespace
 * @param reason                : Reason of Subscribe End operation
 *
 */
void
QuicRServerH3Session::subscriptionEnded(
  const uint64_t& subscriber_id,
  const quicr::Namespace& quicr_namespace,
  const SubscribeResult::SubscribeStatus& reason)
{
  // Locate the connection associated with this namespace
  auto subscriber = pub_sub_registry->FindRecord(subscriber_id);
  if (!subscriber.has_value()) {
    logger->warning << "subscriptionEnded could not find subscriber record"
                    << std::flush;
    return;
  }
  if (subscriber->publisher == true) {
    logger->error << "subscriptionEnded subscriber ID is not a subscriber";
    return;
  }
  QUICConnectionID dcid = (*subscriber).connection_id;

  // Protect the shared server data
  std::unique_lock<std::mutex> server_protect(server_lock);

  // Locate the connection given the connection ID
  auto item = connections.find(dcid);

  // If connection not found, just return
  if (item == connections.end()) {
    logger->warning << "subscriptionEnded could not find connection"
                    << std::flush;
    return;
  }

  // Get the shared pointer to the connection
  auto connection = item->second;

  // Release the lock
  server_protect.unlock();

  // Forward the reason to the connection, passing the subscriber record to
  // avoid a second query by the connection object
  connection->SubscriptionEnded(*subscriber, quicr_namespace, reason);
}

/**
 * @brief Send a named QUICR media object
 *
 * @param subscriber_id            : Subscriber ID to send the message to
 * @param use_reliable_transport   : Indicates the preference for the object's
 *                                   transport, if forwarded.
 * @param priority                 : Identifies the relative priority of the
 *                                   current object
 * @param expiry_age_ms            : Time hint for the object to be in cache
 *                                   before being purged after reception
 * @param datagram                 : QuicR Publish Datagram to send
 *
 */
void
QuicRServerH3Session::sendNamedObject(const uint64_t& subscriber_id,
                                      bool use_reliable_transport,
                                      [[maybe_unused]] uint8_t priority,
                                      [[maybe_unused]] uint16_t expiry_age_ms,
                                      const messages::PublishDatagram& datagram)
{
  // Locate the connection associated with this namespace
  auto subscriber = pub_sub_registry->FindRecord(subscriber_id);
  if (!subscriber.has_value()) {
    logger->warning << "sendNamedObject could not find subscriber record"
                    << std::flush;
    return;
  }
  if (subscriber->publisher == true) {
    logger->error << "sendNamedObject subscriber ID is not a subscriber";
    return;
  }
  QUICConnectionID dcid = (*subscriber).connection_id;

  // Protect the shared server data
  std::unique_lock<std::mutex> server_protect(server_lock);

  // Locate the connection given the connection ID
  auto item = connections.find(dcid);

  // If connection not found, just return
  if (item == connections.end()) {
    logger->warning << "sendNamedObject could not find connection"
                    << std::flush;
    return;
  }

  // Get the shared pointer to the connection
  auto connection = item->second;

  // Release the lock
  server_protect.unlock();

  // Forward the reason to the connection, passing the subscriber record to
  // avoid a second query by the connection object
  connection->SendNamedObject(*subscriber, use_reliable_transport, datagram);
}

////////////////////////////////////////////////////////////////////////////
// End of Interface Functions
////////////////////////////////////////////////////////////////////////////

} // namespace quicr::h3
