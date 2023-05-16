/*
 *  quicr_server_session.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 *
 *  Description:
 *      This is an interface specification for the session layer sitting at the
 *      next level of the server library.  The topology looks like this:
 *
 *          QuicRServer => QuicRServerSession => Transport
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include "quicr/quicr_server_delegate.h"
#include "transport/transport.h"

/*
 * QUICR Server Session Interface
 */
namespace quicr {

class QuicRServerSession
{
public:

  // Default constructor and virtual destructor
  QuicRServerSession() = default;
  virtual ~QuicRServerSession() = default;

  // Transport APIs
  virtual bool is_transport_ready() = 0;

  /**
   * @brief Run Server API event loop
   *
   * @details This method will open listening sockets and run an event loop
   *    for callbacks.
   *
   * @returns true if error, false if no error
   */
  virtual bool run() = 0;

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
  virtual void publishIntentResponse(const quicr::Namespace& quicr_namespace,
                                     const PublishIntentResult& result) = 0;

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
  virtual void subscribeResponse(const uint64_t& subscriber_id,
                                 const quicr::Namespace& quicr_namespace,
                                 const SubscribeResult& result) = 0;

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
  virtual void subscriptionEnded(
    const uint64_t& subscriber_id,
    const quicr::Namespace& quicr_namespace,
    const SubscribeResult::SubscribeStatus& reason) = 0;

  /**
   * @brief Send a named QUICR media object
   *
   * @param subscriber_id            : Subscriber ID to send the message to
   * @param use_reliable_transport   : Indicates the preference for the object's
   *                                   transport, if forwarded.
   * @param datagram                 : QuicR Publish Datagram to send
   *
   */
  virtual void sendNamedObject(const uint64_t& subscriber_id,
                               bool use_reliable_transport,
                               const messages::PublishDatagram& datagram) = 0;
};

} // namespace quicr
