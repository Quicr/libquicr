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
#include "quicr/moq_message_types.h"

#include <cantina/logger.h>
#include <transport/transport.h>

#include <map>
#include <mutex>
#include <list>
#include <set>
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
   * @param logger           : Log handler instance. Will be used by transport
   *                           quicr api
   */
  ServerRawSession(const RelayInfo& relayInfo,
                   const qtransport::TransportConfig& tconfig,
                   std::shared_ptr<ServerDelegate> delegate,
                   const cantina::LoggerPointer& logger,
                   std::shared_ptr<UriConvertor> uri_convertor = nullptr);

  /**
   * API for unit test cases.
   */
  ServerRawSession(std::shared_ptr<qtransport::ITransport> transport,
                   std::shared_ptr<ServerDelegate> delegate,
                   const cantina::LoggerPointer& logger);

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
                             const uint64_t publisher_id,
                             const PublishIntentResult& result) override;

  virtual void subscribe(const quicr::Namespace& quicr_namespace,
                         const SubscribeIntent& intent,
                         const TransportMode transport_mode) override;


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
    void on_recv_notify(const qtransport::TransportConnId& conn_id,
                        const qtransport::DataContextId& data_ctx_id,
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

  bool is_moq_enabled() const {
      return  enable_moq;
  }

private:
  std::shared_ptr<qtransport::ITransport> setupTransport(
    const qtransport::TransportConfig& cfg);

  void handle_subscribe(const qtransport::TransportConnId& conn_id,
                        const qtransport::DataContextId& data_ctx_id,
                        messages::MessageBuffer&& msg);
  void handle_unsubscribe(const qtransport::TransportConnId& conn_id,
                          const qtransport::DataContextId& data_ctx_id,
                          messages::MessageBuffer&& msg);
  void handle_publish(const qtransport::TransportConnId& conn_id,
                      const qtransport::DataContextId& data_ctx_id,
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

  void handle_moq(const qtransport::TransportConnId& conn_id,
                  const qtransport::DataContextId& data_ctx_id,
                  bool is_bidir,
                  std::vector<uint8_t>&& data);


  struct ConnectionContext {
    qtransport::TransportConnId conn_id {0};
    qtransport::DataContextId ctrl_data_ctx_id {0};
    qtransport::TransportRemote remote;
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
    uint8_t priority { 126 };                           /// Priority used for sending subscribed objects

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
  };

  std::shared_ptr<ServerDelegate> delegate;
  cantina::LoggerPointer logger;
  TransportDelegate transport_delegate;
  std::shared_ptr<qtransport::ITransport> transport;
  qtransport::TransportRemote t_relay;

  using SubscriptionContextMap = std::map<qtransport::TransportConnId, std::shared_ptr<SubscribeContext>>;
  // namespace -> [connId -> SubscribeContext ]
  namespace_map<SubscriptionContextMap> _subscribe_state{};
  std::map<uint64_t, std::shared_ptr<SubscribeContext>> subscribe_id_state{};

  // Active Connections
  std::map<qtransport::TransportConnId, ConnectionContext> _connections;

  // TODO: Support more than one publisher per ns
  namespace_map<PublishIntentContext> publish_namespaces{};

  bool _running{ false };
  uint64_t _subscriber_id{ 0 };

  // Moq Stuff
  std::shared_ptr<UriConvertor> uri_convertor {nullptr};
  bool enable_moq {false};

  struct TransportContext {
    qtransport::TransportConnId transport_conn_id { 0 };
    qtransport::DataContextId transport_data_ctx_id { 0 };
  };

  struct Subscription {
    enum struct State{
        pending = 0,
        ready
    };

    State state;
    messages::SubscribeId subscribe_id;
    messages::TrackAlias track_alias;
    quicr::Namespace quicr_namespace;
    std::string uri;

    TransportContext transport_context {};
  };

  // Active subscription keyed by quicr::namespace and per connection
  namespace_map<std::list<std::shared_ptr<Subscription>>> active_subscriptions{};
  std::map<messages::SubscribeId, quicr::Namespace> subscribe_id_namespace {};

  struct TrackInfo {
      enum class State {
          Inactive = 0,
          Ready
      };

      State state;
      quicr::Namespace quicr_namespace;
      messages::FullTrackName  fulltrackname;
      messages::TrackAlias track_alias{0};
      uint64_t last_group_id{ 0 };
      uint64_t last_object_id{ 0 };
      uint64_t offset{ 0 };
      TransportMode transport_mode {TransportMode::ReliablePerObject};
  };

  using PublisherId = uint64_t;
  PublisherId publisher_id {0};

  struct AnnounceInfo {
    enum State {
        Pending = 0,
        Active,
        Done
    };

    State state { Pending };
    messages::TrackNamespace track_namespace;
    quicr::Namespace quicr_namespace;
    TransportMode transport_mode {TransportMode::ReliablePerGroup};
    uint64_t publisher_id {0}; // publisher of the track
    TransportContext transport_context;
  };

  std::mutex announce_mutex;

  /*
   * Note on publisher
   * - Announcements happen on Track Namespace
   * - one or more tracks are published under the track namespace
   * - More than on publisher can announce on the same track namespace
   */
  std::map<PublisherId, qtransport::TransportConnId> publisher_connection_map {};
  namespace_map<std::map<qtransport::TransportConnId, AnnounceInfo>> announcements{};

  // active tracks
  std::map<messages::TrackAlias, std::map<qtransport::TransportConnId, TrackInfo>> tracks {};

  // Subscriber and Subscription State
  struct SubscriptionInfo {
    messages::SubscribeId  subscription_id;
    TrackInfo track_info;
    TransportContext transport_context;
  };

  namespace_map<bool> subscription_exists {};
  namespace_map<messages::TrackAlias> subscriber_namespace_map{};
  std::map<messages::SubscribeId, std::map<qtransport::TransportConnId, SubscriptionInfo>>  subscriptions{};
  //std::map<messages::TrackAlias, std::map<qtransport::TransportConnId, SubscriptionInfo>> subscriptions {};
};

} // namespace quicr
