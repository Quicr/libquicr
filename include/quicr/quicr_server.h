#pragma once

#include <optional>
#include <string>
#include <vector>

#include <quicr/quicr_common.h>

/*
 * API for implementing server side of the QUICR protocol
 */

namespace quicr
{

  /*
  * Server delegate QUICR callback methods implemented by the QUICR Server
  * implementation
  */
class ServerDelegate
{
  virtual ~ServerDelegate() = default;

  /*
   * @brief  Reports interest to publish under given
   * QuicrName. 
   * 
   * @param context_id            : Context identifier that binds the namespace 
   *                                to the publisher. Implementations must pass
   *                                in this context identifer when sending
   *                                response to the publish_intent.
   * @param namespace             : Identifies QUICR namespace
   * @param origin_url            : Origin serving the QUICR Session    
   * @param use_reliable_transport: Reliable or Unreliable transport 
   * @param auth_token            : Auth Token to valiadate the Subscribe Request
   * @parm payload                : Opaque payload to be forwarded to the Origin 
   * 
   *  @details Entities processing the Publish Intent MUST validate the request 
   *           against the token, verify if the Origin specified in the origin_url
   *           is trusted and forward the request to the next hop Relay for that
   *           Origin or to the Origin (if it is the next hop) unless the entity 
   *           itself the Origin server.
   *           It is expected for the Relays to store the publisher state mapping
   *           the context, namespaces and other relation information.
   */
  virtual void on_publish_intent(const QUICRContext& context_id,
                                 const QUICRNamespace& quicr_name,
                                 const std::string& origin_url,
                                 bool use_reliable_transport,
                                 const std::string& auth_token,
                                 bytes&& payload) = 0;

  /*
   * @brief Reports arrival of fully assembled QUICR object under the name
   *
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param priority                 : Identifies the relative priority of the current object
   * @param best_before              : TTL for the object to be useful for the application
   * @param use_reliable_transport   : Indicates the preference for the object's
   *                                   transport, if forwarded.
   * @param fragment_number          : Current Fragment Identifier
   * @param num_total_fragments      : identifies current object's fragment count
   * @param data                     : Opaque payload of the fragment
   * 
   *  @details Entities implementing this API shall perform look up
   *           on active subscriptions for the name to forward the
   *           objects.
   *  
   * @note: It is important that the implementations not perform 
   *         compute intesive tasks in this callback, but rather
   *         copy/move the needed information and hand back the control
   *         to the stack  
   * 
   *  @note: Both the on_publish_object and on_publish_object_fragment
   *         callbacks will be called. The delegate implementation
   *         shall decide the right callback for their usage.
   */
  virtual void on_published_object(const QUICRName& quicr_name,
                                   uint8_t priority,
                                   uint64_t best_before,
                                   bool use_reliable_transport,
                                   bytes&& data) = 0;

  /*
   * @brief arrival of published QUICR object fragment under a Name
   *
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param priority                 : Identifies the relative priority of the current object
   * @param best_before              : TTL for the object to be useful for the application
   * @param use_reliable_transport   : Indicates the preference for the object's
   *                                   transport, if forwarded.
   * @param fragment_number          : Current Fragment Identifier
   * @param num_total_fragments      : identifies current object's fragment count
   * @param data                     : Opaque payload of the fragment
   * 
   *  @details For cases where the latecny needs to kept low, Relays 
   *           shall use the framgment API to store and forward objects
   *           as fragments. 
   * 
   *  @note: It is important that the implementations not perform 
   *         compute intesive tasks in this callback, but rather
   *         copy/move the needed information and hand back the control
   *         to the stack  
   * 
   *  @note: Both the on_publish_object and on_publish_object_fragment
   *         callbacks will be called. The delegate implementation
   *         shall decide the right callback for their usage.
   */
  virtual void on_published_fragment(const QUICRName& quicr_name,
                                     uint8_t priority,
                                     uint64_t best_before,
                                     bool use_reliable_transport,
                                     const uint16_t fragment_number,
                                     uint16_t num_total_fragments,
                                     bytes&& data) = 0;

  /*
   * @brief Report arrival of subscribe request for a QUICR Namespace
   * 
   * @param context_id            : Opaque context identifier for correlation.
   * @param namespace             : Identifies QUICR namespace
   * @param subscribe_intent      : Subscribe intent to determine the start point for 
   *                                 serving the mactched objects. The application
   *                                 may choose a different intent mode, but must
   *                                 be aware of the effects.
   * @param origin_url            : Origin serving the QUICR Session    
   * @param use_reliable_transport: Reliable or Unreliable transport 
   * @param auth_token            : Auth Token to valiadate the Subscribe Request
   * @param payload               : Opaque payload to be forwarded to the Origin 
   * 
   * @details Entities processing the Subscribe Request MUST validate the request 
   *          against the token, verify if the Origin specified in the origin_url
   *          is trusted and forward the request to the next hop Relay for that
   *          Origin or to the Origin (if it is the next hop) unless the entity 
   *          itself the Origin server.
   *          It is expected for the Relays to store the subscriber state mapping
   *          the subscribe context, namespaces and other relation information.
   */
  virtual void on_subscribe(const QUICRContext& context_id,
                            const QUICRNamespace& quicr_namespace,
                            const SubscribeIntent subscribe_intent,
                            const std::string& origin_url,
                            bool use_reliable_transport,
                            const std::string& auth_token,
                            bytes&& data) = 0;
};

class QuicRServer
{

  /*
   * Start the  QUICR server at the port specified.
   *  @param delegate: Callback handlers for QUICR operations
   */
  QuicRServer(const uint16_t port, ServerDelegate& delegate) explicit;

  // Transport APIs
  bool is_transport_ready();

  /*
   * @brief Send publish intent response
   * 
   * @param context_id            : Opaque context identifier for correlation
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param result                : Status of Publish Intetn 
   * 
   * @details Entities processing the Subscribe Request MUST validate the request 
   * @todo: Add payload with origin signed blob
   */ 
  void publish_intent_response(const QUICRContext& context_id,
                               const QUICRNamespace& quicr_namespace,
                               const PublishIntentResult& result);

  /*
   * @brief Send subscribe response 
   * 
   * @param context_id            : Opaque context identifier for correlation
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param result                : Status of Subscribe operation 
   * 
   * @details Entities processing the Subscribe Request MUST validate the request 
   * @todo: Add payload with origin signed blob
   */
  void subscribe_response(const QUICRContext& context_id,
                          const QUICRNamespace& quicr_namespace,
                          const SubscribeResult& result);

  
  /*
   * @brief Indicates a given subscription is no longer valid
   * 
   * @param context_id            : Opaque context identifier for correlation
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param result                : Status of Subscribe operation 
   * 
   * @details  Subscription can terminate when a publisher terminated
   *           the stream or subscription timeout or other application
   *           reasons
   */
  void subscription_ended(const QUICRContext& context_id,
                          const QUICRNamespace& quicr_namespace,
                          const SubscribeResult& result);

  /*
   * @brief Send a named QUICR media object
   *
   * @param context_id               : Opaque context identifier for correlation
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param priority                 : Identifies the relative priority of the current object
   * @param best_before              : TTL for the object to be useful for the application
   * @param use_reliable_transport   : Indicates the preference for the object's
   *                                   transport, if forwarded.
   * @param data                     : Opaque object payload
   * 
   */
  void send_named_object(const QUICRContext& context_id,
                         const QUICRName& quicr_name,
                         uint8_t priority,
                         uint64_t best_before,
                         bool use_reliable_transport,
                         bytes&& data);

   /*
    * @brief Send a named QUICR media object fragment
    *
    * @param context_id               : Opaque context identifier for correlation
    * @param quicr_name               : Identifies the QUICR Name for the object
    * @param priority                 : Identifies the relative priority of the current object
    * @param best_before              : TTL for the object to be useful for the application
    * @param use_reliable_transport   : Indicates the preference for the object's
    *                                   transport, if forwarded.
    * @param data                     : Opaque object fragment payload
    * 
   */
  void send_named_fragment(const QUICRContext& context_id,
                           const QUICRName& name,
                           uint8_t priority,
                           uint64_t best_before,
                           bool use_reliable_transport,
                           uint16_t fragment_number,
                           uint16_t num_total_fragments,
                           bytes&& data);
};
    
} // namespace quicr
