/*
 *  quicr_client_h3_session.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      This file defines a session layer between the client APIs and the
 *      transport that uses HTTP/3 to communicate with a server.
 *
 *  Portability Issues:
 *      None.
 *
 */

#pragma once

#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include "cantina/async_requests.h"
#include "cantina/logger.h"
#include "cantina/network.h"
#include "cantina/timer_manager.h"
#include "pub_sub_registry.h"
#include "quic_identifier.h"
#include "quiche.h"
#include "quicr/quicr_client_delegate.h"
#include "quicr/quicr_client_session.h"
#include "quicr_client_h3_connection.h"
#include "transport/transport.h"

namespace quicr {

// Define an exception class for H3 client-related errors
class QuicRClientH3SessionException : public std::runtime_error
{
  using std::runtime_error::runtime_error;
};

// Define the QuicRClientH3Session object
class QuicRClientH3Session : public QuicRClientSession
{
protected:
  static constexpr std::size_t Connection_ID_Len{ 20 };
  static constexpr std::uint64_t Connection_Timeout{ 5'000 }; // ms
  static constexpr std::uint64_t Heartbeat_Interval{ Connection_Timeout / 3 };
  static constexpr std::uint64_t Max_Recv_Size{1500};
  static constexpr std::size_t Max_Send_Size{
    std::min(std::size_t(1350), std::size_t(MAX_TRANSPORT_DATA_SIZE))
  };

public:
  QuicRClientH3Session(RelayInfo& relay_info,
                       qtransport::TransportConfig transport_config,
                       qtransport::LogHandler& transport_logger);

  virtual ~QuicRClientH3Session();

  ////////////////////////////////////////////////////////////////////////////
  // Functions to satisfy the QuicRClient interface
  ////////////////////////////////////////////////////////////////////////////

  virtual ClientStatus status() override;
  virtual bool publishIntent(std::shared_ptr<PublisherDelegate> pub_delegate,
                             const quicr::Namespace& quicr_namespace,
                             const std::string& origin_url,
                             const std::string& auth_token,
                             bytes&& payload) override;
  virtual void publishIntentEnd(const quicr::Namespace& quicr_namespace,
                                const std::string& auth_token) override;
  virtual void subscribe(
    std::shared_ptr<SubscriberDelegate> subscriber_delegate,
    const quicr::Namespace& quicr_namespace,
    const SubscribeIntent& intent,
    const std::string& origin_url,
    bool use_reliable_transport,
    const std::string& auth_token,
    bytes&& e2e_token) override;
  virtual void unsubscribe(const quicr::Namespace& quicr_namespace,
                           const std::string& origin_url,
                           const std::string& auth_token) override;
  virtual void publishNamedObject(const quicr::Name& quicr_name,
                                  uint8_t priority,
                                  uint16_t expiry_age_ms,
                                  bool use_reliable_transport,
                                  bytes&& data) override;
  virtual void publishNamedObjectFragment(const quicr::Name& quicr_name,
                                          uint8_t priority,
                                          uint16_t expiry_age_ms,
                                          bool use_reliable_transport,
                                          const uint64_t& offset,
                                          bool is_last_fragment,
                                          bytes&& data) override;

  ////////////////////////////////////////////////////////////////////////////
  // End of functions to satisfy the QuicRClient interface
  ////////////////////////////////////////////////////////////////////////////

protected:
  // Holds connection-specific data
  struct ConnectionData
  {
    socket_t socket;
    cantina::NetworkAddress local_address;
    cantina::NetworkAddress remote_address;
    QUICConnectionID id;
    H3ClientConnectionPointer connection;
  };

  typedef std::map<cantina::RegistrationID, ConnectionData> ConnectionMap;

  void ConfigureQuiche();
  std::pair<bool, cantina::RegistrationID> CreateNewConnection(
    const std::string& hostname,
    const cantina::NetworkAddress& remote_address);
  static void quiche_log(const char* line, void* argp)
  {
    auto h3_client = reinterpret_cast<QuicRClientH3Session*>(argp);
    h3_client->logger->info << line << std::flush;
  }
  void PacketHandler(const cantina::RegistrationID& registration_id,
                     cantina::DataPacket& data_packet);
  void ConnectionClosed(const cantina::RegistrationID registration_id);
  void ConnectionCleanup();
  void NetworkConnectionCleanup(cantina::RegistrationID registration_id,
                                socket_t socket);
  QUICConnectionID CreateConnectionID();
  std::uint8_t GetRandomOctet();
  cantina::NetworkAddress FindIPv4Address(const std::string hostname,
                                          const std::string& service);

  // TODO: H3 session was written to allow a plurality of connections, but this
  // function exists in this prototype with the assumption there is only one
  H3ClientConnectionPointer GetConnection()
  {
    std::lock_guard<std::mutex> lock(client_lock);
    if (connections.size() != 1) return {};
    return connections.begin()->second.connection;
  }

  bool terminate;                               // Termination flag
  cantina::LoggerPointer logger;                // Logger object
  cantina::TimerManagerPointer timer_manager;   // Timer manager
  cantina::AsyncRequestsPointer async_requests; // Asynchronous requests
  cantina::NetworkPointer network;              // Network object
  cantina::NetworkAddress local_address;        // Local client address
  std::string certificate;                      // Certificate file
  std::string certificate_key;                  // Certificate Key file
  bool verify_certificate;                      // Verify remote certificate?
  bool use_datagrams;                           // Send media via QUIC datagrams
  quiche_config* client_config;                 // Quiche config context
  std::mt19937 generator;                       // PRNG
  PubSubRegistryPointer pub_sub_registry;       // Pub/Sub Registry
  ConnectionMap connections;                    // Current connection map
  ConnectionMap closed_connections;             // List of closed connections
  std::mutex client_lock;                       // Thread syncronization
  std::thread cleanup_thread;                   // Connection cleanup thread
  std::condition_variable cleanup_signal;       // Controls connection cleanup
};

} // quicr namespace
