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
#include "cantina/memory_allocator.h"
#include "cantina/memory_manager.h"
#include "quic_identifier.h"
#include "quiche_api_lock.h"
#include "quiche_types.h"
#include <chrono>
#include <functional>
#include <memory>
#include <sstream>

namespace quicr {

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
  , network{ nullptr }
  , server_delegate{ server_delegate }
  , certificate{ (transport_config.tls_cert_filename
                    ? transport_config.tls_cert_filename
                    : "") }
  , certificate_key{ (transport_config.tls_key_filename
                        ? transport_config.tls_key_filename
                        : "") }
  , server_config{ nullptr }
  , data_socket{ 0 }
  , generator{ std::random_device()() }
  , network_registration{}
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

  // Create a thread pool for use by the Network and TimerManager
  auto thread_pool = std::make_shared<cantina::ThreadPool>(logger, 5, 10);

  // Create the TimerManager object
  timer_manager = std::make_shared<cantina::TimerManager>(logger, thread_pool);

  // Create an AsyncRequests object to facilitate asynchronous callbacks
  async_requests = std::make_shared<cantina::AsyncRequests>(thread_pool);

  // Create a MemoryManager for use by Network
  auto memory_manager = std::make_shared<cantina::MemoryManager>(
    // clang-format off
    cantina::MemoryPoolConfig{
      { 256,           10 },
      { Max_Recv_Size,  5 },
      { 65536,          2 },
    },
    // clang-format on
    logger,
    false,
    true,
    true);
  MemoryAllocatorInitialize(memory_manager);

  // Define the local address on which to listen
  cantina::NetworkAddress listen_address(relay_info.hostname, relay_info.port);

  // Create the network object to manage network traffic
  network = std::make_shared<cantina::Network>(logger,
                                               thread_pool,
                                               memory_manager,
                                               nullptr,
                                               Max_Recv_Size,
                                               2,
                                               5,
                                               2,
                                               listen_address);

  // Configure Quiche
  try {
    ConfigureQuiche();
  } catch (...) {
    if (server_config) quiche_config_free(server_config);
    throw;
  }

  // Open the socket to listen for incoming packets
  if ((data_socket =
         network->OpenUDPSocket(listen_address, listen_address.GetPort())) ==
      cantina::Network::Socket_Error) {
    std::ostringstream oss;
    oss << "Failed to listen on " << listen_address;
    throw QuicRServerH3SessionException(oss.str());
  }

  // Set the local address based on the successfully opened port
  local_address = network->GetSocketAddress(data_socket);

  // Populate the server token prefix
  for (std::size_t i = 0; i < 7; i++) token_prefix.push_back(GetRandomOctet());

  // Register to receive datagrams
  network_registration = network->RegisterCallback(
    data_socket,
    [&](const cantina::RegistrationID& registration_id,
        cantina::DataPacket& data_packet) {
      PacketHandler(registration_id, data_packet);
    });

  // Create the thread to perform connection cleanup
  cleanup_thread = std::thread([&]() { ConnectionCleanup(); });

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

  // Unregister from the network (ensuring no threads running here)
  network->UnregisterCallback(network_registration);

  // Stop the thread closing connections
  cleanup_signal.notify_one();
  cleanup_thread.join();

  // At this point, there should be no other threads running in this object

  // Explicitly destroy all connection objects
  connections.clear();
  closed_connections.clear();

  // Close the network data socket
  network->CloseSocket(data_socket);

  // Release the configuration
  quiche_config_free(server_config);

  // Destroy objects created
  // TODO: This is required only because the qtransport logger is provided
  //       as a reference and then that reference is access when the
  //       MemoryManager terminates.  Resetting these objects forces
  //       destruction.
  network.reset();
  cantina::MemoryAllocatorInitialize(
    std::make_shared<cantina::MemoryManager>(cantina::MemoryPoolConfig{}),
    true);
  timer_manager.reset();

  // Wait for asynchronous requests to complete
  async_requests.reset();

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
 *  QuicRServerH3Session::PacketHandler()
 *
 *  Description:
 *      This function handles packets received by the network.
 *
 *  Parameters:
 *      (unused) [in]
 *          Registration ID associated with this callback.
 *
 *      data_packet [in]
 *          The data packet received from the network.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
QuicRServerH3Session::PacketHandler(const cantina::RegistrationID&,
                                    cantina::DataPacket& data_packet)
{
  H3ServerConnectionPointer connection;

  LOGGER_DEBUG(logger,
               "Received datagram of size " << data_packet.GetDataLength()
                                            << " octets from "
                                            << data_packet.GetAddress());

  // Process the QUIC header
  connection = ProcessQUICHeader(data_packet);

  // If we have a connection, the process the packet
  if (connection) {
    connection->ProcessPacket(data_packet);

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
 *      data_packet [in]
 *          The data packet received from the network.
 *
 *  Returns:
 *      Connection object associated with this data packet.
 *
 *  Comments:
 *      None.
 */
H3ServerConnectionPointer
QuicRServerH3Session::ProcessQUICHeader(cantina::DataPacket& data_packet)
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
                      data_packet.GetBufferPointer(),
                      data_packet.GetDataLength(),
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
                                        << dcid << " from "
                                        << data_packet.GetAddress());

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
  return HandleNewConnection(
    version, scid, dcid, token, data_packet.GetAddress());
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
 *      client [in]
 *          The address of the remote client.
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
                                          const cantina::NetworkAddress& client)
{
  H3ServerConnectionPointer connection;
  QUICConnectionID original_dcid;

  // Check the version and renegotiate if necessary
  if (!QuicheCall(quiche_version_is_supported, version)) {
    logger->info << "Received version " << version << " with SCID " << scid
                 << " and DCID " << dcid << " from " << client
                 << "; renegotiating" << std::flush;
    NegotiateVersion(scid, dcid, client);
    return connection;
  }

  // If there is no token, then request the client to retry (send a token)
  if (token.GetDataLength() == 0) {
    logger->info << "Requesting a retry; with SCID " << scid << " and DCID "
                 << dcid << " from " << client << std::flush;
    RequestRetry(version, scid, dcid, client);
    return connection;
  }

  // Validate that the token is correct
  if (!ValidateToken(token, client, original_dcid)) {
    logger->warning << "Token received from the client failed to validate"
                    << std::flush;
    return connection;
  }

  // Accept the connection
  quiche_conn* quiche_connection = QuicheCall(
    quiche_accept,
    dcid.GetDataBuffer(),
    dcid.GetDataLength(),
    original_dcid.GetDataBuffer(),
    original_dcid.GetDataLength(),
    reinterpret_cast<const sockaddr*>(local_address.GetAddressStorage()),
    local_address.GetAddressStorageSize(),
    reinterpret_cast<const sockaddr*>(client.GetAddressStorage()),
    client.GetAddressStorageSize(),
    server_config);

  if (quiche_connection == nullptr) {
    logger->error << "Failed to accept incoming connection" << std::flush;
    return connection;
  }

  logger->info << "Connection accepted from " << client << std::flush;

  try {
    connection = std::make_shared<H3ServerConnection>(
      logger,
      timer_manager,
      async_requests,
      network,
      pub_sub_registry,
      server_delegate,
      data_socket,
      Max_Send_Size,
      Max_Recv_Size,
      dcid,
      scid,
      local_address,
      quiche_connection,
      Heartbeat_Interval,
      [&, dcid]() { ConnectionClosed(dcid); });
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
    cleanup_signal.notify_one();
  }
}

/*
 *  QuicRServerH3Session::ConnectionCleanup()
 *
 *  Description:
 *      This function is called by a cleanup thread that takes responsibility
 *      to clean up connections after they are moved onto the closed
 *      connections list.
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
QuicRServerH3Session::ConnectionCleanup()
{
  std::vector<H3ServerConnectionPointer> connection_list;
  std::unique_lock<std::mutex> lock(server_lock);

  logger->info << "Starting connection cleanup thread" << std::flush;

  while (true) {
    // Wait for a connection closure or object termination
    cleanup_signal.wait(lock, [&]() {
      return (terminate == true) || (closed_connections.size() > 0);
    });

    // Stop if terminating
    if (terminate) break;

    // Swap the closed connections list so they can be removed
    std::swap(connection_list, closed_connections);

    // Unlock the mutex
    lock.unlock();

    // Empty the local list (destroying connections)
    connection_list.clear();

    // Re-lock the mutex
    lock.lock();
  }

  logger->info << "Cleanup thread exiting" << std::flush;
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
 *      client [in]
 *          The address of the remote client.
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
                                       const cantina::NetworkAddress& client)
{
  cantina::DataPacket version_packet(Max_Recv_Size);
  ssize_t size;

  // Negotiate the version by creating a version negotiation packet
  size = QuicheCall(quiche_negotiate_version,
                    scid.GetDataBuffer(),
                    scid.GetDataLength(),
                    dcid.GetDataBuffer(),
                    dcid.GetDataLength(),
                    version_packet.GetBufferPointer(),
                    version_packet.GetBufferSize());

  // If we have data to send, send it
  if (size > 0) {
    version_packet.SetDataLength(size);
    version_packet.SetAddress(client);
    if (network->SendData(data_socket, version_packet) ==
        cantina::Network::Socket_Error) {
      logger->error << "Failed to send version negotiation packet"
                    << std::flush;
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
 *      client [in]
 *          The address of the remote client.
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
                                   const cantina::NetworkAddress& client)
{
  cantina::DataPacket retry_packet(Max_Recv_Size);
  ssize_t size;

  // Create a token
  QUICToken token = NewToken(dcid, client);

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
                    retry_packet.GetBufferPointer(),
                    retry_packet.GetBufferSize());

  // If we have data to send, send it
  if (size > 0) {
    retry_packet.SetDataLength(size);
    retry_packet.SetAddress(client);
    if (network->SendData(data_socket, retry_packet) ==
        cantina::Network::Socket_Error) {
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
 *      client [in]
 *          The address of the remote client.
 *
 *  Returns:
 *      A newly created QUIC token.
 *
 *  Comments:
 *      The token has this form:
 *          [SERVER PREFIX] [REMOTE ADDRESS] [CONNECTION ID]
 */
QUICToken
QuicRServerH3Session::NewToken(const QUICConnectionID& dcid,
                               const cantina::NetworkAddress& client)
{
  // Create a token with the random token prefix
  QUICToken token(token_prefix);

  // Append the NetworkAddress
  token.Append(
    reinterpret_cast<const std::uint8_t*>(client.GetAddressStorage()),
    sizeof(sockaddr_storage));

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
 *      client [in]
 *          The address of the remote client.
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
                                    const cantina::NetworkAddress& client,
                                    QUICConnectionID& dcid)
{
  // Create a token without a connection ID
  QUICToken generated_token = NewToken(QUICConnectionID(0), client);

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
  return true;
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
QuicRServerH3Session::run()
{
  return false;
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
    logger->warning << "Could not find publisher record" << std::endl;
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
                    << std::endl;
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
                    << std::endl;
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
                    << std::endl;
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
                    << std::endl;
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
                    << std::endl;
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
 * @param datagram                 : QuicR Publish Datagram to send
 *
 */
void
QuicRServerH3Session::sendNamedObject(const uint64_t& subscriber_id,
                                      bool use_reliable_transport,
                                      const messages::PublishDatagram& datagram)
{
  // Locate the connection associated with this namespace
  auto subscriber = pub_sub_registry->FindRecord(subscriber_id);
  if (!subscriber.has_value()) {
    logger->warning << "sendNamedObject could not find subscriber record"
                    << std::endl;
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
    logger->warning << "sendNamedObject could not find connection" << std::endl;
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

} // namespace quicr
