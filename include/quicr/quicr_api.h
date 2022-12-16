#include <optional>
#include <string>
#include <vector>

namespace quicr {

// TODO: Do we need a different structure or the name
using bytes = std::vector<uint8_t>;

///
/// Common Defines
///

/** 
 * QuicRNameId is the published name that identifies a set of subscribers.
 * 
 *   While this value is opaque to the relays, it is used by origins for authorization. 
 *   The name and origin servers are application/deployment specific.  
 *   
 *   The name Id conforms to the following: 
 * 
 *   * Is represented as two 64bit unsigned numbers, for a total of 128 bits
 *   * Little endian is used for in-code but transmission/wire is big endian
 *   * Does not have to be unique (sequence/message number is not required)
 *   * Can be reused, but must be authorized via the publish intent process
 *   * Length of the name defines the length of bits, up to 128 bits, that are
 *     significant. Non-significant bits are ignored. In this sense, a name Id/length
 *     is like an IPv6 prefix/len
 *  
 *   The length of the name id defines the significant bits (big endian). A published
 *    message is always 128 bits in length, which is represented with a length of 128.
 *    Considering a published message is always 128 bits in length, the length is ignored
 *    when publishing. 
 * 
 *   The length is use for both subscription and publish intent requests. Both define a
 *    length that in effect becomes a wildcard for what is being subscribed to or what
 *    is being authorized to publish to. Both subscribe and publish intent
 *    (aka request/authorization) will determine if the length is a valid length for
 *    a given name id. 
 * 
 *   Name Ids should be padded with unset bits (zero value) for intent message. Set bits
 *    longer than the length will be ignored/truncated.
 * 
 *   The format/schema of the encoded bits in the name Id are application specific. The
 *    primary requirement for names are that they must be encoded as big-endian bit values
 *    with a length value that indicates the significant bits that will be used to match
 *    a set of subscribers and publish intent (authorizations) requests.
*/
struct QuicRNameId
{
  uint64_t  hi;         // High ordered bits of the 128bit name Id (on-wire is big-endian)
  uint64_t  low;        // Low ordered bits of the 128bit name Id (on-wire is big-endian)

  uint8_t   length;     // Number of significant bits (big-endian) of hi + low bits.  0 - 128
};


/**
 * SubscribeJoinMode enum defines the join mode for a new or resumed subscription 
 */
enum class SubscribeJoinMode
{
  Immediate = 0,  // Deliver new messages after subscription; no delay
  WaitNextMsg,    // Wait for next complete message; mid stream fragments are not transmitted
  LastX,          // Deliver the last X value number of complete messages and then deliver real-time
  Resume          // Deliver messages based on last delivered for the given session Id; resume after disconnect
                  //   If this is a first seen session, then treat as immediate. If this is for an existing
                  //   session, then resume as far back as the buffer allows, up to where the last message was
                  //   delivered.  This does require some level of state to track last message delivered for a
                  //   given session. 
};


/** 
 * RelayInfo defines the connection information for relays
*/
struct RelayInfo {

  enum class RelayProtocol {
    QUIC = 0,
    UDP,
    TLS,
    TCP
  };

  std::string   relay;     // Relay IP or FQDN being redirected to
  uint16_t      port;      // Relay port to connect to
  RelayProtocol proto;
};

/**
 * SubscribeResult defines the result of a subscription request
 */
struct SubscribeResult {

  enum class SubscribeStatus
  {
      Ok = 0,       // Success 
      Expired,      // Indicates the subscription is considered expired, anti-replay or otherwise
      Redirect,     // Not failed, this request should be reattempted to another relay as indicated
      FailedError,  // Failed due to relay error, error will be indicated
      FailedAuthz,  // Valid credentials, but not authorized
      TimeOut       // Timed out. This happens if failed auth or if there is a failure with the relay
                    //   Auth failures are timed out because providing status of failed auth can be exploited
  };

  SubscribeStatus status;         // Subscription status
  
  RelayInfo       redirectInfo;   // Set only if status is redirect 
};

/**
 * Publish intent and message status
 */
enum class PublishStatus
{
  Ok = 0,         // Success 
  Redirect,       // Indicates the publish (intent or msg) should be reattempted to another relay
  FailedError,    // Failed due to relay error, error will be indicated
  FailedAuthz,    // Valid credentials, but not authorized
  ReAssigned,     // Publish intent is ok, but name/len has been reassigned due to restrictions.
  TimeOut         // Timed out. The relay failed or auth failed. 
};

/**
 * PublishIntentResult defines the result of a publish intent
 */
struct PublishIntentResult
{
  PublishStatus status;         // Publish status
  uint64_t      publishId;      // ID to use when publishing messages

  RelayInfo     redirectInfo;   // Set only if status is redirect
  QuicRNameId   reassignedName; // Set only if status is ReAssigned
};

/**
 * PublishMsgResult defines the result of a publish message
 */
struct PublishMsgResult
{
  PublishStatus status;         // Publish status
};


/*
 * Subscriber delegate callback methods
 * 
 * NOTES:
 * 
 *  Fragments are handled by the library/implementation.  The client implementing
 *   this API always receives complete messages.  TTL of a message is defined by
 *   the smallest fragment TTL.
 * 
 *  TTL (aka best before) is handled by the library on received messages. TTL is
 *   set by the publisher.
 */
class SubscriberDelegate
{

  virtual ~SubscriberDelegate() = default;

  /**
   * @brief Callback for subscription response
   * 
   * @details This callback will be called when a subscription response
   *          is received, on error, or timeout. 
   * 
   * @param[in]  name    Name Id of the subscribe request
   * @param[in]  result  Subscription result of the subscribe request
   */
  virtual void on_subscribe_response(const QuicRNameId& name,
                                     const SubscribeResult& result) = 0;

  /**
   * @brief Callback when subscription has closed/finished.
   * 
   * @param[in] name    Name Id of the subscribe request
   */
  virtual void on_subscribe_close(const QuicRNameId& name) = 0;

  /**
   * @brief Callback on received message
   * 
   * @details Called when messages are received. Messages can be buffered and
   *   deduplicated using the publisher Id and sequence Id.  Sequence Ids
   *   increment serially one at a time by publisher Id.  Publisher Id is an
   *   ephemeral unique number for a given time period.  
   * 
   * @param[in] name      Name Id of the published message; len will be 128. 
   * @param[in] priority  Priority value for the message
   * @param[in] publishId The publisher Id of the message
   * @param[in] seqId     Message sequence Id. This is relative per publisher Id
   * @param[in] data      Data of the message received
   */
  virtual void on_msg_recv(const QuicRNameId& name,
                           uint8_t            priority,
                           uint64_t           publishId,
                           uint32_t           seqId,
                           bytes&&            data) = 0;
};

/*
 *  Publisher delegate callback methods
 *
 * NOTES:
 *  Published messages are always complete messages. Fragmenting is handled by the library and
 *   transport implementation. Messages greater than MTU will automatically be split
 *   and fragmented via the pub/sub infrastructure.  
 */
class PublisherDelegate
{
  virtual ~PublisherDelegate() = default;

  /**
   * @brief Callback for published message ACK
   * 
   * @todo Support grouping of ACKs so that 
   *
   * @param[in] name      Published name ID being acknowledged. This is always 128 bit in length 
   * @param[in] publishId The publisher Id of the message
   * @param[in] seqId     Message sequence Id. This is relative per publisher Id
   * @param[in] result    Result of the publish operation
   */
  virtual void on_publish_ack(const QuicRNameId&  name,
                              uint64_t            publishId,
                              const uint32_t      seqId,
                              PublishMsgResult&   result) = 0;

  /**
   * @brief Callback for published intent response
   * 
   * @param[in] name      Original published name ID/len of publish intent
   * @param[in] result    Result of the publish operation
   */
  virtual void on_publish_intent_response(const QuicRNameId&   name,
                                          PublishIntentResult& result) = 0;

};

/*
 * Client API for using QUICR Protocol
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


  /**
   * @brief Construct a new QuicR Client
   * 
   * @details A new client thread will be started with an event loop
   *   running to process received messages. Subscriber and publisher
   *   delegate callbacks will be called on received messages. 
   *   The relay will be connected and maintained by the event loop.
   * 
   * @param relay                Relay connection information
   * @param subscriber_delegate  Subscriber delegate
   * @param pub_delegate         Publisher delegate
   */
  QuicRClient(const RelayInfo relay,
              SubscriberDelegate& subscriber_delegate,
              PublisherDelegate& pub_delegate);

  // Recvonly client
  QuicRClient(const RelayInfo relay,
              SubscriberDelegate& subscriber_delegate);

  // Sendonly client
  QuicRClient(const RelayInfo relay,
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
   * @brief Send Publish Intent
   * 
   * @details This method is asynchronous. The publisher delegate intent
   *    response method will be called to indicate intent status.
   * 
   * @details Express interest to publish media under a given QuicrName
   *    auth_token is used to validate the authorization for the
   *    name specified.
   *
   * @note (0): Intent to publish is typically done at a higher level
   *            grouping than individual objects.
   *            ex: user1/ or user1/cam1 or user1/space3/
   *            This ties authz to prefix/group rather than individual
   *            data objects.
   *
   * @note (1): Authorization Token shall embed the information
   *            needed for the authorizing entity to bind the name
   *            to the token.
   *
   * @todo Support array of names
   * 
   * @param name          Name ID/Len to request publish rights
   * @param use_reliable  Request reliable transport for published messages
   * @param auth_token    Authentication token for the origin
   * 
   * @return True if successful, false if not.  True only indicates
   *    that the message was sent to the relay. It does not indicate if it was
   *    accepted and authorized by the origin.  Publisher delegate
   *    is used for that. 
   */
  bool publish_intent(const QuicRNameId& name,
                      bool use_reliable,
                      const std::string& auth_token);



  /**
   * @brief Publish a message
   * 
   * @param name        Name ID to publish a message to. Length is 128
   * @param priority    Priority value for the message
   * @param publishId   The publisher Id from publish intent result
   * @param data        Data of the message to send. This can be up to max
   *                    message size. If the message is larger than MTU it will
   *                    be fragmented.
   * 
   * @return True if successful, false if not.  True only indicates
   *    that the message was sent to the relay. Publisher delegate
   *    is used to confirm ack/response from relay.
   */
  bool publish(const QuicRNameId& name,
               uint8_t            priority,
               uint64_t           publishId,
               uint32_t           seqId,
               const bytes&       data);


  /**
   * @brief Stop publish intent
   * 
   * @param name          Name ID/Len to request publish rights
   * @param[in] publishId The publisher Id of the message
   * @param auth_token    Authentication token for the origin
   */
  void publish_intent_end(const QuicRNameId& name,
                          uint64_t publishId,
                          const std::string& auth_token);

  /**
   * @brief Subscribe to given QuicRName/len
   * 
   * @param name          Name ID/Len to subscribe
   * @param intent        Join mode on start of subscription
   * @param use_reliable  Request reliable transport for published messages
   * @param auth_token    Authentication token for the origin
   * 
   * @return True if successful, false if not.  True only indicates
   *    that the message was sent to the relay. It does not indicate if it was
   *    accepted and authorized.  Subscriber delegate is used for that. 
   */
  bool subscribe(const QuicRNameId& name,
                 SubscribeJoinMode& joinMode,
                 bool use_reliable,
                 const std::string& auth_token);


  /**
   * @brief Unsubscribe to given QuicRName/len
   * 
   * @param name          Name ID/Len to unsubscribe. Must match the subscription
   * @param auth_token    Authentication token for the origin
   */
  void unsubscribe(const QuicRNameId& name, const std::string& auth_token);


};



/****************************************************************************
 * Revisit the below. 
 */

// Server Delegate QUICR protocol callback
class ServerDelegate
{

  virtual ~ServerDelegate() = default;

  /*
   * Callback arrival of published QuicR msg for given Name.
   *
   * Priority - Identifies the relative priority of the current object
   * BestBefore - TTL for the object to be useful for the application
   * Use Reliable Transport - Indicates the preference for the object's
   *                          transport, if forwarded.
   * Note: Both the on_publish_object and on_publish_object_fragment
   * callbacks will be called. The delegate implementation
   * shall decide the right callback for their usage.
   */
  virtual void cb_published_object(const QuicRNameId& name,
                                   uint8_t priority,
                                   uint64_t best_before,
                                   bool use_reliable_transport,
                                   bytes&& data) = 0;

  /*
   * Report arrival of published QuicR object fragment under a Name
   *
   * Priority - Identifies the relative priority of the current object
   * BestBefore - TTL for the object to be useful for the application
   * Use Reliable Transport - Indicates the preference for the object's
   *                          transport, if forwarded.
   * Fragment Number - Current Fragment Identifier
   * Num Total Fragments identifies current object's fragment count

   * Note: Both the on_publish_object and on_publish_object_fragment
   * callbacks will be called. The delegate implementation
   * shall decide the right callback for their usage.
  */
  virtual void on_published_fragment(const QuicRNameId& name,
                                     uint8_t priority,
                                     uint64_t best_before,
                                     bool use_reliable_transport,
                                     const uint16_t fragment_number,
                                     uint16_t num_total_fragments,
                                     bytes&& data) = 0;

  /*
   * Report arrival of subscribe request
   */
  virtual void on_subscribe(const QuicRNameId& name,
                            SubscribeIntent& intent,
                            bool use_reliable_transport,
                            const std::string& auth_token) = 0;
};

class QuicRServer
{

  // TODO: Need to have connection/stream level info

  QuicRServer(const uint16_t port, ServerDelegate& delegate) explicit;

  // Transport APIs
  bool is_transport_ready();

  /*
   * Send the result of processing the PublishIntent
   * TODO: add some form of transaction_id
   */
  void publish_intent_ok(const QuicRNameIdRange& name,
                         const PublishResult& result);

  /*
   * Send the result of processing the Subscribe Request
   * TODO: add some form of transaction_id
   */
  void subscribe_ok(const QuicRNameIdRange& name, const SusbcribeResult& resut)

    /*
     * Report that a subscription ended for the name range
     */

    void subscribe_end(const QuicRNameIdRange& name,
                       const SubscribeResult& result);
  ;

  /*
   * Send named object ot the subscriber clients interested in the given name
   *
   * Priority - Identifies the relative priority of the current object
   * BestBefore - TTL for the object to be useful for the application
   * Use Reliable Transport - Indicates the preference for the object's
   *                          transport, if forwarded.
   * Fragment Number - Current Fragment Identifier
   * Num Total Fragments identifies current object's fragment count
   */
  void send_named_object(const QuicrNameID& name,
                         uint8_t priority,
                         uint64_t best_before,
                         bool use_reliable_transport,
                         bytes&& data);

  /*
   * Send named object fragments ot the subscriber clients interested in the
   given name

   * Priority - Identifies the relative priority of the current object
   * BestBefore - TTL for the object to be useful for the application
   * Use Reliable Transport - Indicates the preference for the object's
   *                          transport, if forwarded.
   * Fragment Number - Current Fragment Identifier
   * Num Total Fragments identifies current object's fragment count
   */
  void send_named_fragment(const QuicrNameID& name,
                           uint8_t priority,
                           uint64_t best_before,
                           bool use_reliable_transport,
                           uint16_t fragment_number,
                           uint16_t num_total_fragments,
                           bytes&& data);
};

}