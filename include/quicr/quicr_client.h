#include <optional>
#include <string>
#include <vector>

#include <quicr/quicr_common.h>

namespace quicr {

/*
 *  Subscriber delegate callback methods
 */

class SubscriberDelegate
{

  virtual ~SubscriberDelegate() = default;
  
  /**
   * @brief Callback for subscription response
   * 
   * @param quicr_namespace: QUICR Namespace associated with the Subscribe Request
   * @param result         : Result for the subscription
   * 
   *  @details This callback will be called when a subscription response
   *          is received, on error, or timeout. 
   */
  virtual void onSubscribeResponse(const QUICRNamespace& quicr_namespace,
                                   const SubscribeResult& result) = 0;

  /*
   * @brief Indicates a given subscription is no longer valid
   * 
   * @param quicr_namespace       : Identifies QUICR namespace
   * @parm result                 : Status of Subscribe operation 
   * 
   * @details  Subscription can terminate when a publisher terminated
   *           the stream or subscription timeout or other application
   *           reasons
   */
  virtual void onSubscriptionEnded(const QUICRNamespace& quicr_namespace,
                                   const SubscribeResult& reason) = 0;

  /*
   * @brief Report arrival of subscribed QUICR object fragment under a Name
   *
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param priority                 : Identifies the relative priority of the current object
   * @param best_before              : TTL for the object to be useful for the application
   * @param use_reliable_transport   : Indicates the preference for the object's
   *                                   transport, if forwarded.
   * @param data                   : Opaque payload of the fragment
   * 
   * 
   *  @note: It is important that the implementations not perform 
   *         compute intensive tasks in this callback, but rather
   *         copy/move the needed information and hand back the control
   *         to the stack  
   * 
   *  @note: Both the on_publish_object and on_publish_object_fragment
   *         callbacks will be called. The delegate implementation
   *         shall decide the right callback for their usage.
   */
  virtual void onSubscribedObject(const QUICRName& name,
                                  uint8_t priority,
                                  uint64_t best_before,
                                  bool use_reliable_transport,
                                  bytes&& data) {}

  /*
   * @brief Report arrival of subscribed QUICR object fragment under a Name
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
   * 
   *  @note: It is important that the implementations not perform 
   *         compute intensive tasks in this callback, but rather
   *         copy/move the needed information and hand back the control
   *         to the stack  
   * 
   *  @note: Both the on_publish_object and on_publish_object_fragment
   *         callbacks will be called. The delegate implementation
   *         shall decide the right callback for their usage.
   */
  virtual void onSubscribedObjectFragment(const QUICRName& name,
                                          uint8_t priority,
                                          uint64_t best_before,
                                          bool use_reliable_transport,
                                          const uint16_t fragment_number,
                                          uint16_t num_total_fragments,
                                          bytes&& data) {}
};

/*
 *  Publisher common delegate callback operations
 */
class PublisherDelegate
{
  virtual ~PublisherDelegate() = default;

  /*
   * @brief Callback on the response to the  publish intent
   * 
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param result                : Status of Publish Intetn 
   * 
   * @details Entities processing the Subscribe Request MUST validate the request 
   * @todo: Add payload with origin signed blob
   */ 
  void onPublishIntentResponse(const QUICRNamespace& quicr_namespace,
                               const PublishIntentResult& result);


  /*
   * @brief Reports result of fragment published generated
   *        by the local stack.
   *
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param fragment_number          : Current Fragment Identifier
   * @param num_total_fragments      : Identifies current object's fragment count
   * @param result                   : Result of the publish operation
   * 
   */
  virtual void onPublishFragmentResult(const QUICRName& quicr_name,
                                       const uint16_t fragment_number,
                                       uint16_t num_total_fragments,
                                       const PublishMsgResult& result) {}

 
  /*
   * @brief Reports result of obejct published under the Name, 
   *        by the local stack
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param result                   : Result of the publish operation
   * 
   */
  virtual void onPublishObjectResult(const QUICRName& quicr_name,
                                     const PublishMsgResult& result) = 0;
};

/*
 *   Client API for using QUICR Protocol
*/
class QuicRClient
{
  enum class ClientStatus {
      READY = 0,
      CONNECTING, 
      RELAY_HOST_INVALID,
      RELAY_PORT_INVALID,
      RELAY_NOT_CONNECTED,
      TRANSPORT_ERROR,
      UNAUTHORIZED,
      TERMINATED,
  };

  /*
   * @brief Setup a QUICR Client with publisher and subscriber functionality
   *
   * @param transport            QuicRTransport class implementation
   * @param subscriber_delagate : Reference to receive callback for subscriber operations
   * @param publisher_delgatw   : Reference to receive callback for publisher operations
   */
  QuicRClient(ITransport& transport,
              SubscriberDelegate& subscriber_delegate,
              PublisherDelegate& pub_delegate);

  /*
   * @brief Setup a QUICR Client with subscriber functionality
   *
   * @param transport            QuicRTransport class implementation
   * @param subscriber_delagate : Reference to receive callback for subscriber operations
   */
  QuicRClient(ITransport& transport,
              SubscriberDelegate& subscriber_delegate);

  /*
   * @brief Setup a QUICR Client with publisher functionality
   *
   * @param transport            QuicRTransport class implementation
   * @param publisher_delgatw   : Reference to receive callback for publisher operations
   */
  QuicRClient(ITransport& transport,
              PublisherDelegate& pub_delegate);

  /**
   * @brief Get the client status
   * 
   * @details This method should be used to determine if the client is
   *   connected and ready for publishing and subscribing to messages.
   *   Status will indicate the type of error if not ready. 
   * 
   * @returns client status
   */
  ClientStatus status();

  /**
   * @brief Run client API event loop
   *
   * @details This method will connect to the relay/transport and run
   *    an event loop for calling the callbacks
   *
   * @returns client status
   */
   virtual ClientStatus run() = 0;

  /*
   * @brief Publish intent to publish on a QUICR Namespace
   * 
   * @param quicr_namespace        : Identifies QUICR namespace
   * @param origin_url            : Origin serving the QUICR Session    
   * @param auth_token            : Auth Token to valiadate the Subscribe Request
   * @param payload               : Opaque payload to be forwarded to the Origin 
   */
  bool publishIntent(const QUICRNamespace& quicr_namespace,
                      const std::string& origin_url,
                      const std::string& auth_token,
                      bytes&& payload,
                      QUICRContext& context_id);

  /*
   * @brief Stop publishing on the given QUICR namespace
   * 
   * @param quicr_namespace        : Identifies QUICR namespace
   * @param origin_url            : Origin serving the QUICR Session    
   * @param auth_token            : Auth Token to valiadate the Subscribe Request
   * @param payload               : Opaque payload to be forwarded to the Origin 
   */
  void publishIntentEnd(const QUICRContext& context_id,
                        const QUICRNamespace& name,
                        const std::string& auth_token);

  /*
   * @brief Perform subscription operation a given QUICR namespace
   * 
   * @param namespace             : Identifies QUICR namespace
   * @param subscribe_intent      : Subscribe intent to determine the start point for 
   *                                 serving the matched objects. The application
   *                                 may choose a different intent mode, but must
   *                                 be aware of the effects.
   * @param origin_url            : Origin serving the QUICR Session    
   * @param use_reliable_transport: Reliable or Unreliable transport 
   * @param auth_token            : Auth Token to validate the Subscribe Request
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
  void subscribe(const QUICRNamespace& quicr_namespace,
                 const SubscribeIntent& intent,
                 const std::string& origin_url,
                 bool use_reliable_transport,
                 const std::string& auth_token,
                 bytes&& payload,
                 QUICRContext& context_id);

  
   /*
   * @brief Stop subscription on the given QUICR namespace
   * 
   * @param quicr_namespace        : Identifies QUICR namespace
   * @param origin_url            : Origin serving the QUICR Session    
   * @param auth_token            : Auth Token to valiadate the Subscribe Request
   */
  void unsubscribe(const QUICRContext& context_id,
                   const QUICRNamespace& quicr_namespace,
                   const std::string& origin_url, 
                   const std::string& auth_token);



  void publishNamedObject(const QUICRContext& context_id,
                          const QUICRName& name,
                          uint8_t priority,
                          uint64_t best_before,
                          bool use_reliable_transport,
                          bytes&& data) {}
};

}