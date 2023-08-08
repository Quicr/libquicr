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

#include "quiche.h"
#include "quicr/quicr_server_delegate.h"
#include "quicr/quicr_server_session.h"
#include "quicr_server_h3_connection.h"
#include "transport/transport.h"
#include <condition_variable>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <deque>
#include <cstdint>
#include <cstddef>
#include "cantina/async_requests.h"
#include "cantina/logger.h"
#include "cantina/timer_manager.h"
#include "cantina/network_types.h"
#include "cantina/network_utilities.h"
#include "pub_sub_registry.h"
#include "quic_identifier.h"
#include "transport_delegate.h"
#include "h3_common.h"

namespace quicr::h3 {

// Define a class for H3 server-related exceptions
class QuicRServerH3SessionException : public QuicRH3Exception
{
  using QuicRH3Exception::QuicRH3Exception;
};

// Define the QuicRServerH3Session object
class QuicRServerH3Session : public QuicRServerSession
{
protected:
  static constexpr std::size_t Connection_ID_Len{ 20 };
  static constexpr std::uint64_t Connection_Timeout{ 5'000 }; // ms
  static constexpr std::uint64_t Heartbeat_Interval{ Connection_Timeout / 3 };
  static constexpr std::uint64_t Max_Recv_Size{ 1500 };
  static constexpr std::size_t Max_Send_Size{ 1500 };
  static constexpr std::size_t Max_Stream_Reads_Per_Notification{ 300 };

public:
  QuicRServerH3Session(RelayInfo& relay_info,
                       qtransport::TransportConfig transport_config,
                       ServerDelegate& server_delegate,
                       qtransport::LogHandler& transport_logger);

  virtual ~QuicRServerH3Session();

  ////////////////////////////////////////////////////////////////////////////
  // Functions to satisfy the QuicRServer interface
  ////////////////////////////////////////////////////////////////////////////

  bool is_transport_ready() override;
  bool run() override;
  void publishIntentResponse(const quicr::Namespace& quicr_namespace,
                             const PublishIntentResult& result) override;
  void subscribeResponse(const uint64_t& subscriber_id,
                         const quicr::Namespace& quicr_namespace,
                         const SubscribeResult& result) override;
  void subscriptionEnded(
    const uint64_t& subscriber_id,
    const quicr::Namespace& quicr_namespace,
    const SubscribeResult::SubscribeStatus& reason) override;
  void sendNamedObject(
    const uint64_t& subscriber_id,
    bool use_reliable_transport,
    uint8_t priority,
    uint16_t expiry_age_ms,
    const messages::PublishDatagram& datagram) override;

  ////////////////////////////////////////////////////////////////////////////
  // End of Functions to satisfy the QuicRServer interface
  ////////////////////////////////////////////////////////////////////////////

protected:
  void ConfigureQuiche();
  static void quiche_log(const char* line, void* argp)
  {
    auto h3_server = reinterpret_cast<QuicRServerH3Session*>(argp);
    h3_server->logger->info << line << std::flush;
  }
  friend class TransportDelegate<QuicRServerH3Session>;
  void IncomingPacketNotification(
    const qtransport::TransportContextId& context_id,
    const qtransport::StreamId& stream_id);
  void ProcessPackets(std::unique_lock<std::mutex> &lock);
  void PacketHandler(const StreamContext& stream_context, quicr::bytes& packet);
  H3ServerConnectionPointer ProcessQUICHeader(
    const StreamContext& stream_context,
    quicr::bytes& packet);
  H3ServerConnectionPointer HandleNewConnection(
    std::uint32_t version,
    const QUICConnectionID& scid,
    const QUICConnectionID& dcid,
    const QUICToken& token,
    const StreamContext& stream_context);
  void ConnectionClosed(const QUICConnectionID& connection_id);
  void CloseNetworkStream(const StreamContext& stream_context);
  void WorkerIdleLoop();
  void ConnectionCleanup(std::unique_lock<std::mutex> &lock);
  void NegotiateVersion(const QUICConnectionID& scid,
                        const QUICConnectionID& dcid,
                        const StreamContext& stream_context);
  void RequestRetry(std::uint32_t version,
                    const QUICConnectionID& scid,
                    const QUICConnectionID& dcid,
                    const StreamContext& stream_context);
  QUICToken NewToken(const QUICConnectionID& dcid,
                     const StreamContext& stream_context);
  bool ValidateToken(const QUICToken& token,
                     const StreamContext& stream_context,
                     QUICConnectionID& dcid);
  QUICConnectionID CreateConnectionID();
  std::uint8_t GetRandomOctet();

  bool terminate;                               // Termination flag
  cantina::LoggerPointer logger;                // Logger object
  cantina::TimerManagerPointer timer_manager;   // Timer manager
  cantina::AsyncRequestsPointer async_requests; // Asynchronous requests
  TransportPointer transport;                   // Transport object
  qtransport::TransportContextId transport_context;
                                                // Transport connection ID
  TransportDelegate<QuicRServerH3Session> transport_delegate;
                                                // Transport delegate
  ServerDelegate& server_delegate;              // Server delegate
  std::string certificate;                      // Certificate file
  std::string certificate_key;                  // Certificate Key file
  bool use_datagrams;                           // Send media via QUIC datagrams
  quiche_config* server_config;                 // Quiche config context
  socket_t data_socket;                         // Socket for communication
  cantina::NetworkAddress local_address;        // Local server address
  std::mt19937 generator;                       // PRNG
  PubSubRegistryPointer pub_sub_registry;       // Pub/Sub Registry
  std::vector<std::uint8_t> token_prefix;       // Prefix to server token
  std::map<QUICConnectionID, H3ServerConnectionPointer> connections;
                                                // Map of client connections
  std::deque<StreamContext> packet_queue;       // Incoming packet queue
  std::vector<H3ServerConnectionPointer> closed_connections;
                                                // List of closed connections
  std::mutex server_lock;                       // Thread syncronization
  std::thread worker_thread;                    // Handles packets and cleanup
  std::condition_variable cv;                   // Worker thread CV
};

} // namespace quicr::h3
