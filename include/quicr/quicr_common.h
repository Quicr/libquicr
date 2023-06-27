#pragma once
#include <optional>
#include <string>
#include <vector>

#include <quicr_name>

namespace quicr {

/**
 * Max transport data size, not counting IP + UDP + QUIC + QUICR headers.
 *    This is the maximum size of the payload data. IPv4 + UDP is 28 bytes,
 *    QUIC adds around 25 bytes and QUICR adds 66 bytes. Assuming a 1400 MTU
 *    end-to-end, 1400 - 119 = 1281.  A max data size of 1280 should be good
 *    end-to-end for all paths.
 */
constexpr uint16_t MAX_TRANSPORT_DATA_SIZE = 1200;

// TODO: Do we need a different structure or the name
using bytes = std::vector<uint8_t>;

/**
 * Context information managed by the underlying QUICR Stack
 * Applications get the QUICRContextId and pass same for
 * as part of the API operations.
 */
using QUICRContext = uint64_t;

namespace messages {
/**
 * Type of message being sent/received
 */
enum class MessageType : uint8_t
{
  Unknown,
  Subscribe,
  SubscribeResponse,
  SubscribeEnd,
  Unsubscribe,
  Publish,
  PublishIntent,
  PublishIntentResponse,
  PublishIntentEnd,
  Fetch,
};

/**
 * Indicates the type of media being sent.
 */
enum class MediaType : uint8_t
{
  Manifest,
  Advertisement,
  Text,
  RealtimeMedia
};

enum class Response : uint8_t
{
  Ok,
  Expired,
  Fail,
  Redirect
};
}

/**
 * Hint providing the start point to serve a subscrption request.
 * Relays use this information to determine the start-point and
 * serve the objects in the time-order from the cache.
 */
enum class SubscribeIntent
{
  immediate = 0, // Start from the most recent object
  wait_up = 1,   // Start from the following group
  sync_up = 2,   // Start from the request position
};

/**
 * RelayInfo defines the connection information for relays
 */
struct RelayInfo
{
  enum class Protocol
  {
    UDP = 0,
    QUIC
  };

  std::string hostname; // Relay IP or FQDN
  uint16_t port;        // Relay port to connect to
  Protocol proto;       // Transport protocol to use
};

/**
 * SubscribeResult defines the result of a subscription request
 */
struct SubscribeResult
{
  // TODO: Should be replaced with messages::Response
  enum class SubscribeStatus
  {
    Ok = 0,  // Success
    Expired, // Indicates the subscription is considered expired, anti-replay or
             // otherwise
    Redirect, // Not failed, this request should be reattempted to another relay
              // as indicated
    FailedError, // Failed due to relay error, error will be indicated
    FailedAuthz, // Valid credentials, but not authorized
    TimeOut, // Timed out. This happens if failed auth or if there is a failure
            // with the relay
            //   Auth failures are timed out because providing status of failed
            //   auth can be exploited
    ConnectionClosed

  };

  SubscribeStatus status; // Subscription status
  std::string reason_string{};
  std::optional<uint64_t> subscriber_expiry_interval{};
  RelayInfo redirectInfo{}; // Set only if status is redirect
};

/**
 * PublishIntentResult defines the result of a publish intent
 */
struct PublishIntentResult
{
  messages::Response status;  // Publish status
  RelayInfo redirectInfo;     // Set only if status is redirect
  quicr::Name reassignedName; // Set only if status is ReAssigned
};

}
