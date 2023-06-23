/*
 *  quicr_server_raw_session.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 *
 *  Description:
 *      This file defines a session layer between the server APIs and the
 *      transport that uses raw data packets, namely UDP or QUIC.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include <map>
#include <mutex>

#include <quicr/encode.h>
#include <quicr/message_buffer.h>
#include <transport/transport.h>
#include "quicr/quicr_server_delegate.h"
#include "quicr/quicr_server_session.h"

/*
 * QUICR Server Raw Session Interface
 */
namespace quicr {

class QuicRServerRawSession : public QuicRServerSession
{
public:
  /**
   * Start the  QUICR server at the port specified.
   *
   * @param relayInfo        : Relay Information to be used by the transport
   * @param tconfig          : Transport configuration
   * @param delegate         : Server delegate
   * @param logger           : Log handler instance. Will be used by transport
   *                           quicr api
   */
  QuicRServerRawSession(RelayInfo& relayInfo,
                        qtransport::TransportConfig tconfig,
                        ServerDelegate& delegate,
                        qtransport::LogHandler& logger);

  /**
   * API for unit test cases.
   */
  QuicRServerRawSession(
    std::shared_ptr<qtransport::ITransport> transport,
    ServerDelegate& delegate /* TODO: Considering shared or weak pointer */,
    qtransport::LogHandler& logger);

  ~QuicRServerRawSession() = default;


  // Transport APIs
  bool is_transport_ready() override;

  /**
   * @brief Run Server API event loop
   *
   * @details This method will open listening sockets and run an event loop
   *    for callbacks.
   *
   * @returns true if error, false if no error
   */
  bool run() override;

  /**
   * @brief Send publish intent response
   *
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param result                : Status of Publish Intetn
   *
   * @details Entities processing the Subscribe Request MUST validate the
   * request
   * @todo: Add payload with origin signed blob
   */
  void publishIntentResponse(const quicr::Namespace& quicr_namespace,
                             const PublishIntentResult& result) override;

  /**
   * @brief Send subscribe response
   *
   * @details Entities processing the Subscribe Request MUST validate the
   * request
   *
   * @param subscriber_id         : Subscriber ID to send the message to
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param result                : Status of Subscribe operation
   *
   */
  void subscribeResponse(const uint64_t& subscriber_id,
                         const quicr::Namespace& quicr_namespace,
                         const SubscribeResult& result) override;

  /**
   * @brief Send subscription end message
   *
   * @details  Subscription can terminate when a publisher terminated
   *           the stream or subscription timeout, upon unsubscribe,
   *           or other application reasons
   *
   * @param subscriber_id         : Subscriber ID to send the message to
   * @param quicr_namespace       : Identifies QUICR namespace
   * @param reason                : Reason of Subscribe End operation
   *
   */
  void subscriptionEnded(
    const uint64_t& subscriber_id,
    const quicr::Namespace& quicr_namespace,
    const SubscribeResult::SubscribeStatus& reason) override;

  /**
   * @brief Send a named QUICR media object
   *
   * @param subscriber_id            : Subscriber ID to send the message to
   * @param use_reliable_transport   : Indicates the preference for the object's
   *                                   transport, if forwarded.
   * @param priority                 : Identifies the relative priority of the
   *                                   current object
   * @param expiry_age_ms            : Time hint for the object to be in cache
   *                                   before being purged after reception
   * @param datagram                 : QuicR Publish Datagram to send
   *
   */
  void sendNamedObject(const uint64_t& subscriber_id,
                       bool use_reliable_transport,
                       uint8_t priority,
                       uint16_t expiry_age_ms,
                       const messages::PublishDatagram& datagram) override;

private:
  /*
   * Implementation of the transport delegate
   */
  class TransportDelegate : public qtransport::ITransport::TransportDelegate
  {
  public:
    TransportDelegate(QuicRServerRawSession& server);

    void on_connection_status(
      const qtransport::TransportContextId& context_id,
      const qtransport::TransportStatus status) override;
    void on_new_connection(const qtransport::TransportContextId& context_id,
                           const qtransport::TransportRemote& remote) override;
    void on_new_stream(
      const qtransport::TransportContextId& context_id,
      const qtransport::StreamId& streamId) override;
    void on_recv_notify(const qtransport::TransportContextId& context_id,
                        const qtransport::StreamId& streamId) override;



  private:
    QuicRServerRawSession& server;
  };

public:
  std::mutex session_mutex;

  /*
   * Metrics
   */
  uint64_t recv_data_count { 0 };
  uint64_t recv_data_count_null { 0 };
  uint64_t recv_publish { 0 };
  uint64_t recv_subscribes { 0 };
  uint64_t recv_unsubscribes { 0 };
  uint64_t recv_pub_intents { 0 };


private:

  std::shared_ptr<qtransport::ITransport> setupTransport(
    RelayInfo& relayInfo, qtransport::TransportConfig cfg);

  void handle_subscribe(const qtransport::TransportContextId& context_id,
                        const qtransport::StreamId& streamId,
                        messages::MessageBuffer&& msg);
  void handle_unsubscribe(const qtransport::TransportContextId& context_id,
                          const qtransport::StreamId& streamId,
                          messages::MessageBuffer&& msg);
  void handle_publish(const qtransport::TransportContextId& context_id,
                      const qtransport::StreamId& streamId,
                      messages::MessageBuffer&& msg);
  void handle_publish_intent(const qtransport::TransportContextId& context_id,
                             const qtransport::StreamId& mStreamId,
                             messages::MessageBuffer&& msg);
  void handle_publish_intent_response(
    const qtransport::TransportContextId& context_id,
    const qtransport::StreamId& mStreamId,
    messages::MessageBuffer&& msg);
  void handle_publish_intent_end(
    const qtransport::TransportContextId& context_id,
    const qtransport::StreamId& mStreamId,
    messages::MessageBuffer&& msg);

  struct Context
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
  };

  struct SubscribeContext : public Context
  {
    uint64_t transaction_id{ 0 };
    uint64_t subscriber_id{ 0 };
    uint64_t group_id{ 0 };
    uint64_t object_id{ 0 };
    uint64_t prev_group_id{ 0 };
    uint64_t prev_object_id{ 0 };
  };

  struct PublishIntentContext : public Context
  {
    uint64_t transaction_id{ 0 };
    uint64_t group_id{ 0 };
    uint64_t object_id{ 0 };
    uint64_t prev_group_id{ 0 };
    uint64_t prev_object_id{ 0 };
  };

  ServerDelegate& delegate;
  qtransport::LogHandler& log_handler;
  TransportDelegate transport_delegate;
  std::shared_ptr<qtransport::ITransport> transport;
  qtransport::TransportRemote t_relay;
  std::map<quicr::Namespace,
           std::map<qtransport::TransportContextId, SubscribeContext>>
    subscribe_state{};
  std::map<uint64_t, SubscribeContext> subscribe_id_state{};
  namespace_map<PublishIntentContext> publish_namespaces{};
  bool running{ false };
  uint64_t subscriber_id{ 0 };
};

} // namespace quicr
