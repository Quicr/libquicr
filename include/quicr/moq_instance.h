/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <quicr/metrics_exporter.h>
#include <quicr/quicr_common.h>
#include <transport/transport.h>

#include <quicr/moq_instance_delegate.h>
#include <quicr/moq_track_delegate.h>

namespace quicr {
using namespace qtransport;

class MoQInstanceConfig
{
  RelayInfo relay_info;

  // TODO(tievens): Convert to use URL instead of Relay Info

  std::string instance_id;                    /// Instance ID for the client or server, should be unique
  TransportConfig transport_config;
};

class MoQInstance : public ITransport::TransportDelegate
{

public:
  /**
   *
   * @param cfg     MoQ Instance Configuration
   * @param logger  MoQ Log pointer to parent logger
   */
  MoQInstance(const MoQInstanceConfig& cfg,
              const cantina::LoggerPointer& logger);

  ~MoQInstance();

  // -------------------------------------------------------------------------------------------------
  // Public API MoQ Intance API methods
  // -------------------------------------------------------------------------------------------------
  /**
   * @brief Subscribe to a track
   *
   * @param track_delegate    Track delegate to use for track related functions and callbacks
   *
   * @returns `track_alias` if no error and nullopt on error
   */
  std::optional<uint64_t> subscribeTrack(TransportConnId conn_id, std::shared_ptr<MoQTrackDelegate> track_delegate);


  /**
   * @brief Publish to a track
   *
   * @param track_delegate    Track delegate to use for track related functions
   *                          and callbacks
   *
   * @returns `track_alias` if no error and nullopt on error
   */
  std::optional<uint64_t> publishTrack(TransportConnId conn_id, std::shared_ptr<MoQTrackDelegate> track_delegate);


  // -------------------------------------------------------------------------------------------------

  /*
   * Transport Delegete/callback functions
   */
  void on_new_data_context(const TransportConnId& conn_id,
                           const DataContextId& data_ctx_id) override
  {
  }
  void on_connection_status(const TransportConnId& conn_id,
                            const TransportStatus status) override;
  void on_new_connection(const TransportConnId& conn_id,
                         const TransportRemote& remote) override;
  void on_recv_stream(const TransportConnId& conn_id,
                      uint64_t stream_id,
                      std::optional<DataContextId> data_ctx_id,
                      const bool is_bidir = false) override;
  void on_recv_dgram(const TransportConnId& conn_id,
                     std::optional<DataContextId> data_ctx_id) override;

private:
  /**
   * @brief Client thread to monitor client published messages
   */
  void PeerQueueThread();

  /**
   * @brief Watch Thread to perform reconnects and cleanup
   * @details Thread will perform various tasks each interval
   *
   * @param interval_ms       Interval in milliseconds to sleep before running
   * check
   */
  void watchThread(int interval_ms);

  /**
   * @brief Create a peering session/connection
   *
   * @param peer_config           Peer/relay configuration parameters
   *
   */
  void createPeerSession(const TransportRemote& peer_config);

  /**
   * @brief Send subscribe to first/best publish intent peers
   *
   * @details There is a single best (first in list) publish intent peer to send
   * matching subscribes to for a given origin. This method will iterate over
   * each matching publish intent best origin peer and send a subscribe.
   *
   * @param ns                    Namespace to subscribe
   * @param source_peer_id        Source peer that sent (or client manager) the
   * intent
   */
  void subscribePeers(const Namespace& ns, const peer_id_t& source_peer_id);

  /**
   * @brief Send subscribe to specific peer
   *
   * @param ns                    Namespace to subscribe peers to
   * @param peer_id               Peer ID to send to
   */
  void subscribePeer(const Namespace& ns, const peer_id_t& peer_id);

  /**
   * @brief Send unsubscribe to peers that had previous subscribes for given
   * namespace
   *
   * @param ns                    Namespace to unsubscribe peers to
   * @param source_peer_id        Source peer that sent (or client manager) the
   * intent
   */
  void unSubscribePeers(const Namespace& ns, const peer_id_t& source_peer_id);

  /**
   * @brief Send unsubscribe to specific peer
   *
   * @param ns                    Namespace to unsubscribe peers to
   * @param peer_id               Peer ID to send to
   */
  void unSubscribePeer(const Namespace& ns, const peer_id_t& peer_id);

  /**
   * @brief Send publish intent to all peers
   *
   * @details Currently publish intents are flooded to all peers
   *
   * @param ns                   Namespace for publish intent
   * @param source_peer_id       Source peer that sent (or client manager) the
   * intent
   * @param origin_peer_id       Origin peer/relay that has the publisher
   * directly connected
   */
  void publishIntentPeers(const Namespace& ns,
                          const peer_id_t& source_peer_id,
                          const peer_id_t& origin_peer_id);

  /**
   * @brief Send publish intent done
   *
   * @brief Currently publish intents are flooded to all peers, so the done
   * needs to be flooded as well
   *
   * @param ns                    Namespace for publish intent done
   * @param source_peer_id        Source peer that sent (or client manager) the
   * intent
   * @param origin_peer_id        Origin peer/relay that has the publisher
   * directly connected
   */
  void publishIntentDonePeers(const Namespace& ns,
                              const peer_id_t& source_peer_id,
                              const peer_id_t& origin_peer_id);

  /**
   * @brief Get the pointer to the session by peer_id
   *
   * @param peer_id               Peer ID to get session
   *
   * @return Returns nullptr or peer session if found
   */
  PeerSession* getPeerSession(const peer_id_t& peer_id);

  void addSubscribedPeer(const Namespace& ns, const peer_id_t& peer_id);

private:
  std::mutex _mutex;
  std::atomic<bool> _stop{ false };
  const Config& _config;

  peerQueue& _peer_queue;
  Cache& _cache;
  ClientSubscriptions& _subscriptions;

  std::shared_ptr<ITransport>
    _server_transport; /// Server Transport for inbound connections

  std::thread _client_rx_msg_thr; /// Client receive message thread
  std::thread _watch_thr;         /// Watch/task thread, handles reconnects

  /// Peer sessions that are accepted by the server
  std::map<TransportConnId, PeerSession> _server_peer_sessions;

  std::vector<PeerSession>
    _client_peer_sessions; /// Peer sessions that are initiated by the peer
                           /// manager

  // TODO: Fix to use list for peer sessions subscribed
  namespace_map<std::set<peer_id_t>>
    _peer_sess_subscribe_sent; /// Peers that subscribes have been sent
  namespace_map<std::set<peer_id_t>>
    _peer_sess_subscribe_recv; /// Peers that subscribes have been received
  namespace_map<std::map<peer_id_t, std::list<peer_id_t>>>
    _pub_intent_namespaces; /// Publish intents received from peers

  // Log handler to use
  cantina::LoggerPointer logger;

#ifndef LIBQUICR_WITHOUT_INFLUXDB
  std::shared_ptr<MetricsExporter> _mexport;
#endif
};

} // namespace quicr
