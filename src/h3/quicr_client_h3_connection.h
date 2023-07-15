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
#include "h3_connection_base.h"
#include "pub_sub_registry.h"
#include "quic_identifier.h"
#include "quiche.h"
#include "quiche_types.h"
#include "quicr/quicr_client_delegate.h"
#include "quicr/quicr_common.h"
#include "quicr/name.h"
#include "quicr/namespace.h"

namespace quicr {

// H3ClientConnection connection object
class H3ClientConnection : public H3ConnectionBase
{
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
                     quiche_conn* quiche_connection,
                     std::uint64_t heartbeat_interval,
                     const ClosureCallback closure_callback,
                     const std::string& hostname);
  virtual ~H3ClientConnection();

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
  void PublishEndNotify(const PubSubRecord& publisher);
  void UnsubscribeNotify(const PubSubRecord& subscriber);

  virtual void HandleIncrementalRequestData(QUICStreamID stream_id,
                                            RequestData* request) override;
  virtual bool HandleCompletedRequest(QUICStreamID stream_id,
                                      RequestData* request) override;
  virtual void HandleReceivedDatagram(
    QUICStreamID stream_id,
    std::vector<std::uint8_t>& datagram) override;

  std::pair<bool, QUICStreamID> InitiateRequest(
    const std::string& method,
    const std::string path,
    std::vector<std::uint8_t>&& request_body);

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

  void PublishNamedObjectFragmented(std::unique_lock<std::mutex>& lock,
                                    const PubSubRecord& publisher,
                                    const quicr::Name& quicr_name,
                                    bytes&& data);

  // Return true if the HTTP status code indicates success
  constexpr bool IsSuccess(unsigned status_code)
  {
    return (status_code >= 200) && (status_code <= 299);
  }

  std::string hostname;                          // Remote hostname
  FragmentAssembler fragment_assembler;          // Datagram fragment assembler
};

typedef std::shared_ptr<H3ClientConnection> H3ClientConnectionPointer;

} // namespace quicr
