/*
 *  quicr_client_h3_connection.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      This file defines a H3ClientConnection connection object that is used
 *      for communication over a single QUIC connection.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include "fragment_assembler.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>
#include "cantina/async_requests.h"
#include "cantina/data_packet.h"
#include "cantina/logger.h"
#include "cantina/network.h"
#include "cantina/network_types.h"
#include "cantina/registration_id.h"
#include "cantina/timer_manager.h"
#include "pub_sub_registry.h"
#include "quic_identifier.h"
#include "quiche.h"
#include "quiche_types.h"
#include "quicr/quicr_client_delegate.h"
#include "quicr/quicr_common.h"
#include "quicr/name.h"
#include "quicr/namespace.h"

namespace quicr {

// Define an exception class for H3ClientConnection exceptions
class H3ClientConnectionException : public std::runtime_error
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

// H3ClientConnection connection object
class H3ClientConnection
{
protected:
  // Request state
  enum class H3RequestState
  {
    Initiated,
    Processing,
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
    HTTPHeaders response_headers;
    std::vector<std::uint8_t> response_body;
  };

public:
  H3ClientConnection(const cantina::LoggerPointer& parent_logger,
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
                     const std::string& hostname,
                     quiche_conn* quiche_connection,
                     std::uint64_t heartbeat_interval,
                     const ClosureCallback closure_callback);
  ~H3ClientConnection();
  void ProcessPacket(cantina::DataPacket& data_packet);
  bool IsConnectionClosed();
  bool IsConnectionEstablished();
  H3ConnectionState GetConnectionState();
  QUICConnectionID GetConnectionID();

  ////////////////////////////////////////////////////////////////////////////
  // Functions to satisfy the QuicRClient interface
  ////////////////////////////////////////////////////////////////////////////

  bool PublishIntent(std::shared_ptr<PublisherDelegate>& pub_delegate,
                     const quicr::Namespace& quicr_namespace,
                     const std::string& origin_url,
                     const std::string& auth_token,
                     bytes&& payload);
  void PublishIntentEnd(const quicr::Namespace& quicr_namespace,
                        const std::string& auth_token);
  void Subscribe(std::shared_ptr<SubscriberDelegate>& subscriber_delegate,
                 const quicr::Namespace& quicr_namespace,
                 const SubscribeIntent& intent,
                 const std::string& origin_url,
                 bool use_reliable_transport,
                 const std::string& auth_token,
                 bytes&& e2e_token);
  void Unsubscribe(const quicr::Namespace& quicr_namespace,
                   const std::string& origin_url,
                   const std::string& auth_token);
  void PublishNamedObject(const quicr::Name& quicr_name,
                          uint8_t priority,
                          uint16_t expiry_age_ms,
                          bool use_reliable_transport,
                          bytes&& data);
  void PublishNamedObjectFragment(const quicr::Name& quicr_name,
                                  uint8_t priority,
                                  uint16_t expiry_age_ms,
                                  bool use_reliable_transport,
                                  const uint64_t& offset,
                                  bool is_last_fragment,
                                  bytes&& data);

  ////////////////////////////////////////////////////////////////////////////
  // End of functions to satisfy the QuicRClient interface
  ////////////////////////////////////////////////////////////////////////////

protected:
  void PublishEndNotify(PubSubRecord& publisher);
  void UnsubscribeNotify(PubSubRecord& subscriber);

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

  std::pair<bool, QUICStreamID> InitiateRequest(
    const std::string& method,
    const std::string path,
    const std::vector<std::uint8_t>& request_body);

  void HandleSubscriberData(QUICStreamID stream_id, RequestData* request);
  void HandleSubscribeResponse(QUICStreamID stream_id,
                               std::vector<std::uint8_t>& response);
  void HandleSubscribedObject(QUICStreamID stream_id,
                              bool reliable_transport,
                              std::vector<std::uint8_t>& object);
  void HandleSubscribeEnded(QUICStreamID stream_id,
                            SubscribeResult::SubscribeStatus reason);
  void HandlePublishIntentResponse(QUICStreamID stream_id,
                                   RequestData* request);

  bool InitiateConnectionClosure();
  void ProcessHeader(QUICStreamID stream_id,
                     std::string& name,
                     std::string& value);
  void ProcessSetting(std::uint64_t identifier, std::uint64_t value);
  RequestData* FindRequest(QUICStreamID stream_id_id);
  void ExpungeRequest(QUICStreamID stream_id_id);
  static int SettingsCallback(std::uint64_t identifier,
                              std::uint64_t value,
                              void* argp)
  {
    H3ClientConnection* connection;
    connection = reinterpret_cast<H3ClientConnection*>(argp);
    if (connection) connection->ProcessSetting(identifier, value);
    return 0;
  }
  static int HeaderCallback(std::uint8_t* name,
                            std::size_t name_len,
                            std::uint8_t* value,
                            std::size_t value_len,
                            void* argp)
  {
    std::pair<H3ClientConnection*, QUICStreamID>* stream_data;

    stream_data =
      reinterpret_cast<std::pair<H3ClientConnection*, QUICStreamID>*>(argp);

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

  void PublishNamedObjectFragmented(std::unique_lock<std::mutex>& lock,
                                    const PubSubRecord& publisher,
                                    const quicr::Name& quicr_name,
                                    bytes&& data);

  // Return true if the HTTP status code indicates success
  constexpr bool IsSuccess(unsigned status_code)
  {
    return (status_code >= 200) && (status_code <= 299);
  }

  std::atomic<bool> terminate; // Connection terminating flag

  cantina::LoggerPointer logger;                 // Logger object
  cantina::TimerManagerPointer timer_manager;    // Timer manager
  cantina::AsyncRequestsPointer async_requests;  // Asynchronous requests
  cantina::NetworkPointer network;               // Network object
  PubSubRegistryPointer pub_sub_registry;        // Pub/Sub Registry
  const socket_t data_socket;                    // Socket for communication
  std::size_t max_send_size;                     // Max sending packet size
  const std::size_t max_recv_size;               // Max receiving packet size
  bool use_datagrams;                            // Attempt to use datagrams?
  bool using_datagrams;                          // Connection using datagrams?
  QUICConnectionID local_cid;                    // Connection ID
  cantina::NetworkAddress local_address;         // Local server address
  std::string hostname;                          // Remote hostname
  quiche_conn* quiche_connection;                // Quiche Connection
  bool publisher;                                // Acting as publisher?
  std::string space_name;                        // Space to pub/sub
  H3ConnectionState connection_state;            // Current connection state
  ClosureCallback closure_callback;              // Notify pending closure
  const cantina::RegistrationID registration_id; // Value to pass on closure
  bool closure_notification;                     // Sent closure notification?
  quiche_h3_config* http3_config;                // HTTP/3 configuration
  quiche_h3_conn* http3_connection;              // HTTP/3 connection
  bool settings_received;                        // Settings received
  bool timer_create_pending;                     // Is a timer creation pending?
  std::deque<cantina::DataPacket> incoming_packets;
  // Incoming packets to process
  std::deque<cantina::TimerID> timeout_timers;  // Timeout timer IDs
  cantina::TimerID heartbeat_timer;             // Heartbeat (keepalive) timer
  std::map<QUICStreamID, RequestData> requests; // Request information
  std::map<std::uint64_t, std::uint64_t> connection_settings;
  FragmentAssembler fragment_assembler;         // Datagram fragment assembler
  // Connections settings
  std::mutex connection_lock; // Connection syncronization
};

typedef std::shared_ptr<H3ClientConnection> H3ClientConnectionPointer;

} // namespace quicr
