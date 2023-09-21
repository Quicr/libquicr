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

#include "quicr_client_common.h"
#include "quicr_client_delegate.h"
#include "quicr_common.h"

#include <quicr/namespace.h>

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
   * @brief Get the client status
   *
   * @details This method should be used to determine if the client is
   *   connected and ready for publishing and subscribing to messages.
   *   Status will indicate the type of error if not ready.
   *
   * @returns client status
   */
  virtual ClientStatus status() const = 0;

  /**
   * @brief Publish intent to publish on a QUICR Namespace
   *
   * @param pub_delegate            : Publisher delegate reference
   * @param quicr_namespace         : Identifies QUICR namespace
   * @param origin_url              : Origin serving the QUICR Session
   * @param auth_token              : Auth Token to validate the Subscribe
   * Request
   * @param payload                 : Opaque payload to be forwarded to the
   * Origin
   * @param use_reliable_transport  : Indicates to use reliable for matching
   * published objects
   */
  virtual bool publishIntent(std::shared_ptr<PublisherDelegate> pub_delegate,
                             const quicr::Namespace& quicr_namespace,
                             const std::string& origin_url,
                             const std::string& auth_token,
                             bytes&& payload,
                             bool use_reliable_transport) = 0;

  /**
   * @brief Stop publishing on the given QUICR namespace
   *
   * @param quicr_namespace        : Identifies QUICR namespace
   * @param origin_url             : Origin serving the QUICR Session
   * @param auth_token             : Auth Token to valiadate the Subscribe
   * Request
   * @param payload                : Opaque payload to be forwarded to the
   * Origin
   */
  virtual void publishIntentEnd(const quicr::Namespace& quicr_namespace,
                                const std::string& auth_token) = 0;

  /**
   * @brief Perform subscription operation a given QUICR namespace
   *
   * @param subscriber_delegate   : Reference to receive callback for subscriber
   *                                ooperations
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param subscribe_intent      : Subscribe intent to determine the start
   * point for serving the matched objects. The application may choose a
   * different intent mode, but must be aware of the effects.
   * @param origin_url            : Origin serving the QUICR Session
   * @param use_reliable_transport: Reliable or Unreliable transport
   * @param auth_token            : Auth Token to validate the Subscribe Request
   * @param e2e_token              : Opaque token to be forwarded to the Origin
   *
   * @details Entities processing the Subscribe Request MUST validate the
   * request against the token, verify if the Origin specified in the origin_url
   *          is trusted and forward the request to the next hop Relay for that
   *          Origin or to the Origin (if it is the next hop) unless the entity
   *          itself the Origin server.
   *          It is expected for the Relays to store the subscriber state
   * mapping the subscribe context, namespaces and other relation information.
   */
  virtual void subscribe(
    std::shared_ptr<SubscriberDelegate> subscriber_delegate,
    const quicr::Namespace& quicr_namespace,
    const SubscribeIntent& intent,
    const std::string& origin_url,
    bool use_reliable_transport,
    const std::string& auth_token,
    bytes&& e2e_token) = 0;

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
   * @brief Publish Named object
   *
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param priority                 : Identifies the relative priority of the
   *                                   current object
   * @param expiry_age_ms            : Time hint for the object to be in cache
   *                                      before being purged after reception
   * @param use_reliable_transport   : Indicates the preference for the object's
   *                                   transport, if forwarded.
   * @param data                     : Opaque payload
   *
   */
  virtual void publishNamedObject(const quicr::Name& quicr_name,
                                  uint8_t priority,
                                  uint16_t expiry_age_ms,
                                  bool use_reliable_transport,
                                  bytes&& data) = 0;

  /**
   * @brief Publish Named object
   *
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param priority                 : Identifies the relative priority of the
   *                                   current object
   * @param expiry_age_ms            : Time hint for the object to be in cache
                                       before being purged after reception
   * @param use_reliable_transport   : Indicates the preference for the object's
   *                                   transport, if forwarded.
   * @param offset                   : Current fragment offset
   * @param is_last_fragment         : Indicates if the current fragment is the
   * @param data                     : Opaque payload of the fragment
   */
  virtual void publishNamedObjectFragment(const quicr::Name& quicr_name,
                                          uint8_t priority,
                                          uint16_t expiry_age_ms,
                                          bool use_reliable_transport,
                                          const uint64_t& offset,
                                          bool is_last_fragment,
                                          bytes&& data) = 0;
};

using QuicRClientSession [[deprecated(
  "quicr::QuicRClientSession stutters, use quicr::ClientSession")]] =
  quicr::ClientSession;

} // namespace quicr
