/*
 *  quicr_client_session.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 *
 *  Description:
 *      This is an interface specification for the session layer sitting at the
 *      next level of the client library.  The topology looks like this:
 *
 *          QuicRClient => ClientSession => Transport
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include "quicr_client_delegate.h"
#include "quicr_common.h"

#include <quicr/namespace.h>
#include <transport/transport.h>

#include <cstdint>
#include <memory>
#include <string>

/*
 * QUICR Client Session Interface
 */
namespace quicr {

class ClientSession
{
public:
  ClientSession() = default;
  virtual ~ClientSession() = default;

  /**
   * @brief Connects the session using the info provided on construction.
   * @returns True if connected, false otherwise.
   */
  virtual bool connect() = 0;

  /**
   * @brief Disconnects the session from the relay.
   * @returns True if successful, false if some error occurred.
   */
  virtual bool disconnect() = 0;

  /**
   * @brief Checks if the session is connected.
   * @returns True if transport has started and connection has been made. False
   *          otherwise.
   */
  virtual bool connected() const = 0;

  /**
   * @brief Publish intent to publish on a QUICR Namespace
   *
   * @param pub_delegate            : Publisher delegate reference
   * @param quicr_namespace         : Identifies QUICR namespace
   * @param origin_url              : Origin serving the QUICR Session
   * @param auth_token              : Auth Token to validate the Subscribe
   *                                  Request
   * @param payload                 : Opaque payload to be forwarded to the
   *                                  Origin
   * @param transport_mode          : Transport mode to use for publishing objects
   *                                  published objects
   * @param priority                : Identifies the relative priority for the stream if reliable
   */
  virtual bool publishIntent(std::shared_ptr<PublisherDelegate> pub_delegate,
                             const quicr::Namespace& quicr_namespace,
                             const std::string& origin_url,
                             const std::string& auth_token,
                             bytes&& payload,
                             const TransportMode transport_mode,
                             uint8_t priority) = 0;

  /**
   * @brief Stop publishing on the given QUICR namespace
   *
   * @param quicr_namespace : Identifies QUICR namespace
   * @param auth_token      : Auth Token to validate the Subscribe Request
   */
  virtual void publishIntentEnd(const quicr::Namespace& quicr_namespace,
                                const std::string& auth_token) = 0;

  /**
   * @brief Perform subscription operation a given QUICR namespace
   *
   * @param subscriber_delegate     : Reference to receive callback for
   *                                  subscriber operations
   * @param quicr_namespace         : Identifies QUICR namespace
   * @param intent                  : Subscribe intent to determine the start
   *                                  point for serving the matched objects. The
   *                                  application may choose a different intent
   *                                  mode, but must be aware of the effects.
   * @param transport_mode          : Transport mode to use for received subscribed objects
   * @param origin_url              : Origin serving the QUICR Session
   * @param auth_token              : Auth Token to validate the Subscribe
   *                                  Request
   * @param e2e_token               : Opaque token to be forwarded to the Origin
   * @param priority                : Identifies the relative priority for the data flow when reliable
   *
   * @details Entities processing the Subscribe Request MUST validate the
   *          request against the token, verify if the Origin specified in the
   *          origin_url is trusted and forward the request to the next hop
   *          Relay for that Origin or to the Origin (if it is the next hop)
   *          unless the entity itself the Origin server. It is expected for the
   *          Relays to store the subscriber state mapping the subscribe
   *          context, namespaces and other relation information.
   */
  virtual void subscribe(
    std::shared_ptr<SubscriberDelegate> subscriber_delegate,
    const quicr::Namespace& quicr_namespace,
    const SubscribeIntent& intent,
    const TransportMode transport_mode,
    const std::string& origin_url,
    const std::string& auth_token,
    bytes&& e2e_token,
    const uint8_t priority) = 0;

  /**
   * @brief Stop subscription on the given QUICR namespace
   *
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param origin_url            : Origin serving the QUICR Session
   * @param auth_token            : Auth Token to validate the Subscribe
   *                                Request
   */
  virtual void unsubscribe(const quicr::Namespace& quicr_namespace,
                           const std::string& origin_url,
                           const std::string& auth_token) = 0;

  /**
   * @brief Get subscription state
   */
  virtual SubscriptionState getSubscriptionState(const quicr::Namespace& quicr_namespace) = 0;

  /**
   * @brief Publish Named object
   *
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param priority                 : Identifies the relative priority of the
   *                                   current object
   * @param expiry_age_ms            : Time hint for the object to be in cache
   *                                      before being purged after reception
   * @param data                     : Opaque payload
   * @param trace                    : Method trace vector
   *
   */
  virtual void publishNamedObject(const quicr::Name& quicr_name,
                                  uint8_t priority,
                                  uint16_t expiry_age_ms,
                                  bytes&& data,
                                  std::vector<qtransport::MethodTraceItem> &&trace) = 0;

  /**
   * @brief Publish Named object
   *
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param priority                 : Identifies the relative priority of the
   *                                   current object
   * @param expiry_age_ms            : Time hint for the object to be in cache
                                       before being purged after reception
   * @param offset                   : Current fragment offset
   * @param is_last_fragment         : Indicates if the current fragment is the
   * @param data                     : Opaque payload of the fragment
   */
  virtual void publishNamedObjectFragment(const quicr::Name& quicr_name,
                                          uint8_t priority,
                                          uint16_t expiry_age_ms,
                                          const uint64_t& offset,
                                          bool is_last_fragment,
                                          bytes&& data) = 0;
};

using QuicRClientSession [[deprecated(
  "quicr::QuicRClientSession stutters, use quicr::ClientSession")]] =
  quicr::ClientSession;

} // namespace quicr
