#pragma once

#include "encode.h"
#include "message_buffer.h"
#include "quicr_client_delegate.h"
#include "quicr_client_session.h"
#include "quicr_common.h"

#include <transport/transport.h>
#include <cantina/logger.h>
#include <qname>

#include <map>
#include <memory>
#include <string>
#include <vector>

/**
 * Forward declarations
 */
namespace qtransport {
class ITransport;
struct TransportConfig;
}

namespace quicr {

// Exception that may be thrown if there is a critical error
class ClientException : public std::runtime_error
{
  using std::runtime_error::runtime_error;
};

/**
 *   Client API for using QUICR Protocol
 */
class Client
{
public:
  /**
   * @brief Setup a QUICR Client with publisher and subscriber functionality
   *
   * @param relay_info  : Relay Information to be used by the transport
   * @param endpoint_id : Client endpoint ID (e.g., email)
   * @param chunk_size  : Size in bytes to chunk messages if greater than this size
   *                      Zero disables, value is max(chunk_size, max_transport_data_size)
   * @param tconfig     : Transport configuration
   * @param logger      : Shared pointer to cantina::Logger object
   *                      loggings operations
   */
  Client(const RelayInfo& relay_info,
         const std::string& endpoint_id,
         size_t chunk_size,
         const qtransport::TransportConfig& tconfig,
         const cantina::LoggerPointer& logger,
         std::optional<quicr::Namespace> metrics_ns = std::nullopt);

  /**
   * @brief Setup a QUICR Client Session with publisher and subscriber
   *        functionality.
   *
   * @param transport : External transport pointer to use.
   * @param logger    : Shared pointer to cantina::Logger object
   */
  Client(std::shared_ptr<qtransport::ITransport> transport,
         const cantina::LoggerPointer& logger);

  /**
   * @brief Destructor for the client
   */
  ~Client() = default;

  /**
   * @brief Connects the session using the info provided on construction.
   * @returns True if connected, false otherwise.
   */
  bool connect();

  /**
   * @brief Disconnects the session from the relay.
   * @returns True if successful, false if some error occurred.
   */
  bool disconnect();

  /**
   * @brief Checks if the session is connected.
   * @returns True if transport has started and connection has been made. False
   *          otherwise.
   */
  bool connected() const;

  /**
   * @brief Publish intent to publish on a QUICR Namespace
   *
   * @param pub_delegate            : Publisher delegate reference
   * @param quicr_namespace         : Identifies QUICR namespace
   * @param origin_url              : Origin serving the QUICR Session
   * @param auth_token              : Auth Token to validate Subscribe Requests
   * @param payload                 : Opaque payload to be forwarded to Origin
   * @param transport_mode          : Transport mode to use for publishing objects
   *                                  published objects
   * @param priority                : Identifies the relative priority for the data flow when reliable
   *
   */
  bool publishIntent(std::shared_ptr<PublisherDelegate> pub_delegate,
                     const quicr::Namespace& quicr_namespace,
                     const std::string& origin_url,
                     const std::string& auth_token,
                     bytes&& payload,
                     const TransportMode transport_mode,
                     const uint8_t priority = 1);

  /**
   * @brief Stop publishing on the given QUICR namespace
   *
   * @param quicr_namespace        : Identifies QUICR namespace
   * @param auth_token             : Auth Token to validate Subscribe Requests
   */
  void publishIntentEnd(const quicr::Namespace& quicr_namespace,
                        const std::string& auth_token);

  /**
   * @brief Perform subscription operation a given QUICR namespace
   *
   * @param subscriber_delegate     : Reference to receive callback for
   *                                  subscriber operations
   * @param quicr_namespace         : Identifies QUICR namespace
   * @param intent                  : Subscribe intent to determine the start
   *                                  point for serving the matched objects. The
   *                                  application may choose a different intent
   *                                  mode, but must be aware of the effects.
   * @param transport_mode          : Transport mode to use for received subscribed objects
   * @param origin_url              : Origin serving the QUICR Session
   * @param auth_token              : Auth Token to validate Subscribe Requests
   * @param e2e_token               : Opaque token to be forwarded to the Origin
   * @param priority                : Relative priority for the data flow when reliable
   *
   * @details Entities processing the Subscribe Request MUST validate the
   *          request against the token, verify if the Origin specified in the
   *          origin_url is trusted and forward the request to the next hop
   *          Relay for that Origin or to the Origin (if it is the next hop)
   *          unless the entity itself the Origin server. It is expected for the
   *          Relays to store the subscriber state mapping the subscribe
   *          context, namespaces and other relation information.
   */
  void subscribe(std::shared_ptr<SubscriberDelegate> subscriber_delegate,
                 const quicr::Namespace& quicr_namespace,
                 const SubscribeIntent& intent,
                 const TransportMode transport_mode,
                 const std::string& origin_url,
                 const std::string& auth_token,
                 bytes&& e2e_token,
                 uint8_t priority = 1);

  /**
   * @brief Stop subscription on the given QUICR namespace
   *
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param origin_url            : Origin serving the QUICR Session
   * @param auth_token            : Auth Token to validate the Subscribe
   *                                Request
   */
  void unsubscribe(const quicr::Namespace& quicr_namespace,
                   const std::string& origin_url,
                   const std::string& auth_token);

  /**
   * @brief Get subscription state
   */
  SubscriptionState getSubscriptionState(const quicr::Namespace& quicr_namespace);

  /**
   * @brief Publish Named object
   *
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param priority                 : Identifies the relative priority of the
   *                                   current object
   * @param expiry_age_ms            : Time hint for the object to be in cache
   *                                   before being purged after reception
   * @param data                     : Opaque payload
   * @param trace                    : Method trace vector
   *
   */
  void publishNamedObject(const quicr::Name& quicr_name,
                          uint8_t priority,
                          uint16_t expiry_age_ms,
                          bytes&& data,
                          std::vector<qtransport::MethodTraceItem> &&trace);

  /**
   * @brief Publish Named object
   *
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param priority                 : Identifies the relative priority of the
   *                                   current object
   * @param expiry_age_ms            : Time hint for the object to be in cache
                                       before being purged after reception
   * @param offset                   : Current fragment offset
   * @param is_last_fragment         : Indicates if the current fragment is the
   * @param data                     : Opaque payload of the fragment
   */
  void publishNamedObjectFragment(const quicr::Name& quicr_name,
                                  uint8_t priority,
                                  uint16_t expiry_age_ms,
                                  const uint64_t& offset,
                                  bool is_last_fragment,
                                  bytes&& data);

  void publishMeasurement(const Measurement& measurement);

protected:
  std::unique_ptr<ClientSession> client_session;
};
}
