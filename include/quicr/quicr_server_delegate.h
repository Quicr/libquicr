/*
 *  quicr_server_delegate.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 *
 *  Description:
 *      This defined the server delegate interface utilized by the library to
 *      deliver information to the application.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include <cstdint>
#include <string>

#include "quicr/encode.h"
#include "quicr/message_buffer.h"
#include "quicr/quicr_common.h"

#include <transport/transport.h>

namespace quicr {

/**
 * Server delegate QUICR callback methods implemented by the QUICR Server
 * implementation
 */
class ServerDelegate
{
public:
  ServerDelegate() = default;
  virtual ~ServerDelegate() = default;

  /**
   * @brief  Reports interest to publish under given
   * quicr::Name.
   *
   * @param
   * @param namespace             : Identifies QUICR namespace
   * @param origin_url            : Origin serving the QUICR Session
   * @param use_reliable_transport: Reliable or Unreliable transport
   * @param auth_token            : Auth Token to validate the Subscribe Request
   * @parm e2e_token              : Opaque token to be forwarded to the Origin
   *
   *  @details Entities processing the Publish Intent MUST validate the request
   *           against the auth_token, verify if the Origin specified
   *           in the origin_url is trusted and forward the request to the
   *           next hop Relay for that
   *           Origin or to the Origin (if it is the next hop) unless the entity
   *           itself the Origin server.
   *           It is expected for the Relays to store the publisher state
   * mapping the namespaces and other relation information.
   */
  virtual void onPublishIntent(const quicr::Namespace& quicr_name,
                               const std::string& origin_url,
                               bool use_reliable_transport,
                               const std::string& auth_token,
                               bytes&& e2e_token) = 0;

  // TODO:Document this
  virtual void onPublishIntentEnd(const quicr::Namespace& quicr_namespace,
                                  const std::string& auth_token,
                                  bytes&& e2e_token) = 0;

  /**
   * @brief Reports arrival of fully assembled QUICR object under the name
   *
   * @param context_id               : Context id the message was received on
   * @param stream_id                : Stream ID the message was received on
   * @param use_reliable_transport   : Indicates the preference for the object's
   *                                   transport, if forwarded.
   * @param datagram                 : QuicR Published Message Datagram
   *
   * @note: It is important that the implementations not perform
   *         compute intensive tasks in this callback, but rather
   *         copy/move the needed information and hand back the control
   *         to the stack
   *
   *  @note: Both the on_publish_object and on_publish_object_fragment
   *         callbacks will be called. The delegate implementation
   *         shall decide the right callback for their usage.
   */
  virtual void onPublisherObject(
    const qtransport::TransportContextId& context_id,
    const qtransport::StreamId& stream_id,
    bool use_reliable_transport,
    messages::PublishDatagram&& datagram) = 0;

  /**
   * @brief Report arrival of subscribe request for a QUICR Namespace
   *
   * @details Entities processing the Subscribe Request MUST validate the
   * 		request against the token, verify if the Origin specified in the
   * origin_url is trusted and forward the request to the next hop Relay for
   * that Origin or to the Origin (if it is the next hop) unless the entity
   *    itself the Origin server.
   *    It is expected for the Relays to store the subscriber state
   *    mapping the subscribe context, namespaces and other relation
   * information.
   *
   * @param namespace             : Identifies QUICR namespace
   * @param subscriber_id            Subscriber ID connection/transport that
   *                                 sent the message
   * @param context_id               : Context id the message was received on
   * @param stream_id                : Stream ID the message was received on
   * @param subscribe_intent      : Subscribe intent to determine the start
   * point for serving the mactched objects. The application may choose a
   * different intent mode, but must be aware of the effects.
   * @param origin_url            : Origin serving the QUICR Session
   * @param use_reliable_transport: Reliable or Unreliable transport
   * @param auth_token            : Auth Token to valiadate the Subscribe
   * Request
   * @param payload               : Opaque payload to be forwarded to the Origin
   *
   */
  virtual void onSubscribe(const quicr::Namespace& quicr_namespace,
                           const uint64_t& subscriber_id,
                           const qtransport::TransportContextId& context_id,
                           const qtransport::StreamId& stream_id,
                           const SubscribeIntent subscribe_intent,
                           const std::string& origin_url,
                           bool use_reliable_transport,
                           const std::string& auth_token,
                           bytes&& data) = 0;

  /**
   * @brief Unsubscribe callback method
   *
   * @details Called for each unsubscribe message
   *
   * @param quicr_namespace          QuicR name/len
   * @param subscriber_id            Subscriber ID connection/transport that
   *                                 sent the message
   * @param auth_token               Auth token to verify if value
   */
  virtual void onUnsubscribe(const quicr::Namespace& quicr_namespace,
                             const uint64_t& subscriber_id,
                             const std::string& auth_token) = 0;
};

} // namespace quicr
