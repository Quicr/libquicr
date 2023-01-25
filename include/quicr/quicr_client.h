#pragma once
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <quicr/message_buffer.h>
#include <quicr/quicr_common.h>
#include <transport/transport.h>

using qtransport::ITransport;

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
  virtual void onSubscribeResponse(
    const QUICRNamespace& quicr_namespace,
    const SubscribeResult::SubscribeStatus& result) = 0;

  /**
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

  /**
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

  /**
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

/**
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
};

/**
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

  /**
   * @brief Setup a QUICR Client with publisher and subscriber functionality
   *
   * @param relayInfo        : Relay Information to be used by the transport
   * operations
   */
  QuicRClient(RelayInfo& relayInfo, qtransport::LogHandler& logger);

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
   * @brief Publish intent to publish on a QUICR Namespace
   *
   * @param pub_delegate          : Publisher delegate reference
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param origin_url            : Origin serving the QUICR Session
   * @param auth_token            : Auth Token to validate the Subscribe Request
   * @param payload               : Opaque payload to be forwarded to the Origin
   */
  bool publishIntent(std::shared_ptr<PublisherDelegate> pub_delegate,
                     const QUICRNamespace& quicr_namespace,
                     const std::string& origin_url,
                     const std::string& auth_token,
                     bytes&& payload);

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
  void publishIntentEnd(const QUICRNamespace& quicr_namespace,
                        const std::string& auth_token);

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
  void subscribe(std::shared_ptr<SubscriberDelegate> subscriber_delegate,
                 const QUICRNamespace& quicr_namespace,
                 const SubscribeIntent& intent,
                 const std::string& origin_url,
                 bool use_reliable_transport,
                 const std::string& auth_token,
                 bytes&& e2e_token);

  /**
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
  void publishNamedObject(const QUICRName& quicr_name,
                          uint8_t priority,
                          uint16_t expiry_age_ms,
                          bool use_reliable_transport,
                          bytes&& data);

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
  void publishNamedObjectFragment(const QUICRName& quicr_name,
                                  uint8_t priority,
                                  uint16_t expiry_age_ms,
                                  bool use_reliable_transport,
                                  const uint64_t& offset,
                                  bool is_last_fragment,
                                  bytes&& data);

  void handle(messages::MessageBuffer&& msg);

  std::shared_ptr<ITransport> transport;

private:
  void make_transport(RelayInfo& relay_info, qtransport::LogHandler& logger);

  // State to store per-subscribe context
  struct SubscribeContext
  {
    enum struct State
    {
      Unknown = 0,
      Pending,
      Ready
    };

    State state{ State::Unknown };
    qtransport::TransportContextId transport_context_id{ 0 };
    qtransport::MediaStreamId media_stream_id{ 0 };
    uint64_t transaction_id{ 0 };
  };

  // State per publish_intent and related publish
  struct PublishContext
  {
    enum struct State
    {
      Unknown = 0,
      Pending,
      Ready
    };

    State state{ State::Unknown };
    qtransport::TransportContextId transport_context_id{ 0 };
    qtransport::MediaStreamId media_stream_id{ 0 };
    uint64_t group_id{ 0 };
    uint64_t object_id{ 0 };
    uint64_t offset{ 0 };
  };

  ClientStatus client_status{ ClientStatus::TERMINATED };
  qtransport::TransportContextId transport_context_id;
  std::map<QUICRNamespace, std::shared_ptr<SubscriberDelegate>> sub_delegates;
  std::map<QUICRName, std::shared_ptr<SubscriberDelegate>> sub_name_delegates;

  std::map<QUICRNamespace, std::shared_ptr<PublisherDelegate>> pub_delegates;
  std::map<QUICRNamespace, SubscribeContext> subscribe_state{};
  std::map<QUICRName, PublishContext> publish_state{};
  std::unique_ptr<ITransport::TransportDelegate> transport_delegate;
};

}
