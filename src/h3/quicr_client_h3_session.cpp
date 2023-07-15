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
#include "quiche_types.h"

namespace quicr {

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
  , network{ nullptr }
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

  // Create a thread pool for use by the Network and TimerManager
  auto thread_pool = std::make_shared<cantina::ThreadPool>(logger, 5, 10);

  // Create the TimerManager object
  timer_manager = std::make_shared<cantina::TimerManager>(logger, thread_pool);

  // Create an AsyncRequests object to facilitate asynchronous callbacks
  async_requests = std::make_shared<cantina::AsyncRequests>(thread_pool);

  // Create a MemoryManager for use by Network
  auto memory_manager = std::make_shared<cantina::MemoryManager>(
    cantina::MemoryPoolConfig{
      { 256, 0 },
      { Max_Recv_Size, 5 },
      { 65536, 0 },
    },
    logger,
    false,
    true,
    true);
  MemoryAllocatorInitialize(memory_manager);

  // Create the network object to manage network traffic
  network = std::make_shared<cantina::Network>(logger,
                                               thread_pool,
                                               memory_manager,
                                               nullptr,
                                               Max_Recv_Size,
                                               2,
                                               5,
                                               2,
                                               local_address);

  // Configure Quiche
  ConfigureQuiche();

  // Create the thread to perform connection cleanup
  cleanup_thread = std::thread([&]() { ConnectionCleanup(); });

  // Resolve the remote address
  auto remote_address =
    FindIPv4Address(relay_info.hostname, std::to_string(relay_info.port));

  // If unable to resolve the address, throw an exception
  if (remote_address == cantina::NetworkAddress()) {
    throw QuicRClientH3SessionException("Unable to resolve remote address");
  }

  // Initiate remote connection
  if (!CreateNewConnection(relay_info.hostname, remote_address).first) {
    throw QuicRClientH3SessionException("Unable to initiate remote connection");
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

  // Lock the client mutex
  std::unique_lock<std::mutex> lock(client_lock);

  // Set the terminate flag to true
  terminate = true;

  // Unlock the mutex so threads can exit
  lock.unlock();

  // Stop the thread closing connections
  cleanup_signal.notify_one();
  cleanup_thread.join();

  // Unregister from the network (ensuring no threads running here)
  for (auto& cx : connections) network->UnregisterCallback(cx.first);
  for (auto& cx : closed_connections) network->UnregisterCallback(cx.first);

  // Close the network data socket
  for (auto& cx : connections) network->CloseSocket(cx.second.socket);
  for (auto& cx : closed_connections) network->CloseSocket(cx.second.socket);

  // At this point, there should be no other threads running in this object

  // Destroy all connection objects
  closed_connections.clear();
  connections.clear();

  // Release the configuration
  quiche_config_free(client_config);

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
 *      a registration identifier associated with this connection.
 *      Success does not indicate establishment, but merely that the request is
 *      now being served.  If false is returned, the registration identifier
 *      has no valid value.
 *
 *  Comments:
 *      TODO: Open the socket in the QuicRClientH3SessionConnection object and
 *      allow the Network object to call directly into it instead of this
 *      object.  That would be more efficient.
 */
std::pair<bool, cantina::RegistrationID>
QuicRClientH3Session::CreateNewConnection(
  const std::string& hostname,
  const cantina::NetworkAddress& remote_address)
{
  ConnectionData connection_data{};

  // Open a socket for this connection
  connection_data.socket = network->OpenUDPSocket(local_address, 0);
  if (connection_data.socket == cantina::Network::Socket_Error) {
    logger->error << "Failed to open UDP port on " << local_address
                  << std::flush;
    return { false, {} };
  }

  // Set the remote address
  connection_data.remote_address = remote_address;

  // Set the connection address based on the successfully opened port
  connection_data.local_address =
    network->GetSocketAddress(connection_data.socket);

  // Create the QUIC connection ID for this connection
  connection_data.id = CreateConnectionID();
  logger->info << "Creating new connection having ID = " << connection_data.id
               << std::flush;

  // Lock the client mutex
  std::unique_lock<std::mutex> lock(client_lock);

  // Register to receive datagrams
  cantina::RegistrationID network_registration = network->RegisterCallback(
    connection_data.socket,
    [&](const cantina::RegistrationID& registration_id,
        cantina::DataPacket& data_packet) {
      PacketHandler(registration_id, data_packet);
    });

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

    // Unregister from Network and close the socket
    NetworkConnectionCleanup(network_registration, connection_data.socket);

    logger->error << "Unable to create Quiche connection" << std::flush;

    return { false, {} };
  }

  try {
    connection_data.connection = std::make_shared<H3ClientConnection>(
      logger,
      timer_manager,
      async_requests,
      network,
      pub_sub_registry,
      connection_data.socket,
      Max_Send_Size,
      Max_Recv_Size,
      use_datagrams,
      connection_data.id,
      connection_data.local_address,
      quiche_connection,
      Heartbeat_Interval,
      [&, network_registration]() { ConnectionClosed(network_registration); },
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

    // Unregister from Network and close the socket
    NetworkConnectionCleanup(network_registration, connection_data.socket);

    return { false, {} };
  }

  // Store this connection in the map
  connections[network_registration] = connection_data;

  return { true, network_registration };
}

/*
 *  QuicRClientH3Session::PacketHandler()
 *
 *  Description:
 *      This function handles packets received by the network.
 *
 *  Parameters:
 *      registration_id [in]
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
QuicRClientH3Session::PacketHandler(
  const cantina::RegistrationID& registration_id,
  cantina::DataPacket& data_packet)
{
  LOGGER_DEBUG(logger,
               "Received datagram of size " << data_packet.GetDataLength()
                                            << " octets from "
                                            << data_packet.GetAddress());

  // Lock the client mutex
  std::unique_lock<std::mutex> lock(client_lock);

  // Exit if terminating
  if (terminate) return;

  // Locate the connection given the registration ID, but just return if
  // it is not found (likely because the connection is closing)
  auto it = connections.find(registration_id);
  if (it == connections.end()) return;

  // Get a pointer to the connection
  auto connection = (*it).second.connection;

  // Unlock the mutex
  lock.unlock();

  // If we have a connection, the process the packet
  if (connection) {
    connection->ProcessPacket(data_packet);

    // If the QUIC connection closed, remove this connection
    if (connection->IsConnectionClosed()) ConnectionClosed(registration_id);
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
 *      registration_id [in]
 *          The network registration ID associated with this connection.
 *
 *  Returns:
 *      Nothing
 *
 *  Comments:
 *      None.
 */
void
QuicRClientH3Session::ConnectionClosed(
  const cantina::RegistrationID registration_id)
{
  // Lock the client mutex
  std::lock_guard<std::mutex> lock(client_lock);

  // Just return if terminating
  if (terminate) return;

  // Locate the connection given the registration ID
  auto item = connections.find(registration_id);

  // If found, we move that to a separate vector
  if (item != connections.end()) {
    closed_connections[registration_id] = item->second;
    connections.erase(item);
    cleanup_signal.notify_one();
  }
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
 *      timer_id [in]
 *          The timer ID associated with this callback.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
void
QuicRClientH3Session::ConnectionCleanup()
{
  std::map<cantina::RegistrationID, ConnectionData> connection_list;
  std::unique_lock<std::mutex> lock(client_lock);

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

    // Ensure registrations are cancelled and sockets are closed
    for (auto& item : connection_list) {
      NetworkConnectionCleanup(item.first, item.second.socket);
    }

    // Empty the local list (destroying connections)
    connection_list.clear();

    // Re-lock the mutex
    lock.lock();
  }

  logger->info << "Cleanup thread exiting" << std::flush;
}

/*
 *  QuicRClientH3Session::NetworkConnectionCleanup()
 *
 *  Description:
 *      For a given registration ID and socket, this function will
 *      unregister from the Network object and close the socket.
 *
 *  Parameters:
 *      registration_id [in]
 *          Network registration ID associated with this connection.
 *
 *      socket [in]
 *          Socket used for communicating by this connection.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      The client mutex should be locked when calling this function.
 */
void
QuicRClientH3Session::NetworkConnectionCleanup(
  cantina::RegistrationID registration_id,
  socket_t socket)
{
  // Unregister so as to no receive more callbacks
  network->UnregisterCallback(registration_id);

  // Close the connection socket
  network->CloseSocket(socket);
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

/*
 *  FindIPv4Address
 *
 *  Description:
 *      This function will accept a host and service/port pair and returns
 *      the first IPv4 address in the set of addresses returned by
 *      the Network object's GetHostAddresses() routine.
 *
 *  Parameters:
 *      hostname [in]
 *          A string containing either a host name or IP address.
 *
 *      service [in]
 *          A string containing the service name (e.g., "http") or port number.
 *
 *  Returns:
 *      A NetworkAddress object containing an IPv4 address or one of type
 *      NetworkAddressType::None if an address could not be found.
 *
 *  Comments:
 *      None.
 */
cantina::NetworkAddress
QuicRClientH3Session::FindIPv4Address(const std::string hostname,
                                      const std::string& service)
{
  // Determine the remote server address
  std::set<cantina::NetworkAddress> addresses =
    network->GetHostAddresses(hostname, service);

  // Look for only IPv4 addresses, if any
  for (const auto& address : addresses) {
    if (address.GetAddressType() == cantina::NetworkAddressType::IPv4) {
      return address;
    }
  }

  return {};
}

////////////////////////////////////////////////////////////////////////////
// Functions to satisfy the QuicRClient interface
////////////////////////////////////////////////////////////////////////////

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
  bytes&& payload)
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
  bytes&& e2e_token)
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
                                         bytes&& data)
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
  [[maybe_unused]] bytes&& data)
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

} // namespace quicr
