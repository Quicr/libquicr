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

#include "quicr/encode.h"
#include "quicr/message_buffer.h"
#include "quicr/quicr_server_delegate.h"
#include "quicr/quicr_server_session.h"
#include "quicr/metrics_exporter.h"

#include <transport/transport.h>
#include <spdlog/spdlog.h>

#include <map>
#include <mutex>

/*
 * QUICR Server Raw Session Interface
 */
namespace quicr {

class ServerRawSession : public ServerSession
{
public:
  /**
   * Start the  QUICR server at the port specified.
   *
   * @param relayInfo        : Relay Information to be used by the transport
   * @param tconfig          : Transport configuration
   * @param delegate         : Server delegate
   */
  ServerRawSession(const RelayInfo& relayInfo,
                   const qtransport::TransportConfig& tconfig,
                   std::shared_ptr<ServerDelegate> delegate);

  /**
   * API for unit test cases.
   */
  ServerRawSession(std::shared_ptr<qtransport::ITransport> transport,
                   std::shared_ptr<ServerDelegate> delegate);

  ~ServerRawSession() = default;

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
   *           the data flow or subscription timeout, upon unsubscribe,
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
   * @param priority                 : Identifies the relative priority of the
   *                                   current object
   * @param expiry_age_ms            : Time hint for the object to be in cache
   *                                   before being purged after reception
   * @param datagram                 :  Publish Datagram to send
   *
   */
  void sendNamedObject(const uint64_t& subscriber_id,
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
    TransportDelegate(ServerRawSession& server);

    void on_connection_status(const qtransport::TransportConnId& conn_id,
                              const qtransport::TransportStatus status) override;
    void on_new_connection(const qtransport::TransportConnId& conn_id,
                           const qtransport::TransportRemote& remote) override;
    void on_new_data_context(const qtransport::TransportConnId& conn_id,
                             const qtransport::DataContextId& data_ctx_id) override;

    void on_recv_dgram(const TransportConnId& conn_id,
                       std::optional<DataContextId> data_ctx_id) override;

    void on_recv_stream(const TransportConnId& conn_id,
                        uint64_t stream_id,
                        std::optional<DataContextId> data_ctx_id,
                        const bool is_bidir) override;

  private:
    ServerRawSession& server;
  };

public:
  std::mutex session_mutex;

  /*
   * Metrics
   */
  uint64_t recv_data_count{ 0 };
  uint64_t recv_publish{ 0 };
  uint64_t recv_subscribes{ 0 };
  uint64_t recv_unsubscribes{ 0 };
  uint64_t recv_pub_intents{ 0 };

private:
  std::shared_ptr<qtransport::ITransport> setupTransport(
    const qtransport::TransportConfig& cfg);

  void handle(TransportConnId conn_id,
              std::optional<uint64_t> stream_id,
              std::optional<qtransport::DataContextId> data_ctx_id,
              messages::MessageBuffer&& msg,
              bool is_bidir=false);

  void handle_connect(const qtransport::TransportConnId& conn_id,
                      messages::MessageBuffer&& msg);
  void handle_subscribe(const qtransport::TransportConnId& conn_id,
                        const qtransport::DataContextId& data_ctx_id,
                        messages::MessageBuffer&& msg);
  void handle_unsubscribe(const qtransport::TransportConnId& conn_id,
                          const qtransport::DataContextId& data_ctx_id,
                          messages::MessageBuffer&& msg);
  void handle_publish(qtransport::TransportConnId conn_id,
                      std::optional<uint64_t> stream_id,
                      std::optional<qtransport::DataContextId> data_ctx_id,
                      messages::MessageBuffer&& msg);
  void handle_publish_intent(const qtransport::TransportConnId& conn_id,
                             const qtransport::DataContextId& data_ctx_id,
                             messages::MessageBuffer&& msg);
  void handle_publish_intent_response(const qtransport::TransportConnId& conn_id,
                                      const qtransport::DataContextId& data_ctx_id,
                                      messages::MessageBuffer&& msg);
  void handle_publish_intent_end(const qtransport::TransportConnId& conn_id,
                                 const qtransport::DataContextId& data_ctx_id,
                                 messages::MessageBuffer&& msg);

  TransportError enqueue_ctrl_msg(TransportConnId conn_id,
                                  DataContextId data_ctx_id,
                                  bytes&& msg_data)
  {
    return transport->enqueue(conn_id,
                              data_ctx_id,
                              std::move(msg_data),
                              { MethodTraceItem{} },
                              0,
                              1000,
                              0,
                              { true, false, false, false });

  }

  struct ConnectionContext {
    qtransport::TransportConnId conn_id {0};
    qtransport::DataContextId ctrl_data_ctx_id {0};
    qtransport::TransportRemote remote;
    std::string endpoint_id;
  };

  struct DataContext
  {
    enum struct State
    {
      Unknown = 0,
      Pending,
      Ready
    };

    State state{ State::Unknown };
    qtransport::TransportConnId transport_conn_id {0};
    TransportMode transport_mode {0};
  };

  struct SubscribeContext : public DataContext
  {
    bool pending_reliable_data_ctx { false };           /// True indicates that the data_ctx_id needs to be created on
                                                        ///    first object using the object's priority
    bool transport_mode_follow_publisher {false};       /// Indicates if transport mode should be updated based on pub
    qtransport::DataContextId data_ctx_id {0};          /// Data context ID used for sending objects based on subscribe
    qtransport::DataContextId remote_data_ctx_id {0};   /// Remote data context ID to add to data header
    uint8_t priority { 126 };                           /// Priority used for sending subscribed objects

    bool paused { false };                              /// Indicates if objects should not be sent (e.g., paused)

    quicr::Namespace nspace;                            /// Subscribed namespace
    uint64_t transaction_id{ 0 };
    uint64_t subscriber_id{ 0 };
    uint64_t group_id{ 0 };
    uint64_t object_id{ 0 };
  };

  struct PublishIntentContext : public DataContext
  {
    uint64_t transaction_id{ 0 };
    uint64_t prev_group_id{ 0 };
    uint64_t prev_object_id{ 0 };

    qtransport::DataContextId data_ctx_id {0};
  };

  std::shared_ptr<ServerDelegate> delegate;
  std::shared_ptr<spdlog::logger> logger;
  TransportDelegate transport_delegate;
  std::shared_ptr<qtransport::ITransport> transport;
  qtransport::TransportRemote t_relay;
  std::string relay_id;

  using SubscriptionContextMap = std::map<qtransport::TransportConnId, std::shared_ptr<SubscribeContext>>;
  namespace_map<SubscriptionContextMap> _subscribe_state{};
  std::map<uint64_t, std::shared_ptr<SubscribeContext>> subscribe_id_state{};
  std::map<qtransport::TransportConnId, ConnectionContext> _connections;

  // TODO: Support more than one publisher per ns
  namespace_map<PublishIntentContext> publish_namespaces{};

  bool _running{ false };
  uint64_t _subscriber_id{ 0 };

#ifndef LIBQUICR_WITHOUT_INFLUXDB
  MetricsExporter _mexport;
#endif
};

} // namespace quicr
