/*
 *  quicr_server_h3_session.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 *
 *  Description:
 *      This file defines a session layer between the server APIs and the
 *      transport that uses HTTP/3 to communicate with clients.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include <condition_variable>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include "cantina/logger.h"
#include "cantina/network.h"
#include "cantina/timer_manager.h"
#include "cantina/async_requests.h"
#include "transport/transport.h"
#include "quicr/quicr_server_delegate.h"
#include "quicr/quicr_server_session.h"
#include "quicr_server_h3_connection.h"
#include "quic_identifier.h"
#include "quiche.h"
#include "pub_sub_registry.h"

/*
 * QUICR Server H3 Session Interface
 */
namespace quicr {

// Define an exception class for QuicRServerH3SessionException-related exceptions
class QuicRServerH3SessionException : public std::runtime_error
{
  using std::runtime_error::runtime_error;
};

// Define the QuicRServerH3Session object
class QuicRServerH3Session : public QuicRServerSession
{
protected:
  static constexpr std::size_t Connection_ID_Len{ 20 };
  static constexpr std::uint64_t Connection_Timeout{ 5'000 }; // ms
  static constexpr std::uint64_t Heartbeat_Interval{ Connection_Timeout / 3 };
  static constexpr std::size_t Max_Packet_Size{ 1350 };

public:
  QuicRServerH3Session(RelayInfo& relay_info,
                       qtransport::TransportConfig transport_config,
                       ServerDelegate& server_delegate,
                       qtransport::LogHandler& transport_logger);

  virtual ~QuicRServerH3Session();

  ////////////////////////////////////////////////////////////////////////////
  // Functions to satisfy the QuicRServer interface
  ////////////////////////////////////////////////////////////////////////////

  // Transport API
  bool is_transport_ready() override;

  /**
   * @brief Run Server API event loop
   *
   * @details This method will open listening sockets and run an event loop
   *    for callbacks.
   *
   * @returns true if error, false if no error
   */
  bool run() override;

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
  void publishIntentResponse(const quicr::Namespace& quicr_namespace,
                             const PublishIntentResult& result) override;

  /**
   * @brief Send subscribe response
   *
   * @details Entities processing the Subscribe Request MUST validate the
   * request
   *
   * @param subscriber_id         : Subscriber ID to send the message to
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param result                : Status of Subscribe operation
   *
   */
  void subscribeResponse(const uint64_t& subscriber_id,
                         const quicr::Namespace& quicr_namespace,
                         const SubscribeResult& result) override;

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
  void subscriptionEnded(
    const uint64_t& subscriber_id,
    const quicr::Namespace& quicr_namespace,
    const SubscribeResult::SubscribeStatus& reason) override;

  /**
   * @brief Send a named QUICR media object
   *
   * @param subscriber_id            : Subscriber ID to send the message to
   * @param use_reliable_transport   : Indicates the preference for the object's
   *                                   transport, if forwarded.
   * @param datagram                 : QuicR Publish Datagram to send
   *
   */
  void sendNamedObject(const uint64_t& subscriber_id,
                       bool use_reliable_transport,
                       const messages::PublishDatagram& datagram) override;

  ////////////////////////////////////////////////////////////////////////////
  // End of Interface Functions
  ////////////////////////////////////////////////////////////////////////////

protected:
  void ConfigureQuiche();
  static void quiche_log(const char* line, void* argp)
  {
    auto h3_server = reinterpret_cast<QuicRServerH3Session*>(argp);
    h3_server->logger->info << line << std::flush;
  }
  void PacketHandler(const cantina::RegistrationID&,
                     cantina::DataPacket& data_packet);
  H3ServerConnectionPointer ProcessQUICHeader(cantina::DataPacket& data_packet);
  H3ServerConnectionPointer HandleNewConnection(
    std::uint32_t version,
    const QUICConnectionID& scid,
    const QUICConnectionID& dcid,
    const QUICToken& token,
    const cantina::NetworkAddress& client);
  void ConnectionClosed(const QUICConnectionID& connection_id);
  void ConnectionCleanup();
  void NegotiateVersion(const QUICConnectionID& scid,
                        const QUICConnectionID& dcid,
                        const cantina::NetworkAddress& client);
  void RequestRetry(std::uint32_t version,
                    const QUICConnectionID& scid,
                    const QUICConnectionID& dcid,
                    const cantina::NetworkAddress& client);
  QUICToken NewToken(const QUICConnectionID& dcid,
                     const cantina::NetworkAddress& client);
  bool ValidateToken(const QUICToken& token,
                     const cantina::NetworkAddress& client,
                     QUICConnectionID& dcid);
  QUICConnectionID CreateConnectionID();
  std::uint8_t GetRandomOctet();

  bool terminate;                               // Termination flag
  cantina::LoggerPointer logger;                // Logger object
  cantina::TimerManagerPointer timer_manager;   // Timer manager
  cantina::AsyncRequestsPointer async_requests; // Asynchronous requests
  cantina::NetworkPointer network;              // Network object
  ServerDelegate& server_delegate;              // Server delegate
  std::string certificate;                      // Certificate file
  std::string certificate_key;                  // Certificate Key file
  quiche_config* server_config;                 // Quiche config context
  socket_t data_socket;                         // Socket for communication
  cantina::NetworkAddress local_address;        // Local server address
  std::mt19937 generator;                       // PRNG
  cantina::RegistrationID network_registration; // Registration with Network
  PubSubRegistryPointer pub_sub_registry;       // Pub/Sub Registry
  std::vector<std::uint8_t> token_prefix;       // Prefix to server token
  std::map<QUICConnectionID, H3ServerConnectionPointer> connections;
                                                // Map of client connections
  std::vector<H3ServerConnectionPointer> closed_connections;
                                                // List of closed connections
  std::mutex server_lock;                       // Thread syncronization
  std::thread cleanup_thread;                   // Connection cleanup thread
  std::condition_variable cleanup_signal;       // Controls connection cleanup
};

} // namespace quicr
