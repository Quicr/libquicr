/*
 *  quicr_client_raw_session.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 *
 *  Description:
 *      This file defines a session layer between the client APIs and the
 *      transport that uses raw data packets, namely UDP or QUIC.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include "quicr/encode.h"
#include "quicr/message_buffer.h"
#include "quicr/quicr_client.h"
#include "quicr/quicr_client_delegate.h"
#include "quicr/quicr_common.h"

#include <quicr_name>
#include <transport/transport.h>

#include <map>
#include <memory>
#include <string>

namespace quicr {

/**
 *   Client Raw Session Interface
 */
class QuicRClientRawSession : public QuicRClientSession
{
public:
  /**
   * @brief Setup a QUICR Client with publisher and subscriber functionality
   *
   * @param relayInfo        : Relay Information to be used by the transport
   * @param tconfig          : Transport configuration
   * @param logger           : Log handler, used by transport and API for
   *                           loggings operations
   */
  QuicRClientRawSession(RelayInfo& relayInfo,
                        qtransport::TransportConfig tconfig,
                        qtransport::LogHandler& logger);

  /**
   * API for usages in Tests. Applications don't need to be bothered
   * about the transport
   */
  QuicRClientRawSession(std::shared_ptr<qtransport::ITransport> transport);

  /**
   * @brief Destructor for the raw client session object
   */
  ~QuicRClientRawSession();

  /**
   * @brief Get the client status
   *
   * @details This method should be used to determine if the client is
   *   connected and ready for publishing and subscribing to messages.
   *   Status will indicate the type of error if not ready.
   *
   * @returns client status
   */
  ClientStatus status() const override { return client_status; }

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
                     bytes&& payload) override;

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
  void publishIntentEnd(const quicr::Namespace& quicr_namespace,
                        const std::string& auth_token) override;

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
                 const quicr::Namespace& quicr_namespace,
                 const SubscribeIntent& intent,
                 const std::string& origin_url,
                 bool use_reliable_transport,
                 const std::string& auth_token,
                 bytes&& e2e_token) override;

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
                   const std::string& auth_token) override;

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
  void publishNamedObject(const quicr::Name& quicr_name,
                          uint8_t priority,
                          uint16_t expiry_age_ms,
                          bool use_reliable_transport,
                          bytes&& data) override;

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
  void publishNamedObjectFragment(const quicr::Name& quicr_name,
                                  uint8_t priority,
                                  uint16_t expiry_age_ms,
                                  bool use_reliable_transport,
                                  const uint64_t& offset,
                                  bool is_last_fragment,
                                  bytes&& data) override;

  void handle(messages::MessageBuffer&& msg);
  void removeSubscribeState(bool all,
                            const quicr::Namespace& quicr_namespace,
                            const SubscribeResult::SubscribeStatus& reason);

  std::shared_ptr<qtransport::ITransport> transport;
  qtransport::LogHandler& log_handler;

protected:
  std::mutex session_mutex;

  bool notify_pub_fragment(const messages::PublishDatagram& datagram,
                           const std::weak_ptr<SubscriberDelegate>& delegate,
                           const std::map<int, bytes>& frag_map);
  void handle_pub_fragment(messages::PublishDatagram&& datagram,
                           const std::weak_ptr<SubscriberDelegate>& delegate);

  qtransport::LogHandler def_log_handler;

  void make_transport(RelayInfo& relay_info,
                      qtransport::TransportConfig tconfig,
                      qtransport::LogHandler& logger);

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
    qtransport::StreamId transport_stream_id{ 0 };
    uint64_t transaction_id{ 0 };
    uint64_t prev_group_id{ 0 };
    uint64_t prev_object_id{ 0 };
    uint64_t group_id{ 0 };
    uint64_t object_id{ 0 };
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
    qtransport::StreamId transport_stream_id{ 0 };
    uint64_t prev_group_id{ 0 };
    uint64_t prev_object_id{ 0 };
    uint64_t group_id{ 0 };
    uint64_t object_id{ 0 };
    uint64_t offset{ 0 };
  };

  bool need_pacing{ false };
  ClientStatus client_status{ ClientStatus::TERMINATED };
  qtransport::TransportContextId transport_context_id;
  namespace_map<std::weak_ptr<SubscriberDelegate>> sub_delegates;
  namespace_map<std::weak_ptr<PublisherDelegate>> pub_delegates;
  namespace_map<SubscribeContext> subscribe_state{};
  namespace_map<PublishContext> publish_state{};
  std::unique_ptr<qtransport::ITransport::TransportDelegate> transport_delegate;
  uint64_t transport_stream_id{ 0 };
};

}
