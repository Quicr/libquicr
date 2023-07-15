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

#pragma once

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include "cantina/async_requests.h"
#include "cantina/data_packet.h"
#include "cantina/logger.h"
#include "cantina/network.h"
#include "cantina/network_types.h"
#include "cantina/octet_string.h"
#include "cantina/registration_id.h"
#include "cantina/timer_manager.h"
#include "pub_sub_registry.h"
#include "quic_identifier.h"
#include "quiche.h"
#include "quiche_types.h"
#include "quicr/quicr_common.h"

namespace quicr {

// Define function for reporting connection closure
typedef std::function<void()> ClosureCallback;

// Define an exception class for QUIC Connection-related exceptions
class H3ConnectionException : public std::runtime_error
{
  using std::runtime_error::runtime_error;
};

// Define function for reporting connection closure
typedef std::function<void()> ClosureCallback;

// Client state
enum class H3ConnectionState
{
  ConnectPending,
  Connected,
  Disconnected
};

// QUICR H3ConnectionBase object
class H3ConnectionBase
{
protected:
  // Request state
  enum class H3RequestState
  {
    Initiated,
    Active,
    Complete
  };

  // Define type for HTTP headers
  typedef std::map<std::string, std::string> HTTPHeaders;

  // Define a structure to hold request information
  struct RequestData
  {
    H3RequestState state;
    std::string method;
    std::string path;
    unsigned status_code;
    HTTPHeaders request_headers;
    std::vector<std::uint8_t> request_body;
  };

  struct MessageQueue
  {
    // Octets sent + actual vector of octets to send
    std::deque<std::pair<std::size_t, std::vector<std::uint8_t>>> messages;

    // Set the final flag when last buffer is sent?
    bool final;
  };

public:
  H3ConnectionBase(const cantina::LoggerPointer& parent_logger,
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
                   const ClosureCallback closure_callback);

  virtual ~H3ConnectionBase();

  H3ConnectionState GetConnectionState();

  bool IsConnectionClosed();
  bool IsConnectionEstablished();
  QUICConnectionID GetConnectionID();

  void ProcessPacket(cantina::DataPacket& data_packet);

protected:
  virtual void HandleIncrementalRequestData(QUICStreamID stream_id,
                                            RequestData* request) = 0;
  virtual bool HandleCompletedRequest(QUICStreamID stream_id,
                                      RequestData* request) = 0;
  virtual void HandleReceivedDatagram(QUICStreamID stream_id,
                                      std::vector<std::uint8_t> &datagram) = 0;

  void ConsumePacket(cantina::DataPacket& data_packet);
  bool QuicheConsumeData(cantina::DataPacket& data_packet);
  void DispatchMessages(std::unique_lock<std::mutex>& lock,
                        cantina::TimerID timer_id = {});
  void SendMessageBody(QUICStreamID stream_id,
                       std::vector<std::uint8_t>& message,
                       bool final);
  void SendQueuedMessages();
  void QuicheEmitQUICMessages(std::unique_lock<std::mutex>& lock);
  void NotifyOnClosure(bool force_close = false);
  void CreateOrRefreshTimer(std::unique_lock<std::mutex>& lock,
                            cantina::TimerID timer_id = {});
  void TimeoutHandler(cantina::TimerID timer_id);
  bool QuicheHTTP3EventHandler(std::unique_lock<std::mutex>& lock);
  void HandleH3HeadersEvent(QUICStreamID stream_id, quiche_h3_event* event);
  void HandleH3DataEvent(QUICStreamID stream_id, quiche_h3_event* event);
  void HandleH3FinishedEvent(QUICStreamID stream_id, quiche_h3_event* event);
  void HandleH3ResetEvent(QUICStreamID stream_id, quiche_h3_event* event);
  void HandleH3PriorityUpdateEvent(QUICStreamID stream_id,
                                   quiche_h3_event* event);
  void HandleH3DatagramEvent(QUICStreamID stream_id, quiche_h3_event* event);
  void HandleH3GoAwayEvent(QUICStreamID stream_id, quiche_h3_event* event);

  void ProcessHeader(QUICStreamID stream_id,
                     std::string& name,
                     std::string& value);
  void ProcessSetting(std::uint64_t identifier, std::uint64_t value);
  RequestData* FindRequest(QUICStreamID stream_id, bool create = false);
  void ExpungeRequest(QUICStreamID stream_id);
  static int SettingsCallback(std::uint64_t identifier,
                              std::uint64_t value,
                              void* argp)
  {
    H3ConnectionBase* connection;
    connection = reinterpret_cast<H3ConnectionBase*>(argp);
    if (connection) connection->ProcessSetting(identifier, value);
    return 0;
  }
  static int HeaderCallback(std::uint8_t* name,
                            std::size_t name_len,
                            std::uint8_t* value,
                            std::size_t value_len,
                            void* argp)
  {
    std::pair<H3ConnectionBase*, QUICStreamID>* stream_data;
    stream_data =
      reinterpret_cast<std::pair<H3ConnectionBase*, QUICStreamID>*>(argp);

    if (stream_data->first) {
      std::string name_string;
      std::string value_string;

      name_string.assign(reinterpret_cast<char*>(name), name_len);
      value_string.assign(reinterpret_cast<char*>(value), value_len);
      stream_data->first->ProcessHeader(
        stream_data->second, name_string, value_string);
    }

    return 0;
  }

  bool terminate;                               // Connection terminating flag
  cantina::LoggerPointer logger;                // Logger object
  cantina::TimerManagerPointer timer_manager;   // Timer manager
  cantina::AsyncRequestsPointer async_requests; // Async requests object
  cantina::NetworkPointer network;              // Network object
  PubSubRegistryPointer pub_sub_registry;       // Pub/Sub Registry
  const socket_t data_socket;                   // Socket for communication
  std::size_t max_send_size;                    // Max sending packet size
  std::size_t max_recv_size;                    // Max receiving packet size
  bool use_datagrams;                           // Use datagrams if supported?
  bool using_datagrams;                         // Connection using datagrams?
  const QUICConnectionID local_cid;             // Local connection ID
  cantina::NetworkAddress local_address;        // Local address
  quiche_conn* quiche_connection;               // Quiche Connection
  H3ConnectionState connection_state;           // Current connection state
  ClosureCallback closure_callback;             // Used to notify of closure
  bool closure_notification;                    // Sent closure notification?
  quiche_h3_config* http3_config;               // HTTP/3 configuration
  quiche_h3_conn* http3_connection;             // HTTP/3 connection
  bool settings_received;                       // Settings received
  bool timer_create_pending;                    // Is timer creation pending?
  cantina::TimerID heartbeat_timer;             // Heartbeat (keepalive) timer
  std::deque<cantina::DataPacket> incoming_packets;
                                                // Incoming packets to process
  std::deque<cantina::TimerID> timeout_timers;  // Timeout timer IDs
  std::map<QUICStreamID, RequestData> requests; // Request information
  std::map<std::uint64_t, std::uint64_t> connection_settings;
                                                // Connections settings
  std::map<QUICStreamID, MessageQueue> queued_messages;
  std::mutex connection_lock;                   // Connection syncronization
};

} // namespace quicr
