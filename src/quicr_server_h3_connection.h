/*
 *  h3_server_connection.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      This file defines an H3ServerConnection object that is used for
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
#include "quicr/quicr_server_delegate.h"

namespace quicr {

// Define function for reporting connection closure
typedef std::function<void()> ClosureCallback;

// Define an exception class for QUIC Connection-related exceptions
class H3ServerConnectionException : public std::runtime_error
{
  using std::runtime_error::runtime_error;
};

// Define function for reporting connection closure
typedef std::function<void()> ClosureCallback;

// QUICR H3 Server Connection object
class H3ServerConnection
{
protected:
  struct SubscriptionRecord
  {
    cantina::RegistrationID registration_id;
    std::string space_name;
  };

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
    HTTPHeaders request_headers;
    std::vector<std::uint8_t> request_body;
  };

public:
  H3ServerConnection(const cantina::LoggerPointer& parent_logger,
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
                     const ClosureCallback& closure_callback);
  ~H3ServerConnection();

  void ProcessPacket(cantina::DataPacket& data_packet);
  bool IsConnectionClosed();
  bool IsConnectionEstablished();
  QUICConnectionID GetConnectionID();

  // Called by the session object to deliver calls from application
  void PublishIntentResponse(const PubSubRecord& publisher,
                             const PublishIntentResult& result);
  void SubscribeResponse(const PubSubRecord& subscriber,
                         const SubscribeResult& result);
  void SubscriptionEnded(const PubSubRecord& subscriber,
                         const Namespace& quicr_namespace,
                         const SubscribeResult::SubscribeStatus& reason);
  void SendNamedObject(const PubSubRecord& subscriber,
                       bool use_reliable_transport,
                       const messages::PublishDatagram& datagram);

protected:
  void PublishEndNotify(const PubSubRecord& publisher);
  void UnsubscribeNotify(const PubSubRecord& subscriber);

  void ConsumePacket(cantina::DataPacket& data_packet);
  bool QuicheConsumeData(cantina::DataPacket& data_packet);
  void DispatchMessages(std::unique_lock<std::mutex>& lock,
                        cantina::TimerID timer_id = {});
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

  void SendHTTPResponse(QUICStreamID stream_id,
                        unsigned status_code,
                        const HTTPHeaders& response_headers,
                        const std::vector<std::uint8_t>& response_body,
                        bool prefix_length,
                        bool close_stream = true);
  std::pair<bool, unsigned> ProcessRequest(QUICStreamID stream_id);
  bool HandlePublishIntent(QUICStreamID stream_id, RequestData* request);
  unsigned HandlePublishIntentEnd(RequestData* request);
  unsigned HandlePublishNamedObject(QUICStreamID stream_id,
                                    RequestData* request);
  bool HandleSubscribe(QUICStreamID stream_id, RequestData* request);
  bool HandleUnsubscribe(RequestData* request);

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
    H3ServerConnection* connection;
    connection = reinterpret_cast<H3ServerConnection*>(argp);
    if (connection) connection->ProcessSetting(identifier, value);
    return 0;
  }
  static int HeaderCallback(std::uint8_t* name,
                            std::size_t name_len,
                            std::uint8_t* value,
                            std::size_t value_len,
                            void* argp)
  {
    std::pair<H3ServerConnection*, QUICStreamID>* stream_data;
    stream_data =
      reinterpret_cast<std::pair<H3ServerConnection*, QUICStreamID>*>(argp);

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
  ServerDelegate& server_delegate;              // Server delegate
  const socket_t data_socket;                   // Socket for communication
  std::size_t max_packet_size;                  // Max size of data packets
  bool using_datagrams;                         // Connection using datagrams?
  const QUICConnectionID local_cid;             // Server's connection ID
  const QUICConnectionID remote_cid;            // Client connection ID
  cantina::NetworkAddress local_address;        // Local server address
  quiche_conn* quiche_connection;               // Quiche Connection
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
  std::mutex connection_lock; // Connection syncronization
};

// Define a shared pointer type for the H3ServerConnection
typedef std::shared_ptr<H3ServerConnection> H3ServerConnectionPointer;

} // namespace quicr
