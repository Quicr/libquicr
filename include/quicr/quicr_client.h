#include <memory>
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
public:
  virtual ~SubscriberDelegate() = default;

  /**
   * @brief Callback for subscription response
   *
   * @param quicr_namespace: QUICR Namespace associated with the Subscribe
   * Request
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
                                   const SubscribeResult& result) = 0;

  /*
   * @brief Report arrival of subscribed QUICR object under a Name
   *
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param priority                 : Identifies the relative priority of the
   *                                   current object
   * @param expiry_age_ms            : Time hint for the object to be in cache
   *                                      before being purged after reception
   * @param use_reliable_transport   : Indicates the preference for the object's
   *                                   transport, if forwarded.
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
  virtual void onSubscribedObject(const QUICRName& quicr_name,
                                  uint8_t priority,
                                  uint16_t expiry_age_ms,
                                  bool use_reliable_transport,
                                  bytes&& data)
  {
  }

  /*
   * @brief Report arrival of subscribed QUICR object fragment under a Name
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
   * @param data                     : Opaque payload of the fragment
   *
   *
   *  @note: It is important that the implementations not perform
   *         compute intensive tasks in this callback, but rather
   *         copy/move the needed information and hand back the control
   *         to the stack
   */
  virtual void onSubscribedObjectFragment(const QUICRName& quicr_name,
                                          uint8_t priority,
                                          uint16_t expiry_age_ms,
                                          bool use_reliable_transport,
                                          const uint64_t& offset,
                                          bool is_last_fragment,
                                          bytes&& data)
  {
  }
};

/*
 *  Publisher common delegate callback operations
 */
class PublisherDelegate
{
public:
  virtual ~PublisherDelegate() = default;

  /*
   * @brief Callback on the response to the  publish intent
   *
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param result                : Status of Publish Intetn
   *
   * @details Entities processing the Subscribe Request MUST validate the
   * request
   * @todo: Add payload with origin signed blob
   */
  virtual void onPublishIntentResponse(const QUICRNamespace& quicr_namespace,
                                       const PublishIntentResult& result) = 0;

  /*
   * @brief Reports result of fragment published generated
   *        by the local stack.
   *
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param offset                   : Current fragment offset
   * @param is_last_fragment         : Indicates if the current fragment is the
   *                                   last fragment
   * @param result                   : Result of the publish operation
   */
  virtual void onPublishFragmentResult(const QUICRName& quicr_name,
                                       const uint64_t& offset,
                                       bool is_last_fragment,
                                       const PublishMsgResult& result)
  {
  }

  /*
   * @brief Reports result of obejct published under the Name,
   *        by the local stack
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param result                   : Result of the publish operation
   *
   */
  virtual void onPublishObjectResult(const QUICRName& quicr_name,
                                     const PublishMsgResult& result)
  {
  }
};

/*
 *   Client API for using QUICR Protocol
 */
class QuicRClient
{
public:
  enum class ClientStatus
  {
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
   * @param transport            : QuicRTransport class implementation
   * @param subscriber_delegate  : Reference to receive callback for subscriber
   * operations
   * @param publisher_delegate   : Reference to receive callback for publisher
   * operations
   */
  QuicRClient(ITransport& transport,
              std::shared_ptr<SubscriberDelegate> subscriber_delegate,
              std::shared_ptr<PublisherDelegate> pub_delegate);

  /*
   * @brief Setup a QUICR Client with subscriber functionality
   *
   * @param transport            : QuicRTransport class implementation
   * @param subscriber_delegate  : Reference to receive callback for subscriber
   * operations
   */
  QuicRClient(ITransport& transport,
              std::shared_ptr<SubscriberDelegate> subscriber_delegate);

  /*
   * @brief Setup a QUICR Client with publisher functionality
   *
   * @param transport            : QuicRTransport class implementation
   * @param publisher_delegate   : Reference to receive callback for publisher
   * operations
   */
  QuicRClient(ITransport& transport,
              std::shared_ptr<PublisherDelegate> pub_delegate);

  /**
   * @brief Get the client status
   *
   * @details This method should be used to determine if the client is
   *   connected and ready for publishing and subscribing to messages.
   *   Status will indicate the type of error if not ready.
   *
   * @returns client status
   */
  ClientStatus status() const { return client_status; }

  /**
   * @brief Run client API event loop
   *
   * @details This method will connect to the relay/transport and run
   *    an event loop for calling the callbacks
   *
   * @returns client status
   */
  ClientStatus run();

  /*
   * @brief Publish intent to publish on a QUICR Namespace
   *
   * @param quicr_namespace        : Identifies QUICR namespace
   * @param origin_url            : Origin serving the QUICR Session
   * @param auth_token            : Auth Token to validate the Subscribe Request
   * @param payload               : Opaque payload to be forwarded to the Origin
   */
  bool publishIntent(const QUICRNamespace& quicr_namespace,
                     const std::string& origin_url,
                     const std::string& auth_token,
                     bytes&& payload);

  /*
   * @brief Stop publishing on the given QUICR namespace
   *
   * @param quicr_namespace        : Identifies QUICR namespace
   * @param origin_url             : Origin serving the QUICR Session
   * @param auth_token             : Auth Token to valiadate the Subscribe
   * Request
   * @param payload                : Opaque payload to be forwarded to the
   * Origin
   */
  void publishIntentEnd(const QUICRNamespace& quicr_namespace,
                        const std::string& auth_token);

  /*
   * @brief Perform subscription operation a given QUICR namespace
   *
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param subscribe_intent      : Subscribe intent to determine the start
   * point for serving the matched objects. The application may choose a
   * different intent mode, but must be aware of the effects.
   * @param origin_url            : Origin serving the QUICR Session
   * @param use_reliable_transport: Reliable or Unreliable transport
   * @param auth_token            : Auth Token to validate the Subscribe Request
   * @parm e2e_token              : Opaque token to be forwarded to the Origin
   *
   * @details Entities processing the Subscribe Request MUST validate the
   * request against the token, verify if the Origin specified in the origin_url
   *          is trusted and forward the request to the next hop Relay for that
   *          Origin or to the Origin (if it is the next hop) unless the entity
   *          itself the Origin server.
   *          It is expected for the Relays to store the subscriber state
   * mapping the subscribe context, namespaces and other relation information.
   */
  void subscribe(const QUICRNamespace& quicr_namespace,
                 const SubscribeIntent& intent,
                 const std::string& origin_url,
                 bool use_reliable_transport,
                 const std::string& auth_token,
                 bytes&& e2e_token);

  /*
   * @brief Stop subscription on the given QUICR namespace
   *
   * @param quicr_namespace        : Identifies QUICR namespace
   * @param origin_url            : Origin serving the QUICR Session
   * @param auth_token            : Auth Token to valiadate the Subscribe
   * Request
   */
  void unsubscribe(const QUICRNamespace& quicr_namespace,
                   const std::string& origin_url,
                   const std::string& auth_token);

  /*
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
  void publishNamedObject(const QUICRName& quicr_name,
                          uint8_t priority,
                          uint16_t expiry_age_ms,
                          bool use_reliable_transport,
                          bytes&& data);

  /*
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
  void publishNamedObjectFragment(const QUICRName& quicr_name,
                                  uint8_t priority,
                                  uint16_t expiry_age_ms,
                                  bool use_reliable_transport,
                                  const uint64_t& offset,
                                  bool is_last_fragment,
                                  bytes&& data);

private:
  ClientStatus client_status{ ClientStatus::TERMINATED };
  ITransport& transport;
  std::shared_ptr<SubscriberDelegate> sub_delegate;
  std::shared_ptr<PublisherDelegate> pub_delegate;
};

}