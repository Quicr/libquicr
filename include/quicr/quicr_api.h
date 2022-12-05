#include <string>
#include <vector>
#include <optional>


namespace quicr {

// TODO: Do we need a different structure or the name
using bytes = std::vector<uint8_t>;

///
/// Common Defines
///

// QuicRName Identifier along with a prefix mask
// Represents a range of names that match the prefix
// TODO: Abstract the representation
struct QuicRNameIdRange
{
  uint64_t[2] name; // obtained out-of-band and app-specific
  size_t mask {0}; // mask of zero implies all 128 bits define the name
};

// QuicR Name for the individual objects being published
// TODO: Abstract the representation
struct QuicRNameId
{
  uint64_t[2] name; // obtained out-of-band and app-specific
};

// Hint providing the start point to serve a subscription
enum class SubscribeIntent {
  immediate = 0, // Start from the most recent object
  wait_up = 1,   // Start from the following group
  sync_up = 2,   // Start from the request position
};


// Result of a subscribe operation
struct SubscribeResult {
  enum class Reason {
    SubscriptionOk = 0,
    SubscriptionCancelled,
    SubscriptionError,
    SubscriptionExpired
  };

  Reason reason_code;
  std::string reason_string;
  std::optional<uint64_t> subscriber_expiry_interval;
};


// Result of the publish intent operation
struct PublishResult {
  enum class Reason {
    PublishIntentOk = 0,
    PublishIntentError
  };

  Reason reason_code;
  std::string reason_string;
};

/*
 *  Subscriber common delegate callback operations
 */

class SubscriberDelegate {

  virtual ~SubscriberDelegate() = default;
  /*
   * Result of Subscribe Operation
   */
  virtual void on_subscribe_response(const QuicRNameIdRange& name,
                                     const SubscribeResult& result) = 0;

  /*
   * Report a given subscription matching the range of names have ended.
   */
  virtual void on_subscribe_ended(const QuicRNameIdRange& name,
                                  const SubscribeResult& reason) = 0;

   /*
     * Report arrival of object subscribed to. The QuicrName must be
     * subset of names subscribed via the QuicRNameIdRange
     *
     * Priority - Identifies the relative priority of the current object
     * BestBefore - TTL for the object to be useful for the application
     * Use Reliable Transport - Indicates the preference for the object's
     *                          transport, if forwarded.
   */
  virtual void on_subscribed_object(const QuicrNameID& name,
                                    uint8_t priority,
                                    uint64_t best_before,
                                    bool use_reliable_transport,
                                    bytes&& data) = 0;

  /*
     * Report arrival of subscribed QuicR object fragment under a Name
     * The QuicrName must be subset of names subscribed via the QuicRNameIdRange
     *
     * Priority - Identifies the relative priority of the current object
     * BestBefore - TTL for the object to be useful for the application
     * Use Reliable Transport - Indicates the preference for the object's
     *                          transport, if forwarded.
     * Fragment Number - Current Fragment Identifier
     * Num Total Fragments identifies current object's fragment count
   */
  virtual void on_subscribed_object_fragment(const QuicrNameID& name,
                                             uint8_t priority,
                                             uint64_t best_before,
                                             bool use_reliable_transport,
                                             const uint16_t fragment_number,
                                             uint16_t num_total_fragments,
                                             bytes&& data) = 0;

};


/*
 *  Publisher common delegate callback operations
 */
class PublisherDelegate {
  virtual ~PublisherDelegate() = default;

  /*
     * Report result of the object fragment publish result
   */
  virtual void on_publish_fragment_result(const QuicrNameID& name,
                                          const uint16_t fragment_number,
                                          uint16_t num_total_fragments,
                                          PublishResult& result) = 0;

  /*
   Report result of the object publish result
  */

  virtual void on_publish_object_result(const QuicrNameID& name,
                                        PublishResult& result) = 0;
};

//
// Client API for using QUICR Protocol
//
class QuicRClient {

  // pub/sub client
  QuicRClient(const std::string& server,
              const uint16_t port,
              SubscriberDelegate& subscriber_delegate,
              PublisherDelegate& pub_delegate);

  // Recvonly client
  QuicRClient(const std::string& server,
              const uint16_t port,
              SubscriberDelegate& subscriber_delegate);


  // Sendonly client
  QuicRClient(const std::string& server,
              const uint16_t port,
              PublisherDelegate& pub_delegate);

  // Transport APIs
  bool is_transport_ready();

  /* Express interest to publish media under a given QuicrName
     * auth_token is used to validate the authorization for the
     * name specified.ok
     *
     * Note (0): Intent to publish is typically done at a higher level
     *           grouping than individual obhjects.
     *           ex: user1/ or user1/cam1 or user1/space3/
     *           This ties authz to prefix/group rather than individial
     *           data objects.
     *
     * Note (1): Authorization Token shall embed the information
     *           needed for the authorizing entity to bind the name
     *           to the token.
     *
     * TBD: Support array of names
   */
  bool publish_intent(const QuicRNameIdRange& name_id,
                      const std::string& auth_token);



  /*
     * Stop publishing under the given name prefix
   */
  void publish_intent_end(const QuicRNameIdRange& name,
                          const std::string& auth_token);

  /*
     *  Subscribe interest to the given QuicrName with appropriate
     *   prefix as defined by the application
   */
  void subscribe(const QuicRNameIdRange& name,
                 SubscribeIntent& intent,
                 bool use_reliable_transport,
                 const std::string& auth_token);

  /*
     * Unsubscribe interest to the given QuicrName
   */
  void unsubscribe(const QuicRNameIdRange& name,
                   const std::string& auth_token);

};


// Server Delegate QUICR protocol callback
class ServerDelegate {

  virtual ~ServerDelegate() = default;

  /*
     * Request interest to publish under given
     * QuicrName. Receiving Quicr entity can authorize
     * the QuicrName if it has the authority to do so or
     * forward the publish intent towards
     * appropriate Quicr entity by republishing the received
     * intent to publish
   */
  virtual void on_publish_intent(const QuicRNameIdRange& name,
                                 const std::string& auth_token);

  /*
     * Report arrival of published QuicR object under a Name.
     *
     * Priority - Identifies the relative priority of the current object
     * BestBefore - TTL for the object to be useful for the application
     * Use Reliable Transport - Indicates the preference for the object's
     *                          transport, if forwarded.
     * Note: Both the on_publish_object and on_publish_object_fragment
     * callbacks will be called. The delegate implementation
     * shall decide the right callback for their usage.
   */
  virtual void on_published_object(const QuicrNameID& name,
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
  virtual void on_published_fragment(const QuicrNameID& name,
                                     uint8_t priority,
                                     uint64_t best_before,
                                     bool use_reliable_transport,
                                     const uint16_t fragment_number,
                                     uint16_t num_total_fragments,
                                     bytes&& data) = 0;


  /*
     * Report arrival of subscribe request
   */
  virtual void on_subscribe(const QuicRNameIdRange& name,
                            SubscribeIntent& intent,
                            bool use_reliable_transport,
                            const std::string& auth_token) = 0;


};


class QuicRServer {

  QuicRServer(const uint16_t port,
              ServerDelegate& delegate) explicit;

  // Transport APIs
  bool is_transport_ready();

  /*
     * Send the result of processing the PublishIntent
     * TODO: add some form of transaction_id
   */
  void publish_intent_ok(const QuicRNameIdRange& name, const PublishResult& result);

  /*
     * Send the result of processing the Subscribe Request
     * TODO: add some form of transaction_id
   */
  void subscribe_ok(const QuicRNameIdRange& name,
                    const SusbcribeResult& resut)

    /*
     * Report that a subscription ended for the name range
     */

    void subscribe_end(const QuicRNameIdRange& name, const SubscribeResult& result);;

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
   * Send named object fragments ot the subscriber clients interested in the given name

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