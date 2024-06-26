#pragma once

#include <qname>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace quicr {

/**
 * Max transport data size, not counting IP + UDP + QUIC + QUICR headers.
 *    This is the maximum size of the payload data. IPv4 + UDP is 28 bytes,
 *    QUIC adds around 25 bytes and QUICR adds 66 bytes. Assuming a 1400 MTU
 *    end-to-end, 1400 - 119 = 1281.  A max data size of 1280 should be good
 *    end-to-end for all paths.
 */
constexpr uint16_t max_transport_data_size = 1200;

using bytes = std::vector<uint8_t>;

/**
 * Context information managed by the underlying QUICR Stack
 * Applications get the QUICRContextId and pass same for
 * as part of the API operations.
 */
using QUICRContext = uint64_t;

namespace messages {
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
 * @brief Transport Mode
 * @details Defines how the transport should be used for publish intent and subscribe
 */
 enum class TransportMode : uint8_t {
   ReliablePerTrack = 0,      /// Reliable transport using per track (namespace) streams
   ReliablePerGroup,          /// Reliable transport using per group streams
   ReliablePerObject,         /// Reliable transport using per object streams
   Unreliable,                /// Unreliable transport (datagram)
   UsePublisher,              /// Only for subscribe transport mode, follow the mode the publisher is using
   Pause,                     /// Instruct relay to pause sending objects for the subscription
   Resume,                   /// Instruct relay to resume/clear pause state and to start sending objects for subscription
 };

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
  std::string relay_id; // ID for the relay
};

/**
 * SubscribeResult defines the result of a subscription request
 */
struct SubscribeResult
{
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
    ConnectionClosed,
    UnSubscribed

  };

  SubscribeStatus status; // Subscription status
  std::string reason_string{};
  std::optional<uint64_t> subscriber_expiry_interval{};
  RelayInfo redirectInfo{}; // Set only if status is redirect
};

/**
 * @brief State of the subscription
 *
 * @details Subscription state indicates the state of the subscription. Ready indicates it is
 *    active and ready for use. Pending indicates that the subscription has not been
 *    acknowledged yet. Paused indicates that the client set the subscription to be paused.
 *    Paused can only be set when the state is Ready.  Resume will put the state back to Ready.
 */
enum struct SubscriptionState
{
    Unknown = 0,
    Pending,
    Ready,
    Paused                /// Pause implies the state was ready before pause
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
