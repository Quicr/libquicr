#pragma once

#include <optional>
#include <string>
#include <vector>

#include <quicr/quicr_common.h>
#include <transport/transport.h>

/*
 * API for implementing server side of the QUICR protocol
 */
namespace quicr {

/*
 * Server delegate QUICR callback methods implemented by the QUICR Server
 * implementation
 */
class ServerDelegate
{
public:
  virtual ~ServerDelegate() = default;

  /*
   * @brief  Reports interest to publish under given
   * QuicrName.
   *
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
  virtual void onPublishIntent(const QUICRNamespace& quicr_name,
                               const std::string& origin_url,
                               bool use_reliable_transport,
                               const std::string& auth_token,
                               bytes&& e2e_token) = 0;

  /*
   * @brief Reports arrival of fully assembled QUICR object under the name
   *
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param priority                 : Identifies the relative priority of the
   *                                   current object
   * @param expiry_age_ms            : Time hint for the object to be in cache
   *                                   before being purged after reception
   * @param use_reliable_transport   : Indicates the preference for the object's
   *                                   transport, if forwarded.
   * @param data                     : Opaque payload of the fragment
   *
   *  @details Entities implementing this API shall perform look up
   *           on active subscriptions for the name to forward the
   *           objects.
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
  virtual void onPublisherObject(const QUICRName& quicr_name,
                                 uint8_t priority,
                                 uint16_t expiry_age_ms,
                                 bool use_reliable_transport,
                                 bytes&& data) = 0;

  /*
   * @brief arrival of published QUICR object fragment under a Name
   *
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param priority                 : Identifies the relative priority of the
   *                                   current object
   * @param expiry_age_ms            : Time hint for the object to be in cache
   *                                      before being purged after reception
   * @param use_reliable_transport   : Indicates the preference for the object's
   *                                   transport, if forwarded.
   * @param offset                   : Current fragment offset
   * @param is_last_fragment         : Indicates if the current fragment is the
   *                                   last fragment
   * @param data                     : Opaque payload of the fragment
   *
   *  @details For cases where the latency needs to kept low, Relays
   *           shall use the fragment API to store and forward objects
   *           as fragments.
   *
   *  @note: It is important that the implementations not perform
   *         compute intensive tasks in this callback, but rather
   *         copy/move the needed information and hand back the control
   *         to the stack
   */
  virtual void onPublishedFragment(const QUICRName& quicr_name,
                                   uint8_t priority,
                                   uint16_t expiry_age_ms,
                                   bool use_reliable_transport,
                                   const uint64_t& offset,
                                   bool is_last_fragment,
                                   bytes&& data)
  {
  }

  /*
   * @brief Report arrival of subscribe request for a QUICR Namespace
   *
   * @param namespace             : Identifies QUICR namespace
   * @param subscribe_intent      : Subscribe intent to determine the start
   * point for serving the mactched objects. The application may choose a
   * different intent mode, but must be aware of the effects.
   * @param origin_url            : Origin serving the QUICR Session
   * @param use_reliable_transport: Reliable or Unreliable transport
   * @param auth_token            : Auth Token to valiadate the Subscribe
   * Request
   * @param payload               : Opaque payload to be forwarded to the Origin
   *
   * @details Entities processing the Subscribe Request MUST validate the
   * request against the token, verify if the Origin specified in the origin_url
   *          is trusted and forward the request to the next hop Relay for that
   *          Origin or to the Origin (if it is the next hop) unless the entity
   *          itself the Origin server.
   *          It is expected for the Relays to store the subscriber state
   * mapping the subscribe context, namespaces and other relation information.
   */
  virtual void onSubscribe(const QUICRNamespace& quicr_namespace,
                           const SubscribeIntent subscribe_intent,
                           const std::string& origin_url,
                           bool use_reliable_transport,
                           const std::string& auth_token,
                           bytes&& data)
  {
  }
};

class QuicRServer
{
public:
  /*
   * Start the  QUICR server at the port specified.
   *  @param delegate: Callback handlers for QUICR operations
   */
  QuicRServer(qtransport::ITransport&, ServerDelegate& delegate);

  // Transport APIs
  bool is_transport_ready();

  /**
   * @brief Run Server API event loop
   *
   * @details This method will open listening sockets and run an event loop
   *    for callbacks.
   *
   * @returns true if error, false if no error
   */
  bool run();

  /*
   * @brief Send publish intent response
   *
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param result                : Status of Publish Intetn
   *
   * @details Entities processing the Subscribe Request MUST validate the
   * request
   * @todo: Add payload with origin signed blob
   */
  void publishIntentResponse(const QUICRNamespace& quicr_namespace,
                             const PublishIntentResult& result);

  /*
   * @brief Send subscribe response
   *
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param result                : Status of Subscribe operation
   *
   * @details Entities processing the Subscribe Request MUST validate the
   * request
   * @todo: Add payload with origin signed blob
   */
  void subscribeResponse(const QUICRNamespace& quicr_namespace,
                         const SubscribeResult& result);

  /*
   * @brief Indicates a given subscription is no longer valid
   *
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param result                : Status of Subscribe operation
   *
   * @details  Subscription can terminate when a publisher terminated
   *           the stream or subscription timeout or other application
   *           reasons
   */
  void subscriptionEnded(const QUICRNamespace& quicr_namespace,
                         const SubscribeResult& result);

  /*
   * @brief Send a named QUICR media object
   *
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param priority                 : Identifies the relative priority of the
   * current object
   * @param best_before              : TTL for the object to be useful for the
   * application
   * @param use_reliable_transport   : Indicates the preference for the object's
   *                                   transport, if forwarded.
   * @param data                     : Opaque object payload
   *
   */
  void sendNamedObject(const QUICRName& quicr_name,
                       uint8_t priority,
                       uint64_t best_before,
                       bool use_reliable_transport,
                       bytes&& data);

  /*
   * @brief Send a named QUICR media object fragment
   *
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param priority                 : Identifies the relative priority of the
   * current object
   * @param best_before              : TTL for the object to be useful for the
   * application
   * @param use_reliable_transport   : Indicates the preference for the object's
   *                                   transport, if forwarded.
   * @param offset                   : Current fragment offset
   * @param is_last_fragment         : Indicates if the current fragment is the
   *                                   last fragment
   * @param data                     : Opaque object fragment payload
   */
  void sendNamedFragment(const QUICRName& name,
                         uint8_t priority,
                         uint64_t best_before,
                         bool use_reliable_transport,
                         uint64_t offset,
                         bool is_last_fragment,
                         bytes&& data);
};

} // namespace quicr
