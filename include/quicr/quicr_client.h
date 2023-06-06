#pragma once

#include "encode.h"
#include "message_buffer.h"
#include "quicr_client_common.h"
#include "quicr_client_delegate.h"
#include "quicr_client_session.h"
#include "quicr_common.h"

#include <quicr_name>

#include <map>
#include <memory>
#include <string>
#include <vector>

/**
 * Forward declarations
 */
namespace qtransport {
class ITransport;
class LogHandler;
struct TransportConfig;
}

namespace quicr {

/**
 * @brief Creation method for creating QuicR client sessions.
 *
 * @param relay_info  : The information for relay connection and peering.
 * @param tconfig     : The configuration to use for connecting.
 * @param logger      : The logger object to log to.
 * @param stream_mode : The mode for creating streams.
 *
 * @returns The appropriate client session to use.
 */
std::unique_ptr<QuicRClientSession>
make_client_session(RelayInfo& relay_info,
                    qtransport::TransportConfig tconfig,
                    qtransport::LogHandler& logger,
                    StreamMode stream_mode);

// Exception that may be thrown if there is a critical error
class QuicRClientException : public std::runtime_error
{
  using std::runtime_error::runtime_error;
};

/**
 *   Client API for using QUICR Protocol
 */
class QuicRClient
{
public:
  /**
   * @brief Setup a QUICR Client with publisher and subscriber functionality
   *
   * @param relayInfo : Relay Information to be used by the transport
   * @param tconfig   : Transport configuration
   * @param logger    : Log handler, used by transport and API for
   *                    loggings operations
   */
  QuicRClient(RelayInfo& relayInfo,
              qtransport::TransportConfig tconfig,
              qtransport::LogHandler& logger,
              StreamMode stream_mode = quicr::StreamMode::Datagram);

  /**
   * @brief Setup a QUICR Client Session with publisher and subscriber
   *        functionality.
   *
   * @param transport : External transport pointer to use.
   * @param logger    : Log handler, used by transport and API for loggings
   *                    operations
   */
  QuicRClient(std::shared_ptr<qtransport::ITransport> transport,
              qtransport::LogHandler& logger);

  /**
   * @brief Destructor for the client
   */
  ~QuicRClient() = default;

  /**
   * @brief Get the client status
   *
   * @details This method should be used to determine if the client is
   *   connected and ready for publishing and subscribing to messages.
   *   Status will indicate the type of error if not ready.
   *
   * @returns client status
   */
  ClientStatus status() const { return client_session->status(); }

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
   * @brief Publish intent to publish on a QUICR Namespace
   *
   * @param pub_delegate          : Publisher delegate reference
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param origin_url            : Origin serving the QUICR Session
   * @param auth_token            : Auth Token to validate the Subscribe Request
   * @param payload               : Opaque payload to be forwarded to the Origin
   */
  bool publishIntent(std::shared_ptr<PublisherDelegate> pub_delegate,
                     const quicr::Namespace& quicr_namespace,
                     const std::string& origin_url,
                     const std::string& auth_token,
                     bytes&& payload);

  /**
   * @brief Stop publishing on the given QUICR namespace
   *
   * @param quicr_namespace : Identifies QUICR namespace
   * @param origin_url      : Origin serving the QUICR Session
   * @param auth_token      : Auth Token to valiadate the Subscribe Request
   * @param payload         : Opaque payload to be forwarded to the Origin
   */
  void publishIntentEnd(const quicr::Namespace& quicr_namespace,
                        const std::string& auth_token);

  /**
   * @brief Perform subscription operation a given QUICR namespace
   *
   * @param subscriber_delegate   : Reference to receive callback for subscriber
   *                                ooperations
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param subscribe_intent      : Subscribe intent to determine the start
   *                                point for serving the matched objects. The
   *                                application may choose a different intent
   *                                mode, but must be aware of the effects.
   * @param origin_url            : Origin serving the QUICR Session
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
                 const quicr::Namespace& quicr_namespace,
                 const SubscribeIntent& intent,
                 const std::string& origin_url,
                 bool use_reliable_transport,
                 const std::string& auth_token,
                 bytes&& e2e_token);

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
   * @brief Publish Named object
   *
   * @param quicr_name               : Identifies the QUICR Name for the object
   * @param priority                 : Identifies the relative priority of the
   *                                   current object
   * @param expiry_age_ms            : Time hint for the object to be in cache
   *                                      before being purged after reception
   * @param use_reliable_transport   : Indicates the preference for the object's
   * @param data                     : Opaque payload
   *
   */
  void publishNamedObject(const quicr::Name& quicr_name,
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
   *                                   before being purged after reception
   * @param use_reliable_transport   : Indicates the preference for the object's
   * @param offset                   : Current fragment offset
   * @param is_last_fragment         : Indicates if the current fragment is the
   * @param data                     : Opaque payload of the fragment
   */
  void publishNamedObjectFragment(const quicr::Name& quicr_name,
                                  uint8_t priority,
                                  uint16_t expiry_age_ms,
                                  bool use_reliable_transport,
                                  const uint64_t& offset,
                                  bool is_last_fragment,
                                  bytes&& data);

protected:
  std::unique_ptr<QuicRClientSession> client_session;
};

}
