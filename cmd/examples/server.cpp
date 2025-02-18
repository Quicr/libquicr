// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <condition_variable>
#include <oss/cxxopts.hpp>
#include <set>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <unordered_map>

#include <quicr/cache.h>
#include <quicr/server.h>

#include "signal_handler.h"

using TrackNamespaceHash = uint64_t;
using TrackNameHash = uint64_t;
using FullTrackNameHash = uint64_t;

/**
 * @brief Defines an object received from an announcer that lives in the cache.
 */
struct CacheObject
{
    quicr::ObjectHeaders headers;
    quicr::Bytes data;
};

/**
 * @brief Specialization of std::less for sorting CacheObjects by object ID.
 */
template<>
struct std::less<CacheObject>
{
    constexpr bool operator()(const CacheObject& lhs, const CacheObject& rhs) const noexcept
    {
        return lhs.headers.object_id < rhs.headers.object_id;
    }
};

namespace qserver_vars {
    bool force_track_alias{ true };

    std::mutex state_mutex;

    /**
     * Map of subscribes (e.g., track alias) sent to announcements
     *
     * @example
     *      track_alias_set = announce_active[track_namespace_hash][connection_handle]
     */
    std::unordered_map<TrackNamespaceHash,
                       std::unordered_map<quicr::ConnectionHandle, std::set<quicr::messages::TrackAlias>>>
      announce_active;

    /**
     * Active subscriber publish tracks for a given track, indexed (keyed) by track_alias, connection handle
     *
     * @note This indexing intentionally prohibits per connection having more
     *           than one subscribe to a full track name.
     *
     * @example track_handler = subscribes[track_alias][connection_handle]
     */
    std::unordered_map<quicr::messages::TrackAlias,
                       std::unordered_map<quicr::ConnectionHandle, std::shared_ptr<quicr::PublishTrackHandler>>>
      subscribes;

    /**
     * Subscribe ID to alias mapping
     *      Used to lookup the track alias for a given subscribe ID
     *
     * @example
     *      track_alias = subscribe_alias_sub_id[conn_id][subscribe_id]
     */
    std::unordered_map<quicr::ConnectionHandle,
                       std::unordered_map<quicr::messages::SubscribeId, quicr::messages::TrackAlias>>
      subscribe_alias_sub_id;

    /**
     * Map of subscribes set by namespace and track name hash
     *      Set<subscribe_who> = subscribe_active[track_namespace_hash][track_name_hash]
     */
    struct SubscribeInfo
    {
        uint64_t connection_handle;
        uint64_t subscribe_id;
        uint64_t track_alias;

        bool operator<(const SubscribeInfo& other) const
        {
            return connection_handle < other.connection_handle ||
                   (connection_handle == other.connection_handle && subscribe_id < other.subscribe_id);
        }
        bool operator==(const SubscribeInfo& other) const
        {
            return connection_handle == other.connection_handle && subscribe_id == other.subscribe_id;
        }
        bool operator>(const SubscribeInfo& other) const
        {
            return connection_handle > other.connection_handle ||
                   (connection_handle == other.connection_handle && subscribe_id > other.subscribe_id);
        }
    };
    std::unordered_map<uint64_t, std::unordered_map<uint64_t, std::set<SubscribeInfo>>> subscribe_active;

    /**
     * Active publisher/announce subscribes that this relay has made to receive objects from publisher.
     *
     * @example
     *      track_delegate = pub_subscribes[track_alias][conn_id]
     */
    std::unordered_map<quicr::messages::TrackAlias,
                       std::unordered_map<quicr::ConnectionHandle, std::shared_ptr<quicr::SubscribeTrackHandler>>>
      pub_subscribes;

    /**
     * Cache of MoQ objects by track namespace hash
     */
    std::map<quicr::TrackNamespaceHash, quicr::Cache<quicr::messages::GroupId, std::set<CacheObject>>> cache;

    /**
     * Tick Service used by the cache
     */
    std::shared_ptr<quicr::ThreadedTickService> tick_service = std::make_shared<quicr::ThreadedTickService>();

}

/**
 * @brief  Subscribe track handler
 * @details Subscribe track handler used for the subscribe command line option.
 */
class MySubscribeTrackHandler : public quicr::SubscribeTrackHandler
{
  public:
    MySubscribeTrackHandler(const quicr::FullTrackName& full_track_name)
      : SubscribeTrackHandler(full_track_name,
                              3,
                              quicr::messages::GroupOrder::kAscending,
                              quicr::messages::FilterType::LatestObject)
    {
    }

    void ObjectReceived(const quicr::ObjectHeaders& object_headers, quicr::BytesSpan data) override
    {
        if (data.size() > 255) {
            SPDLOG_CRITICAL("Example server is for example only, received data > 255 bytes is not allowed!");
            SPDLOG_CRITICAL("Use github.com/quicr/laps for full relay functionality");
            throw std::runtime_error("Example server is for example only, received data > 255 bytes is not allowed!");
        }

        latest_group_ = object_headers.group_id;
        latest_object_ = object_headers.object_id;

        std::lock_guard<std::mutex> _(qserver_vars::state_mutex);

        auto track_alias = GetTrackAlias();
        if (!track_alias.has_value()) {
            SPDLOG_DEBUG("Data without valid track alias");
            return;
        }

        auto sub_it = qserver_vars::subscribes.find(track_alias.value());

        if (sub_it == qserver_vars::subscribes.end()) {
            SPDLOG_INFO("No subscribes, not relaying data size: {0} ", data.size());
            return;
        }

        // Cache Object
        if (qserver_vars::cache.count(*track_alias) == 0) {
            qserver_vars::cache.insert(std::make_pair(
              *track_alias,
              quicr::Cache<quicr::messages::GroupId, std::set<CacheObject>>{ 50000, 1, qserver_vars::tick_service }));
        }

        auto& cache_entry = qserver_vars::cache.at(*track_alias);

        CacheObject object{ object_headers, { data.begin(), data.end() } };

        if (auto group = cache_entry.Get(object_headers.group_id)) {
            group->insert(std::move(object));
        } else {
            cache_entry.Insert(object_headers.group_id, { std::move(object) }, 50000);
        }

        // Fan out to all subscribers
        for (const auto& [conn_id, pth] : sub_it->second) {
            pth->PublishObject(object_headers, data);
        }
    }

    void StatusChanged(Status status) override
    {
        if (status == Status::kOk) {
            SPDLOG_INFO("Track alias: {0} is subscribed", GetTrackAlias().value());
        } else {
            std::string reason = "";
            switch (status) {
                case Status::kNotConnected:
                    reason = "not connected";
                    break;
                case Status::kError:
                    reason = "subscribe error";
                    break;
                case Status::kNotAuthorized:
                    reason = "not authorized";
                    break;
                case Status::kNotSubscribed:
                    reason = "not subscribed";
                    break;
                case Status::kPendingResponse:
                    reason = "pending subscribe response";
                    break;
                case Status::kSendingUnsubscribe:
                    reason = "unsubscribing";
                    break;
                default:
                    break;
            }
            SPDLOG_INFO("Track alias: {0} failed to subscribe reason: {1}", GetTrackAlias().value(), reason);
        }
    }

  private:
    uint64_t latest_group_{ 0 };
    uint64_t latest_object_{ 0 };
};

/**
 * @brief Publish track handler
 * @details Publish track handler used for the publish command line option
 */
class MyPublishTrackHandler : public quicr::PublishTrackHandler
{
  public:
    MyPublishTrackHandler(const quicr::FullTrackName& full_track_name,
                          quicr::TrackMode track_mode,
                          uint8_t default_priority,
                          uint32_t default_ttl)
      : quicr::PublishTrackHandler(full_track_name, track_mode, default_priority, default_ttl)
    {
    }

    void StatusChanged(Status status) override
    {
        if (status == Status::kOk) {
            SPDLOG_INFO("Publish track alias {0} has subscribers", GetTrackAlias().value());
        } else {
            std::string reason = "";
            switch (status) {
                case Status::kNotConnected:
                    reason = "not connected";
                    break;
                case Status::kNotAnnounced:
                    reason = "not announced";
                    break;
                case Status::kAnnounceNotAuthorized:
                    reason = "not authorized";
                    break;
                case Status::kPendingAnnounceResponse:
                    reason = "pending announce response";
                    break;
                case Status::kNoSubscribers:
                    reason = "no subscribers";
                    break;
                case Status::kSendingUnannounce:
                    reason = "sending unannounce";
                    break;
                default:
                    break;
            }
            SPDLOG_INFO("Publish track alias: {0} not ready, reason: {1}", GetTrackAlias().value(), reason);
        }
    }

    void MetricsSampled(const quicr::PublishTrackMetrics& metrics) override
    {
        SPDLOG_DEBUG("Metrics sample time: {0}"
                     " track_alias: {1}"
                     " objects sent: {2}"
                     " bytes sent: {3}"
                     " object duration us: {4}"
                     " queue discards: {5}"
                     " queue size: {6}",
                     metrics.last_sample_time,
                     GetTrackAlias().value(),
                     metrics.objects_published,
                     metrics.bytes_published,
                     metrics.quic.tx_object_duration_us.avg,
                     metrics.quic.tx_queue_discards,
                     metrics.quic.tx_queue_size.avg);
    }
};

/**
 * @brief MoQ Server
 * @details Implementation of the MoQ Server
 */
class MyServer : public quicr::Server
{
  public:
    MyServer(const quicr::ServerConfig& cfg)
      : quicr::Server(cfg)
    {
    }

    void NewConnectionAccepted(quicr::ConnectionHandle connection_handle, const ConnectionRemoteInfo& remote) override
    {
        SPDLOG_INFO("New connection handle {0} accepted from {1}:{2}", connection_handle, remote.ip, remote.port);
    }

    void MetricsSampled(quicr::ConnectionHandle connection_handle, const quicr::ConnectionMetrics& metrics) override
    {
        SPDLOG_DEBUG("Metrics sample time: {0}"
                     " connection handle: {1}"
                     " rtt_us: {2}"
                     " srtt_us: {3}"
                     " rate_bps: {4}"
                     " lost pkts: {5}",
                     metrics.last_sample_time,
                     connection_handle,
                     metrics.quic.rtt_us.max,
                     metrics.quic.srtt_us.max,
                     metrics.quic.tx_rate_bps.max,
                     metrics.quic.tx_lost_pkts);
    }

    void UnannounceReceived(quicr::ConnectionHandle connection_handle,
                            const quicr::TrackNamespace& track_namespace) override
    {
        auto th = quicr::TrackHash({ track_namespace, {}, std::nullopt });

        SPDLOG_DEBUG("Received unannounce from connection handle: {0} for namespace hash: {1}, removing all tracks "
                     "associated with namespace",
                     connection_handle,
                     th.track_namespace_hash);

        for (auto track_alias : qserver_vars::announce_active[th.track_namespace_hash][connection_handle]) {
            auto ptd = qserver_vars::pub_subscribes[track_alias][connection_handle];
            if (ptd != nullptr) {
                SPDLOG_INFO(
                  "Received unannounce from connection handle: {0} for namespace hash: {1}, removing track alias: {2}",
                  connection_handle,
                  th.track_namespace_hash,
                  track_alias);

                UnsubscribeTrack(connection_handle, ptd);
            }
            qserver_vars::pub_subscribes[track_alias].erase(connection_handle);
            if (qserver_vars::pub_subscribes[track_alias].empty()) {
                qserver_vars::pub_subscribes.erase(track_alias);
            }
        }

        qserver_vars::announce_active[th.track_namespace_hash].erase(connection_handle);
        if (qserver_vars::announce_active[th.track_namespace_hash].empty()) {
            qserver_vars::announce_active.erase(th.track_namespace_hash);
        }
    }

    void AnnounceReceived(quicr::ConnectionHandle connection_handle,
                          const quicr::TrackNamespace& track_namespace,
                          const quicr::PublishAnnounceAttributes&) override
    {
        auto th = quicr::TrackHash({ track_namespace, {}, std::nullopt });

        SPDLOG_INFO("Received announce from connection handle: {0} for namespace_hash: {1}",
                    connection_handle,
                    th.track_namespace_hash);

        // Add to state if not exist
        auto [anno_conn_it, is_new] =
          qserver_vars::announce_active[th.track_namespace_hash].try_emplace(connection_handle);

        if (!is_new) {
            SPDLOG_INFO("Received announce from connection handle: {0} for namespace hash: {0} is duplicate, ignoring",
                        connection_handle,
                        th.track_namespace_hash);
            return;
        }

        AnnounceResponse announce_response;
        announce_response.reason_code = quicr::Server::AnnounceResponse::ReasonCode::kOk;
        ResolveAnnounce(connection_handle, track_namespace, announce_response);

        auto& anno_tracks = qserver_vars::announce_active[th.track_namespace_hash][connection_handle];

        // Check if there are any subscribes. If so, send subscribe to announce for all tracks matching namespace
        const auto sub_active_it = qserver_vars::subscribe_active.find(th.track_namespace_hash);
        if (sub_active_it != qserver_vars::subscribe_active.end()) {
            for (const auto& [track_name, who] : sub_active_it->second) {
                if (who.size()) { // Have subscribes
                    auto& a_who = *who.begin();
                    if (anno_tracks.find(a_who.track_alias) == anno_tracks.end()) {
                        SPDLOG_INFO("Sending subscribe to announcer connection handle: {0} subscribe track_alias: {1}",
                                    connection_handle,
                                    a_who.track_alias);

                        anno_tracks.insert(a_who.track_alias); // Add track to state

                        const auto pub_track_h = qserver_vars::subscribes[a_who.track_alias][a_who.connection_handle];

                        auto sub_track_handler =
                          std::make_shared<MySubscribeTrackHandler>(pub_track_h->GetFullTrackName());

                        SubscribeTrack(connection_handle, sub_track_handler);
                        qserver_vars::pub_subscribes[a_who.track_alias][connection_handle] = sub_track_handler;
                    }
                }
            }
        }
    }

    void ConnectionStatusChanged(quicr::ConnectionHandle connection_handle, ConnectionStatus status) override
    {
        if (status == ConnectionStatus::kConnected) {
            SPDLOG_DEBUG("Connection ready connection_handle: {0} ", connection_handle);
        } else {
            SPDLOG_DEBUG(
              "Connection changed connection_handle: {0} status: {1}", connection_handle, static_cast<int>(status));
        }
    }

    ClientSetupResponse ClientSetupReceived(quicr::ConnectionHandle,
                                            const quicr::ClientSetupAttributes& client_setup_attributes) override
    {
        ClientSetupResponse client_setup_response;

        SPDLOG_INFO("Client setup received from endpoint_id: {0}", client_setup_attributes.endpoint_id);

        return client_setup_response;
    }

    void UnsubscribeReceived(quicr::ConnectionHandle connection_handle, uint64_t subscribe_id) override
    {
        SPDLOG_INFO("Unsubscribe connection handle: {0} subscribe_id: {1}", connection_handle, subscribe_id);

        auto ta_conn_it = qserver_vars::subscribe_alias_sub_id.find(connection_handle);
        if (ta_conn_it == qserver_vars::subscribe_alias_sub_id.end()) {
            SPDLOG_WARN("Unable to find track alias connection for connection handle: {0} subscribe_id: {1}",
                        connection_handle,
                        subscribe_id);
            return;
        }

        auto ta_it = ta_conn_it->second.find(subscribe_id);
        if (ta_it == ta_conn_it->second.end()) {
            SPDLOG_WARN("Unable to find track alias for connection handle: {0} subscribe_id: {1}",
                        connection_handle,
                        subscribe_id);
            return;
        }

        std::lock_guard<std::mutex> _(qserver_vars::state_mutex);

        auto track_alias = ta_it->second;

        ta_conn_it->second.erase(ta_it);
        if (!ta_conn_it->second.size()) {
            qserver_vars::subscribe_alias_sub_id.erase(ta_conn_it);
        }

        auto& track_h = qserver_vars::subscribes[track_alias][connection_handle];

        if (track_h == nullptr) {
            SPDLOG_WARN("Unsubscribe unable to find track delegate for connection handle: {0} subscribe_id: {1}",
                        connection_handle,
                        subscribe_id);
            return;
        }

        auto th = quicr::TrackHash(track_h->GetFullTrackName());

        qserver_vars::subscribes[track_alias].erase(connection_handle);
        bool unsub_pub{ false };
        if (!qserver_vars::subscribes[track_alias].size()) {
            unsub_pub = true;
            qserver_vars::subscribes.erase(track_alias);
        }

        qserver_vars::subscribe_active[th.track_namespace_hash][th.track_name_hash].erase(
          qserver_vars::SubscribeInfo{ connection_handle, subscribe_id, th.track_fullname_hash });

        if (!qserver_vars::subscribe_active[th.track_namespace_hash][th.track_name_hash].size()) {
            qserver_vars::subscribe_active[th.track_namespace_hash].erase(th.track_name_hash);
        }

        if (!qserver_vars::subscribe_active[th.track_namespace_hash].size()) {
            qserver_vars::subscribe_active.erase(th.track_namespace_hash);
        }

        if (unsub_pub) {
            SPDLOG_INFO("No subscribers left, unsubscribe publisher track_alias: {0}", track_alias);

            auto anno_ns_it = qserver_vars::announce_active.find(th.track_namespace_hash);
            if (anno_ns_it == qserver_vars::announce_active.end()) {
                return;
            }

            for (auto& [pub_connection_handle, tracks] : anno_ns_it->second) {
                if (tracks.find(th.track_fullname_hash) != tracks.end()) {
                    SPDLOG_INFO("Unsubscribe to announcer conn_id: {0} subscribe track_alias: {1}",
                                pub_connection_handle,
                                th.track_fullname_hash);

                    tracks.erase(th.track_fullname_hash); // Add track alias to state

                    auto sub_track_h = qserver_vars::pub_subscribes[th.track_fullname_hash][pub_connection_handle];
                    if (sub_track_h != nullptr) {
                        UnsubscribeTrack(pub_connection_handle, sub_track_h);
                    }
                }
            }
        }
    }

    void SubscribeReceived(quicr::ConnectionHandle connection_handle,
                           uint64_t subscribe_id,
                           uint64_t proposed_track_alias,
                           [[maybe_unused]] quicr::messages::FilterType filter_type,
                           const quicr::FullTrackName& track_full_name,
                           const quicr::SubscribeAttributes& attrs) override
    {
        auto th = quicr::TrackHash(track_full_name);

        SPDLOG_INFO("New subscribe connection handle: {} subscribe_id: {} computed track alias: {} proposed "
                    "track_alias: {} priority: {}",
                    connection_handle,
                    subscribe_id,
                    th.track_fullname_hash,
                    proposed_track_alias,
                    attrs.priority);

        if (qserver_vars::force_track_alias && proposed_track_alias && proposed_track_alias != th.track_fullname_hash) {
            std::ostringstream err;
            err << "Use track alias: " << th.track_fullname_hash;
            ResolveSubscribe(connection_handle,
                             subscribe_id,
                             {
                               quicr::SubscribeResponse::ReasonCode::kRetryTrackAlias,
                               err.str(),
                               th.track_fullname_hash,
                               std::nullopt,
                               std::nullopt,
                             });
            return;
        }

        uint64_t latest_group_id = 0;
        uint64_t latest_object_id = 0;

        auto cache_entry_it = qserver_vars::cache.find(th.track_fullname_hash);
        if (cache_entry_it != qserver_vars::cache.end()) {
            auto& [_, cache] = *cache_entry_it;
            if (const auto& latest_group = cache.Last(); latest_group && !latest_group->empty()) {
                const auto& latest_object = std::prev(latest_group->end());
                latest_group_id = latest_object->headers.group_id;
                latest_object_id = latest_object->headers.object_id;
            }
        }

        ResolveSubscribe(connection_handle,
                         subscribe_id,
                         {
                           quicr::SubscribeResponse::ReasonCode::kOk,
                           std::nullopt,
                           std::nullopt,
                           latest_group_id,
                           latest_object_id,
                         });

        auto pub_track_h =
          std::make_shared<MyPublishTrackHandler>(track_full_name, quicr::TrackMode::kStream, attrs.priority, 50000);
        qserver_vars::subscribes[th.track_fullname_hash][connection_handle] = pub_track_h;
        qserver_vars::subscribe_alias_sub_id[connection_handle][subscribe_id] = th.track_fullname_hash;

        // record subscribe as active from this subscriber
        qserver_vars::subscribe_active[th.track_namespace_hash][th.track_name_hash].emplace(
          qserver_vars::SubscribeInfo{ connection_handle, subscribe_id, th.track_fullname_hash });

        // Create a subscribe track that will be used by the relay to send to subscriber for matching objects
        BindPublisherTrack(connection_handle, subscribe_id, pub_track_h, false);

        // Subscribe to announcer if announcer is active
        auto anno_ns_it = qserver_vars::announce_active.find(th.track_namespace_hash);
        if (anno_ns_it == qserver_vars::announce_active.end()) {
            SPDLOG_INFO("Subscribe to track namespace hash: {0}, does not have any announcements.",
                        th.track_namespace_hash);
            return;
        }

        for (auto& [conn_h, tracks] : anno_ns_it->second) {
            // aggregate subscriptions
            if (tracks.find(th.track_fullname_hash) == tracks.end()) {
                last_subscription_time_ = std::chrono::steady_clock::now();
                SPDLOG_INFO("Sending subscribe to announcer connection handler: {0} subscribe track_alias: {1}",
                            conn_h,
                            th.track_fullname_hash);

                tracks.insert(th.track_fullname_hash); // Add track alias to state

                auto sub_track_h = std::make_shared<MySubscribeTrackHandler>(track_full_name);
                auto copy_sub_track_h = sub_track_h;
                SubscribeTrack(conn_h, sub_track_h);

                SPDLOG_INFO("Sending subscription to announcer connection: {0} hash: {1}, handler: {2}",
                            conn_h,
                            th.track_fullname_hash,
                            sub_track_h->GetFullTrackName().track_alias.value());
                qserver_vars::pub_subscribes[th.track_fullname_hash][conn_h] = copy_sub_track_h;
            } else {
                auto now = std::chrono::steady_clock::now();
                auto elapsed =
                  std::chrono::duration_cast<std::chrono::milliseconds>(now - last_subscription_time_.value()).count();
                if (elapsed > kSubscriptionDampenDurationMs_) {
                    // send subscription update
                    auto& sub_track_h = qserver_vars::pub_subscribes[th.track_fullname_hash][conn_h];
                    if (sub_track_h == nullptr) {

                        return;
                    }
                    SPDLOG_INFO("Sending subscription update to announcer connection: {0} hash: {1}",
                                th.track_namespace_hash,
                                subscribe_id);
                    UpdateTrackSubscription(conn_h, sub_track_h);
                    last_subscription_time_ = std::chrono::steady_clock::now();
                }
            }
        }
    }

    /**
     * @brief Checks the cache for the requested objects.
     *
     * @param connection_handle Source connection ID.
     * @param subscribe_id      Subscribe ID received.
     * @param track_full_name   Track full name
     * @param attrs             Fetch attributes received.
     *
     * @returns true if the range of groups and objects exist in the cache, otherwise returns false.
     */
    bool FetchReceived([[maybe_unused]] quicr::ConnectionHandle connection_handle,
                       [[maybe_unused]] uint64_t subscribe_id,
                       const quicr::FullTrackName& track_full_name,
                       const quicr::FetchAttributes& attrs) override
    {
        SPDLOG_INFO("Received Fetch for conn_id: {} subscribe_id: {} start_group: {} end_group: {}",
                    connection_handle,
                    subscribe_id,
                    attrs.start_group,
                    attrs.end_group);

        const auto th = quicr::TrackHash(track_full_name);

        auto cache_entry_it = qserver_vars::cache.find(th.track_fullname_hash);
        if (cache_entry_it == qserver_vars::cache.end()) {
            SPDLOG_WARN("No cache entry for the hash {}", th.track_fullname_hash);
            return false;
        }

        auto& [_, cache_entry] = *cache_entry_it;

        const auto groups = cache_entry.Get(attrs.start_group, attrs.end_group);

        if (groups.empty()) {
            SPDLOG_WARN("No groups found for requested range");
            return false;
        }

        return std::any_of(groups.begin(), groups.end(), [&](const auto& group) {
            return !group->empty() && group->begin()->headers.object_id <= attrs.start_object &&
                   std::prev(group->end())->headers.object_id >= (attrs.end_object - 1);
        });
    }

    /**
     * @brief Event run on sending FetchOk.
     *
     * @details Event run upon sending a FetchOk to a fetching client. Retrieves the requested objects from the cache
     *          and send them to the requesting client's fetch handler.
     *
     * @param connection_handle Source connection ID.
     * @param subscribe_id      Subscribe ID received.
     * @param track_full_name   Track full name
     * @param attributes        Fetch attributes received.
     */
    void OnFetchOk(quicr::ConnectionHandle connection_handle,
                   uint64_t subscribe_id,
                   const quicr::FullTrackName& track_full_name,
                   const quicr::FetchAttributes& attrs) override
    {
        auto pub_track_h =
          std::make_shared<MyPublishTrackHandler>(track_full_name, quicr::TrackMode::kStream, attrs.priority, 50000);
        BindPublisherTrack(connection_handle, subscribe_id, pub_track_h);

        const auto th = quicr::TrackHash(track_full_name);

        std::thread retrieve_cache_thread(
          [=, cache_entries = qserver_vars::cache.at(th.track_fullname_hash).Get(attrs.start_group, attrs.end_group)] {
              for (const auto& cache_entry : cache_entries) {
                  for (const auto& object : *cache_entry) {
                      if ((object.headers.group_id < attrs.start_group || object.headers.group_id >= attrs.end_group) ||
                          (object.headers.object_id < attrs.start_object ||
                           object.headers.object_id >= attrs.end_object))
                          continue;

                      pub_track_h->PublishObject(object.headers, object.data);
                      std::this_thread::sleep_for(std::chrono::milliseconds(1));
                  }
              }
          });

        retrieve_cache_thread.detach();
    }

    void FetchCancelReceived([[maybe_unused]] quicr::ConnectionHandle connection_handle,
                             [[maybe_unused]] uint64_t subscribe_id) override
    {
        SPDLOG_INFO("Canceling fetch for subscribe_id: {0}", subscribe_id);
    }

  private:
    /// The server cache for fetching from.
    const int kSubscriptionDampenDurationMs_ = 1000;
    std::optional<std::chrono::time_point<std::chrono::steady_clock>> last_subscription_time_;
};

/* -------------------------------------------------------------------------------------------------
 * Main program
 * -------------------------------------------------------------------------------------------------
 */
quicr::ServerConfig
InitConfig(cxxopts::ParseResult& cli_opts)
{
    quicr::ServerConfig config;

    std::string qlog_path;
    if (cli_opts.count("qlog")) {
        qlog_path = cli_opts["qlog"].as<std::string>();
    }

    if (cli_opts.count("debug") && cli_opts["debug"].as<bool>() == true) {
        SPDLOG_INFO("setting debug level");
        spdlog::default_logger()->set_level(spdlog::level::debug);
    }

    if (cli_opts.count("version") && cli_opts["version"].as<bool>() == true) {
        SPDLOG_INFO("QuicR library version: {}", QUICR_VERSION);
        exit(0);
    }

    if (cli_opts.count("relax_track_alias")) {
        qserver_vars::force_track_alias = false;
    }

    config.endpoint_id = cli_opts["endpoint_id"].as<std::string>();

    config.server_bind_ip = cli_opts["bind_ip"].as<std::string>();
    config.server_port = cli_opts["port"].as<uint16_t>();

    config.transport_config.debug = cli_opts["debug"].as<bool>();
    config.transport_config.tls_cert_filename = cli_opts["cert"].as<std::string>();
    config.transport_config.tls_key_filename = cli_opts["key"].as<std::string>();
    config.transport_config.use_reset_wait_strategy = false;
    config.transport_config.time_queue_max_duration = 50000;
    config.transport_config.quic_qlog_path = qlog_path;

    return config;
}

int
main(int argc, char* argv[])
{
    int result_code = EXIT_SUCCESS;

    cxxopts::Options options("qclient",
                             std::string("MOQ Example Server using QuicR Version: ") + std::string(QUICR_VERSION));
    options.set_width(75).set_tab_expansion().allow_unrecognised_options().add_options()("h,help", "Print help")(
      "d,debug", "Enable debugging") // a bool parameter
      ("relax_track_alias", "Set to allow client provided track alias")("v,version",
                                                                        "QuicR Version") // a bool parameter
      ("b,bind_ip", "Bind IP", cxxopts::value<std::string>()->default_value("127.0.0.1"))(
        "p,port", "Listening port", cxxopts::value<uint16_t>()->default_value("1234"))(
        "e,endpoint_id", "This relay/server endpoint ID", cxxopts::value<std::string>()->default_value("moq-server"))(
        "c,cert", "Certificate file", cxxopts::value<std::string>()->default_value("./server-cert.pem"))(
        "k,key", "Certificate key file", cxxopts::value<std::string>()->default_value("./server-key.pem"))(
        "q,qlog", "Enable qlog using path", cxxopts::value<std::string>()); // end of options

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help({ "" }) << std::endl;
        return EXIT_SUCCESS;
    }

    // Install a signal handlers to catch operating system signals
    installSignalHandlers();

    // Lock the mutex so that main can then wait on it
    std::unique_lock<std::mutex> lock(moq_example::main_mutex);

    quicr::ServerConfig config = InitConfig(result);

    try {
        auto server = std::make_shared<MyServer>(config);
        if (server->Start() != quicr::Transport::Status::kReady) {
            SPDLOG_ERROR("Server failed to start");
            exit(-2);
        }

        // Wait until told to terminate
        moq_example::cv.wait(lock, [&]() { return moq_example::terminate; });

        // Unlock the mutex
        lock.unlock();
    } catch (const std::invalid_argument& e) {
        std::cerr << "Invalid argument: " << e.what() << std::endl;
        result_code = EXIT_FAILURE;
    } catch (const std::exception& e) {
        std::cerr << "Unexpected exception: " << e.what() << std::endl;
        result_code = EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Unexpected exception" << std::endl;
        result_code = EXIT_FAILURE;
    }

    return result_code;
}
