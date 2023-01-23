#pragma once

#include <optional>
#include <string>
#include <vector>

namespace quicr {

// TODO: Do we need a different structure or the name
using bytes = std::vector<uint8_t>;

/*
 * Context information managed by the underlying QUICR Stack
 * Applications get the QUICRContextId and pass same for
 * as part of the API operations.
 */
using QUICRContext = uint64_t;

/**
 *  QUICRNamespace identifies set of possible QUICRNames
 *  The mask length captures the length of bits, up to 128 bits, that are
 *  significant. Non-significant bits are ignored. In this sense,
 *  a namespace is like an IPv6 prefix/len
 */
struct QUICRNamespace
{
  uint64_t
    hi; // High ordered bits of the 128bit name Id (on-wire is big-endian)
  uint64_t
    low; // Low ordered bits of the 128bit name Id (on-wire is big-endian)
  size_t mask{
    0
  }; // Number of significant bits (big-endian) of hi + low bits.  0 - 128
};

/**
 * Published media objects are uniquely identifed with QUICRName.
 * The construction and the intepretation of the bits are
 * application specific. QUICR protocol and API must consider
 * these bits as opaque
 */
struct QUICRName
{
  uint64_t hi;
  uint64_t low;
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
    QUIC = 0,
    UDP,
    TLS,
    TCP
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

  enum class SubscribeStatus
  {
    Ok = 0,  // Success
    Expired, // Indicates the subscription is considered expired, anti-replay or
             // otherwise
    Redirect, // Not failed, this request should be reattempted to another relay
              // as indicated
    FailedError, // Failed due to relay error, error will be indicated
    FailedAuthz, // Valid credentials, but not authorized
    TimeOut // Timed out. This happens if failed auth or if there is a failure
            // with the relay
            //   Auth failures are timed out because providing status of failed
            //   auth can be exploited
  };

  SubscribeStatus status; // Subscription status
  std::string reason_string;
  std::optional<uint64_t> subscriber_expiry_interval;
  RelayInfo redirectInfo; // Set only if status is redirect
};

/**
 * Publish intent and message status
 */
enum class PublishStatus
{
  Ok = 0,      // Success
  Redirect,    // Indicates the publish (intent or msg) should be reattempted to
               // another relay
  FailedError, // Failed due to relay error, error will be indicated
  FailedAuthz, // Valid credentials, but not authorized
  ReAssigned,  // Publish intent is ok, but name/len has been reassigned due to
               // restrictions.
  TimeOut      // Timed out. The relay failed or auth failed.
};

/**
 * PublishIntentResult defines the result of a publish intent
 */
struct PublishIntentResult
{
  PublishStatus status;     // Publish status
  RelayInfo redirectInfo;   // Set only if status is redirect
  QUICRName reassignedName; // Set only if status is ReAssigned
};

/**
 * PublishMsgResult defines the result of a publish message
 */
struct PublishMsgResult
{
  PublishStatus status; // Publish status
};

}