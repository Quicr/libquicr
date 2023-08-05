/*
 *  quicr_client_h3_session.cpp
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      This file implements a session layer between the client APIs and the
 *      transport that uses HTTP/3 to communicate with a server.
 *
 *      NOTE: This module has the ability to support a plurality of connections,
 *            but the API defined in libquicr assumes there is one connection
 *            per session object (thus the reason for a single address in the
 *            constructor).  Removing the connection map would simplify this
 *            code.
 *
 *  Portability Issues:
 *      None.
 */

#include <chrono>
#include <functional>
#include <sstream>
#include "quicr_client_h3_session.h"
#include "cantina/logger_macros.h"
#include "cantina/memory_allocator.h"
#include "cantina/memory_manager.h"
#include "quic_identifier.h"
#include "quiche_api_lock.h"
#include "transport_api_lock.h"
#include "quiche_types.h"
#include "h3_common.h"

namespace quicr::h3 {

/*
 *  QuicRClientH3Session::QuicRClientH3Session()
 *
 *  Description:
 *      Constructor for the QuicRClientH3Session object.
 *
 *  Parameters:
 *      relay_info [in]
 *          Relay information.
 *
 *      transport_config [in]
 *          Transport configuration information.
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
QuicRClientH3Session::QuicRClientH3Session(
  RelayInfo& relay_info,
  qtransport::TransportConfig transport_config,
  qtransport::LogHandler& transport_logger)
  : terminate{ false }
  , logger{ nullptr }
  , timer_manager{ nullptr }
  , async_requests{ nullptr }
  , relay_info{ relay_info }
  , transport{ nullptr }
  , transport_context{ 0 }
  , transport_delegate{ this }
  , local_address{ "0.0.0.0" } // TODO: This should be provided not assumed
  , certificate{ (transport_config.tls_cert_filename
                    ? transport_config.tls_cert_filename
                    : "") }
  , certificate_key{ (transport_config.tls_key_filename
                        ? transport_config.tls_key_filename
                        : "") }
  , verify_certificate{ false } // TODO: We should signal this
  , use_datagrams{ true }       // TODO: We should signal this
  , client_config{ nullptr }
  , generator{ std::random_device()() }
  , pub_sub_registry{ std::make_shared<PubSubRegistry>() }
{
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

  // Create the logging object
  logger = std::make_shared<cantina::CustomLogger>("H3", logging_function);

  logger->info << "QuicRClientH3Session starting" << std::flush;

  // Configure Quiche
  try {
    ConfigureQuiche();
  } catch (...) {
    if (client_config) quiche_config_free(client_config);
    throw;
  }

  // Create a thread pool for use by the TimerManager
  auto thread_pool = std::make_shared<cantina::ThreadPool>(logger, 5, 10);

  // Create the TimerManager object
  timer_manager = std::make_shared<cantina::TimerManager>(logger, thread_pool);

  // Create an AsyncRequests object to facilitate asynchronous callbacks
  async_requests = std::make_shared<cantina::AsyncRequests>(thread_pool);

  // Create the worker thread to handle packets and connection cleanup
  worker_thread = std::thread([&]() { WorkerIdleLoop(); });

  // Set up remote transport info
  qtransport::TransportRemote transport_remote;
  transport_remote.host_or_ip = relay_info.hostname;
  transport_remote.port = relay_info.port;
  transport_remote.proto = qtransport::TransportProtocol::UDP;

  transport = qtransport::ITransport::make_client_transport(
    transport_remote, transport_config, transport_delegate, transport_logger);

  // Resolve the remote address
  remote_address = cantina::FindIPv4Address(
    relay_info.hostname, std::to_string(relay_info.port));

  // If unable to resolve the address, throw an exception
  if (!remote_address) {
    throw QuicRClientH3SessionException("Unable to resolve remote address");
  }

  logger->info << "QuicRClientH3Session started" << std::flush;
}

/*
 *  QuicRClientH3Session::~QuicRClientH3Session()
 *
 *  Description:
 *      Destructor for the QuicRClientH3Session object.
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
QuicRClientH3Session::~QuicRClientH3Session()
{
  logger->info << "QuicRClientH3Session terminating" << std::flush;

  // Lock the mutex, set terminate to true, and unlock
  std::unique_lock<std::mutex> lock(client_lock);
  terminate = true;
  lock.unlock();

  // Stop the worker thread
  cv.notify_one();
  worker_thread.join();

  // Destroy all connection objects
  connections.clear();
  closed_connections.clear();

  // Terminate the transport context
  if (transport_context > 0)
  {
    TransportCall([&]() { transport->close(transport_context); });
  }

  // Destroy the transport (preventing further callbacks)
  transport.reset();

  // Wait for asynchronous requests to complete
  async_requests.reset();

  // Destroy the timer manager
  timer_manager.reset();

  // Release the configuration
  quiche_config_free(client_config);

  logger->info << "QuicRClientH3Session terminated" << std::flush;
}

/*
 *  QuicRClientH3Session::ConfigureQuiche()
 *
 *  Description:
 *      Configure Quiche.
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
QuicRClientH3Session::ConfigureQuiche()
{
  // Initialize the Quiche code
  if ((client_config = quiche_config_new(QUICHE_PROTOCOL_VERSION)) == nullptr) {
    throw QuicRClientH3SessionException("Unable to initialize Quiche module");
  }

  // Enable debug logging for debugging
  if (logger->GetLogLevel() == cantina::LogLevel::Debug) {
    quiche_enable_debug_logging(&QuicRClientH3Session::quiche_log, this);
  }

  // Specify the certificate files to use (if given)
  if (!certificate.empty() && !certificate_key.empty()) {
    if (quiche_config_load_cert_chain_from_pem_file(client_config,
                                                    certificate.c_str())) {
      throw QuicRClientH3SessionException(
        std::string("Failed to load certificate file: ") + certificate);
    }
    if (quiche_config_load_priv_key_from_pem_file(client_config,
                                                  certificate_key.c_str())) {
      throw QuicRClientH3SessionException(
        std::string("Failed to load certificate key file: ") + certificate_key);
    }
  }

  // Set the application protocols
  if (quiche_config_set_application_protos(
        client_config,
        reinterpret_cast<const uint8_t*>(QUICHE_H3_APPLICATION_PROTOCOL),
        sizeof(QUICHE_H3_APPLICATION_PROTOCOL) - 1)) {
    throw QuicRClientH3SessionException(
      "Failed to set TLS application protocols");
  }

  // Specify connection timeout in ms
  quiche_config_set_max_idle_timeout(client_config, Connection_Timeout);

  // Define max send/receive UDP buffer sizes
  quiche_config_set_max_recv_udp_payload_size(client_config, Max_Recv_Size);
  quiche_config_set_max_send_udp_payload_size(client_config, Max_Recv_Size);

  // Set the size of the incoming buffer stream
  quiche_config_set_initial_max_data(client_config, 10'000'000);

  // Buffer for each locally-initiated bidirectional stream (needed here?)
  quiche_config_set_initial_max_stream_data_bidi_local(client_config,
                                                       1'000'000);

  // Buffer for each remote-initiated bidirectional stream
  quiche_config_set_initial_max_stream_data_bidi_remote(client_config,
                                                        1'000'000);

  // Initial buffer for remote-initiated unidirectional streams
  quiche_config_set_initial_max_stream_data_uni(client_config, 1'000'000);

  // Max number of remote bidirectional streams initiated
  quiche_config_set_initial_max_streams_bidi(client_config, 100);

  // Max number of remote unidirectional streams initiated
  quiche_config_set_initial_max_streams_uni(client_config, 100);

  // Disable active migration (See section 9 of RFC 9000)
  quiche_config_set_disable_active_migration(client_config, true);

  // Congestion control algorithm (RFC 9002)
  quiche_config_set_cc_algorithm(client_config, QUICHE_CC_RENO);

  // Verify the peer certificate?
  quiche_config_verify_peer(client_config, verify_certificate);

  // Enable datagram support
  if (use_datagrams) {
    quiche_config_enable_dgram(client_config, true, 1'000, 1'000);
  }
}

/*
 *  QuicRClientH3Session::CreateNewConnection()
 *
 *  Description:
 *      This function is used to initiate a new remote connection.
 *
 *  Parameters:
 *      hostname [in]
 *          The name of the remote host.
 *
 *      remote_address [in]
 *          Remote address and port to which to connect.
 *
 *  Returns:
 *      A pair indicating success or failure and, in the case of success (true),
 *      the session context for this connection.  Success does not indicate
 *      establishment, but merely that the request is now being served.  If
 *      false is returned, the session context value has no meaning.
 *
 *  Comments:
 *      None.
 */
std::pair<bool, StreamContext>
QuicRClientH3Session::CreateNewConnection(
  const std::string& hostname,
  const cantina::NetworkAddress& remote_address)
{
  // Get the session ID associated with this connection
  // NOTE: there is only one connection possible with qtransport, but this
  //       we should really have the ability to specify multiple context
  //       if we want to have a plurality of clients.  For now, there is just
  //       a single client connection object associated with a single session.
  auto stream_id = TransportCall(
    [&]() { return transport->createStream(transport_context, false); });

  // Form the StreamContext info that gets passed throughout
  auto stream_context = std::make_pair(transport_context, stream_id);

  // Create the connection data object
  ConnectionData connection_data{};

  // Set the remote address
  connection_data.remote_address = remote_address;

  // Set the connection address based on the successfully opened port
  connection_data.local_address = local_address;

  // Create the QUIC connection ID for this connection
  connection_data.id = CreateConnectionID();
  logger->info << "Creating new connection having ID = " << connection_data.id
               << std::flush;

  // Lock the client mutex
  std::unique_lock<std::mutex> lock(client_lock);

  // Create the Quiche connection
  quiche_conn* quiche_connection =
    QuicheCall(quiche_connect,
               hostname.c_str(),
               connection_data.id.GetDataBuffer(),
               connection_data.id.GetDataLength(),
               reinterpret_cast<const sockaddr*>(
                 connection_data.local_address.GetAddressStorage()),
               connection_data.local_address.GetAddressStorageSize(),
               reinterpret_cast<const sockaddr*>(
                 connection_data.remote_address.GetAddressStorage()),
               connection_data.remote_address.GetAddressStorageSize(),
               client_config);

  if (quiche_connection == nullptr) {
    // Unlock the mutex
    lock.unlock();

    // Close the stream since there was a failure
    CloseNetworkStream(stream_context);

    logger->error << "Unable to create Quiche connection" << std::flush;

    return { false, {} };
  }

  try {
    connection_data.connection = std::make_shared<H3ClientConnection>(
      logger,
      timer_manager,
      async_requests,
      transport,
      stream_context,
      pub_sub_registry,
      Max_Send_Size,
      Max_Recv_Size,
      use_datagrams,
      connection_data.id,
      connection_data.local_address,
      connection_data.remote_address,
      quiche_connection,
      Heartbeat_Interval,
      [&, stream_context]() {
        CloseNetworkStream(stream_context);
        ConnectionClosed(stream_context);
      },
      hostname);
  } catch (const H3ConnectionException& e) {
    logger->error << "Failed to create a QUIC Connection: " << e.what()
                  << std::flush;
    // Error code from RFC 9000 Section 20.1 ("Internal error")
    QuicheCall(quiche_conn_close, quiche_connection, false, 0x01, nullptr, 0);
    connection_data.connection.reset();

    // Unlock the mutex
    lock.unlock();

    // Free the connection structure
    QuicheCall(quiche_conn_free, quiche_connection);

    // Clean up the closed connection
    CloseNetworkStream(stream_context);

    return { false, {} };
  }

  // Store this connection in the map
  connections[stream_context] = connection_data;

  return { true, stream_context };
}

/*
 *  QuicRClientH3Session::IncomingPacketNotification()
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
 *          The stream identifier associated with this packet.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void QuicRClientH3Session::IncomingPacketNotification(
    const qtransport::TransportContextId& context_id,
    const qtransport::StreamId& stream_id)
{
  // Protect the shared server data
  std::lock_guard<std::mutex> server_protect(client_lock);

  // Notify the worker thread of new packet notifications
  packet_queue.emplace_back(context_id, stream_id);

  // Notify the worker thread of the new packet
  cv.notify_one();
}

/*
 *  QuicRClientH3Session::ProcessPackets()
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
QuicRClientH3Session::ProcessPackets(std::unique_lock<std::mutex> &lock)
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
 *  QuicRClientH3Session::PacketHandler()
 *
 *  Description:
 *      This function handles packets received by the network.
 *
 *  Parameters:
 *      stream_context [in]
 *          The transport context and stream identifier.
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
QuicRClientH3Session::PacketHandler(const StreamContext& stream_context,
                                    quicr::bytes& packet)
{
  LOGGER_DEBUG(logger,
               "Received datagram of size " << data_packet.GetDataLength()
                                            << " octets");

  // Lock the client mutex
  std::unique_lock<std::mutex> lock(client_lock);

  // Exit if terminating
  if (terminate) return;

  // Locate the connection given the registration ID, but just return if
  // it is not found (likely because the connection is closing)
  auto it = connections.find(stream_context);
  if (it == connections.end()) return;

  // Get a pointer to the connection
  auto connection = (*it).second.connection;

  // Unlock the mutex
  lock.unlock();

  // If we have a connection, the process the packet
  if (connection) {
    connection->ProcessPacket(packet);

    // If the QUIC connection closed, remove this connection
    if (connection->IsConnectionClosed()) ConnectionClosed(stream_context);
  }
}

/*
 *  QuicRClientH3Session::ConnectionClosed()
 *
 *  Description:
 *      This function is called when a QUIC connection is closed.  It is a
 *      callback function that is invoked by the H3ClientConnection object.
 *      When this function is called, the connection is already closed.
 *      This function merely moves the associated connection object to
 *      a list for later disposal.
 *
 *  Parameters:
 *      stream_context [in]
 *          The transport context and stream identifier that was closed.
 *
 *  Returns:
 *      Nothing
 *
 *  Comments:
 *      None.
 */
void
QuicRClientH3Session::ConnectionClosed(const StreamContext& stream_context)
{
  // Lock the client mutex
  std::lock_guard<std::mutex> lock(client_lock);

  // Just return if terminating
  if (terminate) return;

  // Locate the connection given the registration ID
  auto item = connections.find(stream_context);

  // If found, we move that to a separate vector
  if (item != connections.end()) {
    closed_connections[stream_context] = item->second;
    connections.erase(item);
    cv.notify_one();
  }
}

/*
 *  QuicRClientH3Session::CloseNetworkStream()
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
QuicRClientH3Session::CloseNetworkStream(
  const StreamContext& stream_context)
{
  TransportCall([&]() {
    transport->closeStream(stream_context.first, stream_context.second);
  });
}

/*
 *  QuicRClientH3Session::WorkerIdleLoop()
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
QuicRClientH3Session::WorkerIdleLoop()
{
  std::unique_lock<std::mutex> lock(client_lock);

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
 *  QuicRClientH3Session::ConnectionCleanup()
 *
 *  Description:
 *      This function is called by a cleanup thread that takes responsibility
 *      to clean up connections after they are moved onto the closed
 *      connections list.
 *
 *  Parameters:
 *      lock [in]
 *          The client mutex lock in a locked state.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
QuicRClientH3Session::ConnectionCleanup(std::unique_lock<std::mutex> &lock)
{
  ConnectionMap connection_list;

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
 *  QuicRClientH3Session::CreateConnectionID()
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
QuicRClientH3Session::CreateConnectionID()
{
  QUICConnectionID connection_id;

  // Fill the connection ID buffer with random octets
  for (std::size_t i = 0; i < connection_id.GetDataLength(); i++) {
    connection_id[i] = GetRandomOctet();
  }

  return connection_id;
}

/*
 * QuicRClientH3Session::GetRandomOctet()
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
QuicRClientH3Session::GetRandomOctet()
{
  std::uniform_int_distribution<int> random_octet(0, 255);

  return random_octet(generator);
}

////////////////////////////////////////////////////////////////////////////
// Functions to satisfy the QuicRClient interface
////////////////////////////////////////////////////////////////////////////

/**
 * @brief Connects the session using the info provided on construction.
 * @returns True if connected, false otherwise.
 */
bool
QuicRClientH3Session::connect()
{
  transport_context = transport->start();

  // Initiate remote connection
  if (!CreateNewConnection(relay_info.hostname, remote_address).first) {
    logger->error << "Unable to initiate remote connection" << std::flush;
    return false;
  }

  // Wait for the connection to be established
  while (!terminate && (status() == ClientStatus::CONNECTING)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // If the connection terminated, report false
  if (status() == ClientStatus::TERMINATED)
  {
    logger->error << "Failed to establish a connection" << std::endl;
    return false;
  }

  // If the client is not connected, destroy it
  if (status() != ClientStatus::READY)
  {
    logger->error << "Connection attempt timed out" << std::endl;
    disconnect();
    return false;
  }

  return true;
}

/**
 * @brief Disconnects the session from the relay.
 * @returns True if successful, false if some error occurred.
 */
bool
QuicRClientH3Session::disconnect()
{
  ConnectionMap connection_list;

  // Lock the client mutex
  std::unique_lock<std::mutex> lock(client_lock);

  // Swap the active connections list
  std::swap(connection_list, connections);

  // Unlock the mutex
  lock.unlock();

  try
  {
    // Empty the local list (destroying active connections)
    connection_list.clear();
  }
  catch (const std::exception &e)
  {
    logger->critical << "Exception caught cleaning up connections: " << e.what()
                     << std::flush;
    return false;
  }
  catch (...)
  {
    logger->critical << "Unknown exception caught will cleaning up connections"
                     << std::flush;
    return false;
  }

  // Re-lock the mutex
  lock.unlock();

  // Terminate the transport
  transport->close(transport_context);
  transport_context = 0;

  return true;
}

/**
 * @brief Get the client status
 *
 * @details This method should be used to determine if the client is
 *   connected and ready for publishing and subscribing to messages.
 *   Status will indicate the type of error if not ready.
 *
 * @returns client status
 */
ClientStatus
QuicRClientH3Session::status()
{
  ClientStatus status;

  // Get the connection
  H3ClientConnectionPointer connection = GetConnection();

  // If there is no connection, it must have terminated
  if (!connection) return ClientStatus::TERMINATED;

  H3ConnectionState state = connection->GetConnectionState();

  switch (state) {
    case H3ConnectionState::ConnectPending:
      status = ClientStatus::CONNECTING;
      break;
    case H3ConnectionState::Connected:
      status = ClientStatus::READY;
      break;
    case H3ConnectionState::Disconnected:
      status = ClientStatus::TERMINATED;
      break;
    default:
      status = ClientStatus::TERMINATED;
      break;
  }

  // TODO: Other client status are RELAY_HOST_INVALID, RELAY_PORT_INVALID,
  //       TRANSPORT_ERROR, UNAUTHORIZED. Some of those can be supported, but
  //       it's not critical for this prototype. Determining whether a host
  //       or port is invalid might be difficult to differentiate from
  //       a transport or network error.

  return status;
}

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
QuicRClientH3Session::publishIntent(
  std::shared_ptr<PublisherDelegate> pub_delegate,
  const quicr::Namespace& quicr_namespace,
  const std::string& origin_url,
  const std::string& auth_token,
  quicr::bytes&& payload)
{
  // Get the connection
  H3ClientConnectionPointer connection = GetConnection();

  // If there is no connection, return false
  if (!connection) {
    logger->warning << "publishIntent received without a connection"
                    << std::flush;
    return false;
  }

  return connection->PublishIntent(
    pub_delegate, quicr_namespace, origin_url, auth_token, std::move(payload));
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
QuicRClientH3Session::publishIntentEnd(const quicr::Namespace& quicr_namespace,
                                       const std::string& auth_token)
{
  // Get the connection
  H3ClientConnectionPointer connection = GetConnection();

  // If there is no connection, just return
  if (!connection) {
    logger->warning << "publishIntentEnd received without a connection"
                    << std::flush;
    return;
  }

  // Forward the request
  connection->PublishIntentEnd(quicr_namespace, auth_token);
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
QuicRClientH3Session::subscribe(
  std::shared_ptr<SubscriberDelegate> subscriber_delegate,
  const quicr::Namespace& quicr_namespace,
  const SubscribeIntent& intent,
  const std::string& origin_url,
  bool use_reliable_transport,
  const std::string& auth_token,
  quicr::bytes&& e2e_token)
{
  // Get the connection
  H3ClientConnectionPointer connection = GetConnection();

  // If there is no connection, just return
  if (!connection) {
    logger->warning << "subscribe received without a connection" << std::flush;
    return;
  }

  // Forward the request
  connection->Subscribe(subscriber_delegate,
                        quicr_namespace,
                        intent,
                        origin_url,
                        use_reliable_transport,
                        auth_token,
                        std::move(e2e_token));
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
QuicRClientH3Session::unsubscribe(const quicr::Namespace& quicr_namespace,
                                  const std::string& origin_url,
                                  const std::string& auth_token)
{
  // Get the connection
  H3ClientConnectionPointer connection = GetConnection();

  // If there is no connection, just return
  if (!connection) {
    logger->warning << "unsubscribe received without a connection"
                    << std::flush;
    return;
  }

  // Forward the request
  connection->Unsubscribe(quicr_namespace, origin_url, std::move(auth_token));
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
QuicRClientH3Session::publishNamedObject(const quicr::Name& quicr_name,
                                         uint8_t priority,
                                         uint16_t expiry_age_ms,
                                         bool use_reliable_transport,
                                         quicr::bytes&& data)
{
  // Get the connection
  H3ClientConnectionPointer connection = GetConnection();

  // If there is no connection, just return
  if (!connection) {
    logger->warning << "subscribe received without a connection" << std::flush;
    return;
  }

  // Forward the request
  connection->PublishNamedObject(quicr_name,
                                 priority,
                                 expiry_age_ms,
                                 use_reliable_transport,
                                 std::move(data));
}

/**
 * @brief Publish Named object
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
QuicRClientH3Session::publishNamedObjectFragment(
  [[maybe_unused]] const quicr::Name& quicr_name,
  [[maybe_unused]] uint8_t priority,
  [[maybe_unused]] uint16_t expiry_age_ms,
  [[maybe_unused]] bool use_reliable_transport,
  [[maybe_unused]] const uint64_t& offset,
  [[maybe_unused]] bool is_last_fragment,
  [[maybe_unused]] quicr::bytes&& data)
{
  // Get the connection
  H3ClientConnectionPointer connection = GetConnection();

  // If there is no connection, just return
  if (!connection) {
    logger->warning
      << "publishNamedObjectFragment received without a connection"
      << std::flush;
    return;
  }

  logger->warning << "publishNamedObjectFragment is not implemented"
                  << std::flush;

  // TODO: Not implemented
}

////////////////////////////////////////////////////////////////////////////
// End of functions to satisfy the QuicRClient interface
////////////////////////////////////////////////////////////////////////////

} // namespace quicr::h3
