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
#include <cstddef>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include "cantina/async_requests.h"
#include "cantina/logger.h"
#include "cantina/registration_id.h"
#include "cantina/timer_manager.h"
#include "h3_connection_base.h"
#include "pub_sub_registry.h"
#include "quic_identifier.h"
#include "quiche.h"
#include "quiche_types.h"
#include "quicr/quicr_common.h"
#include "quicr/message_buffer.h"
#include "quicr/quicr_server_delegate.h"
#include "h3_common.h"

namespace quicr::h3 {

// QUICR H3 Server Connection object
class H3ServerConnection : public H3ConnectionBase
{
public:
  H3ServerConnection(const cantina::LoggerPointer& parent_logger,
                     const cantina::TimerManagerPointer& timer_manager,
                     const cantina::AsyncRequestsPointer& async_requests,
                     const TransportPointer& transport,
                     const StreamContext& stream_context,
                     const PubSubRegistryPointer& pub_sub_registry,
                     std::size_t max_send_size,
                     std::size_t max_recv_size,
                     bool use_datagrams,
                     const QUICConnectionID& local_cid,
                     const cantina::NetworkAddress& local_address,
                     const cantina::NetworkAddress& remote_address,
                     quiche_conn* quiche_connection,
                     std::uint64_t heartbeat_interval,
                     const ClosureCallback closure_callback,
                     ServerDelegate& server_delegate);
  virtual ~H3ServerConnection();

  ////////////////////////////////////////////////////////////////////////////
  // Functions to satisfy the QUICR server interface
  ////////////////////////////////////////////////////////////////////////////

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

  ////////////////////////////////////////////////////////////////////////////
  // End of functions to satisfy the QUICR server interface
  ////////////////////////////////////////////////////////////////////////////

protected:
  void PublishEndNotify(const PubSubRecord& publisher);
  void UnsubscribeNotify(const PubSubRecord& subscriber);

  virtual void HandleIncrementalRequestData(QUICStreamID,
                                            RequestData*) override
  {
    // Nothing to do
  }
  virtual bool HandleCompletedRequest(QUICStreamID stream_id,
                                      RequestData* request) override;
  virtual void HandleReceivedDatagram(
    QUICStreamID stream_id,
    quicr::messages::MessageBuffer& datagram) override;

  std::pair<bool, unsigned> ProcessRequest(QUICStreamID stream_id,
                                           RequestData* request);
  void SendHTTPResponse(QUICStreamID stream_id,
                        unsigned status_code,
                        const HTTPHeaders& response_headers,
                        quicr::messages::MessageBuffer& response_body,
                        bool prefix_length,
                        bool close_stream = true);
  bool HandlePublishIntent(QUICStreamID stream_id, RequestData* request);
  unsigned HandlePublishIntentEnd(RequestData* request);
  unsigned HandlePublishNamedObject(QUICStreamID stream_id,
                                    RequestData* request);
  bool HandleSubscribe(QUICStreamID stream_id, RequestData* request);
  bool HandleUnsubscribe(RequestData* request);

  ServerDelegate& server_delegate;
};

// Define a shared pointer type for the H3ServerConnection
typedef std::shared_ptr<H3ServerConnection> H3ServerConnectionPointer;

} // namespace quicr::h3
