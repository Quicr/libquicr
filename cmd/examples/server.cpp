// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <condition_variable>
#include <map>
#include <oss/cxxopts.hpp>
#include <set>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <quicr/cache.h>
#include <quicr/detail/defer.h>
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

    std::mutex state_mutex;

    /**
     * Map of subscribes (e.g., track alias) sent to announcements
     *
     * @example
     *      track_alias_set = announce_active[track_namespace][connection_handle]
     */
    std::map<quicr::TrackNamespace, std::map<quicr::ConnectionHandle, std::set<quicr::messages::TrackAlias>>>
      announce_active;

    /**
     * Active subscriber publish tracks for a given track, indexed (keyed) by track_alias, connection handle
     *
     * @note This indexing intentionally prohibits per connection having more
     *           than one subscribe to a full track name.
     *
     * @example track_handler = subscribes[track_alias][connection_handle]
     */
    std::map<quicr::messages::TrackAlias,
             std::map<quicr::ConnectionHandle, std::shared_ptr<quicr::PublishTrackHandler>>>
      subscribes;

    /**
     * Request ID to alias mapping
     *      Used to lookup the track alias for a given request ID
     *
     * @example
     *      track_alias = subscribe_alias_req_id[conn_id][request_id]
     */
    std::map<quicr::ConnectionHandle, std::map<quicr::messages::RequestID, quicr::messages::TrackAlias>>
      subscribe_alias_req_id;

    /**
     * Map of subscribes set by namespace and track name hash
     *      Set<SubscribeInfo> = subscribe_active[track_namespace][track_name_hash]
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
    std::map<quicr::TrackNamespace, std::map<TrackNameHash, std::set<SubscribeInfo>>> subscribe_active;

    /**
     * Active publisher/announce subscribes that this relay has made to receive objects from publisher.
     *
     * @example
     *      track_delegate = pub_subscribes[track_alias][conn_id]
     */
    std::map<quicr::messages::TrackAlias,
             std::map<quicr::ConnectionHandle, std::shared_ptr<quicr::SubscribeTrackHandler>>>
      pub_subscribes;

    std::map<quicr::ConnectionHandle,
             std::map<quicr::messages::RequestID, std::shared_ptr<quicr::SubscribeTrackHandler>>>
      pub_subscribes_by_req_id;

    /// Subscriber connection handles by subscribe prefix namespace for subscribe announces
    std::map<quicr::TrackNamespace, std::set<quicr::ConnectionHandle>> subscribes_announces;

    /**
     * Cache of MoQ objects by track alias
     */
    std::map<quicr::messages::TrackAlias, quicr::Cache<quicr::messages::GroupId, std::set<CacheObject>>> cache;

    /**
     * Tick Service used by the cache
     */
    std::shared_ptr<quicr::ThreadedTickService> tick_service = std::make_shared<quicr::ThreadedTickService>();

    /**
     * @brief Map of atomic bools to mark if a fetch thread should be interrupted.
     */
    std::map<std::pair<quicr::ConnectionHandle, quicr::messages::RequestID>, std::atomic_bool> stop_fetch;
}

/**
 * @brief  Subscribe track handler
 * @details Subscribe track handler used for the subscribe command line option.
 */
class MySubscribeTrackHandler : public quicr::SubscribeTrackHandler
{
  public:
    MySubscribeTrackHandler(const quicr::FullTrackName& full_track_name, bool is_publisher_initiated = false)
      : SubscribeTrackHandler(full_track_name,
                              3,
                              quicr::messages::GroupOrder::kAscending,
                              quicr::messages::FilterType::kLatestObject,
                              std::nullopt,
                              is_publisher_initiated)
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
            SPDLOG_TRACE("No subscribes, ignoring data size: {0} ", data.size());
            return;
        }

        // Cache Object
        if (qserver_vars::cache.count(*track_alias) == 0) {
            qserver_vars::cache.insert(std::make_pair(*track_alias,
                                                      quicr::Cache<quicr::messages::GroupId, std::set<CacheObject>>{
                                                        50000, 1000, qserver_vars::tick_service }));
        }

        auto& cache_entry = qserver_vars::cache.at(*track_alias);

        CacheObject object{ object_headers, { data.begin(), data.end() } };

        if (auto group = cache_entry.Get(object_headers.group_id)) {
            group->insert(std::move(object));
        } else {
            cache_entry.Insert(object_headers.group_id, { std::move(object) }, 50000);
        }

        // Fan out to all subscribers
        try {
            for (const auto& [conn_id, pth] : sub_it->second) {
                pth->PublishObject(object_headers, data);
            }
        } catch (const std::exception& e) {
            SPDLOG_ERROR("Caught exception trying to publish. (error={})", e.what());
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
 * @brief  Fetch track handler
 * @details Fetch track handler.
 */
class MyFetchTrackHandler : public quicr::FetchTrackHandler
{
    MyFetchTrackHandler(const std::shared_ptr<quicr::PublishFetchHandler> publish_fetch_handler,
                        const quicr::FullTrackName& full_track_name,
                        quicr::messages::SubscriberPriority priority,
                        quicr::messages::GroupOrder group_order,
                        uint64_t start_group,
                        uint64_t start_object,
                        uint64_t end_group,
                        uint64_t end_object)
      : FetchTrackHandler(full_track_name, priority, group_order, start_group, end_group, start_object, end_object)
    {
        publish_fetch_handler_ = publish_fetch_handler;
    }

  public:
    static auto Create(const std::shared_ptr<quicr::PublishFetchHandler> publish_fetch_handler,
                       const quicr::FullTrackName& full_track_name,
                       std::uint8_t priority,
                       quicr::messages::GroupOrder group_order,
                       uint64_t start_group,
                       uint64_t start_object,
                       uint64_t end_group,
                       uint64_t end_object)
    {
        return std::shared_ptr<MyFetchTrackHandler>(new MyFetchTrackHandler(publish_fetch_handler,
                                                                            full_track_name,
                                                                            priority,
                                                                            group_order,
                                                                            start_group,
                                                                            end_group,
                                                                            start_object,
                                                                            end_object));
    }

    void ObjectReceived(const quicr::ObjectHeaders& headers, quicr::BytesSpan data) override
    {
        // Simple - forward what we get to the fetch handler
        if (publish_fetch_handler_) {
            publish_fetch_handler_->PublishObject(headers, data);
        }
    }

    void StatusChanged(Status status) override
    {
        switch (status) {
            case Status::kOk: {
                if (auto track_alias = GetTrackAlias(); track_alias.has_value()) {
                    SPDLOG_INFO("Track alias: {0} is ready to read", track_alias.value());
                }
            } break;

            case Status::kError: {
                SPDLOG_INFO("Fetch failed");
                break;
            }
            default:
                break;
        }
    }

  private:
    std::shared_ptr<quicr::PublishFetchHandler> publish_fetch_handler_;
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

    std::vector<quicr::ConnectionHandle> UnannounceReceived(quicr::ConnectionHandle connection_handle,
                                                            const quicr::TrackNamespace& track_namespace) override
    {
        auto th = quicr::TrackHash({ track_namespace, {} });

        SPDLOG_DEBUG("Received unannounce from connection handle: {0} for namespace hash: {1}, removing all tracks "
                     "associated with namespace",
                     connection_handle,
                     th.track_namespace_hash);

        std::vector<quicr::ConnectionHandle> sub_annos_connections;

        // TODO: Fix O(prefix namespaces) matching
        for (const auto& [ns, conns] : qserver_vars::subscribes_announces) {
            if (!ns.HasSamePrefix(track_namespace)) {
                continue;
            }

            for (auto sub_conn_handle : conns) {
                SPDLOG_DEBUG(
                  "Received unannounce matches prefix subscribed from connection handle: {} for namespace hash: {}",
                  sub_conn_handle,
                  th.track_namespace_hash);

                sub_annos_connections.emplace_back(sub_conn_handle);
            }
        }

        for (auto track_alias : qserver_vars::announce_active[track_namespace][connection_handle]) {
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

        qserver_vars::announce_active[track_namespace].erase(connection_handle);
        if (qserver_vars::announce_active[track_namespace].empty()) {
            qserver_vars::announce_active.erase(track_namespace);
        }

        return sub_annos_connections;
    }

    void UnsubscribeAnnouncesReceived(quicr::ConnectionHandle connection_handle,
                                      const quicr::TrackNamespace& prefix_namespace) override
    {
        auto it = qserver_vars::subscribes_announces.find(prefix_namespace);
        if (it == qserver_vars::subscribes_announces.end()) {
            return;
        }

        auto th = quicr::TrackHash({ prefix_namespace, {} });
        SPDLOG_INFO("Unsubscribe announces received connection handle: {} for namespace_hash: {}, removing",
                    connection_handle,
                    th.track_namespace_hash);
    }

    std::pair<std::optional<quicr::messages::SubscribeAnnouncesErrorCode>, std::vector<quicr::TrackNamespace>>
    SubscribeAnnouncesReceived(quicr::ConnectionHandle connection_handle,
                               const quicr::TrackNamespace& prefix_namespace,
                               const quicr::PublishAnnounceAttributes&) override
    {
        auto th = quicr::TrackHash({ prefix_namespace, {} });

        auto [it, is_new] = qserver_vars::subscribes_announces.try_emplace(prefix_namespace);
        it->second.insert(connection_handle);

        if (is_new) {
            SPDLOG_INFO("Subscribe announces received connection handle: {} for namespace_hash: {}, adding to state",
                        connection_handle,
                        th.track_namespace_hash);
        }

        std::vector<quicr::TrackNamespace> matched_ns;

        // TODO: Fix O(prefix namespaces) matching
        for (const auto& [ns, _] : qserver_vars::announce_active) {
            if (ns.HasSamePrefix(prefix_namespace)) {
                matched_ns.push_back(ns);
            }
        }

        return { std::nullopt, std::move(matched_ns) };
    }

    void PublishReceived(quicr::ConnectionHandle connection_handle,
                         uint64_t request_id,
                         const quicr::FullTrackName& track_full_name,
                         const quicr::messages::SubscribeAttributes& subscribe_attributes) override
    {
        auto th = quicr::TrackHash(track_full_name);

        SPDLOG_INFO("Received publish from connection handle: {} using track alias: {} request_id: {}",
                    connection_handle,
                    th.track_fullname_hash,
                    request_id);

        quicr::PublishResponse publish_response;
        publish_response.reason_code = quicr::PublishResponse::ReasonCode::kOk;

        // passively create the subscribe handler towards the publisher
        auto sub_track_handler = std::make_shared<MySubscribeTrackHandler>(track_full_name, true);

        sub_track_handler->SetRequestId(request_id);
        sub_track_handler->SetReceivedTrackAlias(subscribe_attributes.track_alias.value());
        sub_track_handler->SetPriority(subscribe_attributes.priority);

        SubscribeTrack(connection_handle, sub_track_handler);
        qserver_vars::pub_subscribes[th.track_fullname_hash][connection_handle] = sub_track_handler;
        qserver_vars::pub_subscribes_by_req_id[connection_handle][request_id] = sub_track_handler;

        ResolvePublish(connection_handle,
                       request_id,
                       true,
                       subscribe_attributes.priority,
                       subscribe_attributes.group_order,
                       publish_response);

        // Check if there are any subscribers
        if (qserver_vars::subscribes[th.track_fullname_hash].empty()) {
            SPDLOG_INFO("No subscribers, pause publish connection handle: {0} using track alias: {1}",
                        connection_handle,
                        th.track_fullname_hash);

            sub_track_handler->Pause();
        }
    }

    void AnnounceReceived(quicr::ConnectionHandle connection_handle,
                          const quicr::TrackNamespace& track_namespace,
                          const quicr::PublishAnnounceAttributes& attrs) override
    {
        auto th = quicr::TrackHash({ track_namespace, {} });

        SPDLOG_INFO("Received announce from connection handle: {0} for namespace_hash: {1}",
                    connection_handle,
                    th.track_namespace_hash);

        // Add to state if not exist
        auto [anno_conn_it, is_new] = qserver_vars::announce_active[track_namespace].try_emplace(connection_handle);

        if (!is_new) {
            SPDLOG_INFO("Received announce from connection handle: {} for namespace hash: {} is duplicate, ignoring",
                        connection_handle,
                        th.track_namespace_hash);
            return;
        }

        AnnounceResponse announce_response;
        announce_response.reason_code = quicr::Server::AnnounceResponse::ReasonCode::kOk;

        auto& anno_tracks = qserver_vars::announce_active[track_namespace][connection_handle];

        std::vector<quicr::ConnectionHandle> sub_annos_connections;

        // TODO: Fix O(prefix namespaces) matching
        for (const auto& [ns, conns] : qserver_vars::subscribes_announces) {
            if (!ns.HasSamePrefix(track_namespace)) {
                continue;
            }

            for (auto sub_conn_handle : conns) {
                SPDLOG_DEBUG(
                  "Received announce matches prefix subscribed from connection handle: {} for namespace hash: {}",
                  sub_conn_handle,
                  th.track_namespace_hash);

                sub_annos_connections.emplace_back(sub_conn_handle);
            }
        }

        ResolveAnnounce(connection_handle, attrs.request_id, track_namespace, sub_annos_connections, announce_response);

        // Check if there are any subscribes. If so, send subscribe to announce for all tracks matching namespace
        for (const auto& [ns, sub_tracks] : qserver_vars::subscribe_active) {
            if (!ns.HasSamePrefix(track_namespace)) {
                continue;
            }

            for (const auto& [track_name, si] : sub_tracks) {
                if (not si.empty()) { // Have subscribes
                    auto& a_si = *si.begin();
                    if (anno_tracks.find(a_si.track_alias) == anno_tracks.end()) {
                        SPDLOG_INFO("Sending subscribe to announcer connection handle: {0} subscribe track_alias: {1}",
                                    connection_handle,
                                    a_si.track_alias);

                        anno_tracks.insert(a_si.track_alias); // Add track to state

                        const auto pub_track_h = qserver_vars::subscribes[a_si.track_alias][a_si.connection_handle];

                        auto sub_track_handler =
                          std::make_shared<MySubscribeTrackHandler>(pub_track_h->GetFullTrackName());
                        sub_track_handler->SetTrackAlias(
                          a_si.track_alias); // Publisher handler may have different track alias

                        SubscribeTrack(connection_handle, sub_track_handler);
                        qserver_vars::pub_subscribes[a_si.track_alias][connection_handle] = sub_track_handler;
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

            // Remove all subscribe announces for this connection handle
            std::vector<quicr::TrackNamespace> remove_ns;
            for (auto& [ns, conns] : qserver_vars::subscribes_announces) {
                auto it = conns.find(connection_handle);
                if (it != conns.end()) {
                    conns.erase(it);
                    if (conns.empty()) {
                        remove_ns.emplace_back(ns);
                    }
                }
            }

            for (auto ns : remove_ns) {
                qserver_vars::subscribes_announces.erase(ns);
            }
        }

        // Remove active subscribes
        std::vector<quicr::messages::RequestID> subscribe_ids;
        auto ta_conn_it = qserver_vars::subscribe_alias_req_id.find(connection_handle);
        if (ta_conn_it != qserver_vars::subscribe_alias_req_id.end()) {
            for (const auto& [sub_id, _] : ta_conn_it->second) {
                subscribe_ids.push_back(sub_id);
            }
        }

        for (const auto& sub_id : subscribe_ids) {
            UnsubscribeReceived(connection_handle, sub_id);
        }
    }

    ClientSetupResponse ClientSetupReceived(quicr::ConnectionHandle,
                                            const quicr::ClientSetupAttributes& client_setup_attributes) override
    {
        ClientSetupResponse client_setup_response;

        SPDLOG_INFO("Client setup received from endpoint_id: {0}", client_setup_attributes.endpoint_id);

        return client_setup_response;
    }

    void SubscribeDoneReceived(quicr::ConnectionHandle connection_handle, uint64_t request_id) override
    {
        SPDLOG_INFO("Subscribe Done connection handle: {0} request_id: {1}", connection_handle, request_id);

        std::lock_guard<std::mutex> _(qserver_vars::state_mutex);
        auto req_it = qserver_vars::pub_subscribes_by_req_id.find(connection_handle);

        if (req_it == qserver_vars::pub_subscribes_by_req_id.end()) {
            SPDLOG_WARN("Subscribe Done connection handle: {0} request_id: {1} does not have a connection entry in "
                        "state, ignoring",
                        connection_handle,
                        request_id);
            return;
        }

        auto sub_it = req_it->second.find(request_id);
        if (sub_it == req_it->second.end()) {
            SPDLOG_WARN(
              "Subscribe Done connection handle: {0} request_id: {1} does not matching existing state, ignoring",
              connection_handle,
              request_id);
            return;
        }

        auto th = quicr::TrackHash(sub_it->second->GetFullTrackName());
        qserver_vars::pub_subscribes[th.track_fullname_hash].erase(connection_handle);

        req_it->second.erase(sub_it);

        if (req_it->second.empty()) {
            qserver_vars::pub_subscribes_by_req_id.erase(req_it);
        }
    }

    void UnsubscribeReceived(quicr::ConnectionHandle connection_handle, uint64_t request_id) override
    {
        SPDLOG_INFO("Unsubscribe received connection handle: {0} subscribe_id: {1}", connection_handle, request_id);

        auto ta_conn_it = qserver_vars::subscribe_alias_req_id.find(connection_handle);
        if (ta_conn_it == qserver_vars::subscribe_alias_req_id.end()) {
            SPDLOG_WARN("Unable to find track alias connection for connection handle: {0} request_id: {1}",
                        connection_handle,
                        request_id);
            return;
        }

        auto ta_it = ta_conn_it->second.find(request_id);
        if (ta_it == ta_conn_it->second.end()) {
            SPDLOG_WARN(
              "Unable to find track alias for connection handle: {0} request_id: {1}", connection_handle, request_id);
            return;
        }

        std::lock_guard<std::mutex> _(qserver_vars::state_mutex);

        auto track_alias = ta_it->second;

        ta_conn_it->second.erase(ta_it);
        if (!ta_conn_it->second.size()) {
            qserver_vars::subscribe_alias_req_id.erase(ta_conn_it);
        }

        auto& track_h = qserver_vars::subscribes[track_alias][connection_handle];

        if (track_h == nullptr) {
            SPDLOG_WARN("Unsubscribe unable to find track delegate for connection handle: {0} request_id: {1}",
                        connection_handle,
                        request_id);
            return;
        }

        const auto& tfn = track_h->GetFullTrackName();
        auto th = quicr::TrackHash(tfn);

        qserver_vars::subscribes[track_alias].erase(connection_handle);
        bool unsub_pub{ false };
        if (!qserver_vars::subscribes[track_alias].size()) {
            unsub_pub = true;
            qserver_vars::subscribes.erase(track_alias);
        }

        qserver_vars::subscribe_active[tfn.name_space][th.track_name_hash].erase(
          qserver_vars::SubscribeInfo{ connection_handle, request_id, th.track_fullname_hash });

        if (!qserver_vars::subscribe_active[tfn.name_space][th.track_name_hash].size()) {
            qserver_vars::subscribe_active[tfn.name_space].erase(th.track_name_hash);
        }

        if (!qserver_vars::subscribe_active[tfn.name_space].size()) {
            qserver_vars::subscribe_active.erase(tfn.name_space);
        }

        if (unsub_pub) {
            SPDLOG_INFO("No subscribers left, unsubscribe publisher track_alias: {0}", track_alias);

            // Pause publisher for PUBLISH initiated subscribes
            for (const auto& [pub_connection_handle, handler] : qserver_vars::pub_subscribes[track_alias]) {
                if (handler->IsPublisherInitiated()) {
                    handler->Pause();
                }
            }

            auto anno_ns_it = qserver_vars::announce_active.find(tfn.name_space);
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

                    qserver_vars::pub_subscribes[th.track_fullname_hash].erase(pub_connection_handle);
                }
            }

            if (qserver_vars::pub_subscribes[th.track_fullname_hash].empty()) {
                qserver_vars::pub_subscribes.erase(th.track_fullname_hash);
            }
        }
    }

    void SubscribeReceived(quicr::ConnectionHandle connection_handle,
                           uint64_t request_id,
                           [[maybe_unused]] quicr::messages::FilterType filter_type,
                           const quicr::FullTrackName& track_full_name,
                           const quicr::messages::SubscribeAttributes& attrs) override
    {
        auto th = quicr::TrackHash(track_full_name);

        SPDLOG_INFO("New subscribe connection handle: {} request_id: {} track alias: {} "
                    "priority: {}",
                    connection_handle,
                    request_id,
                    th.track_fullname_hash,
                    attrs.priority);

        std::optional<quicr::messages::Location> largest_location = std::nullopt;

        auto cache_entry_it = qserver_vars::cache.find(th.track_fullname_hash);
        if (cache_entry_it != qserver_vars::cache.end()) {
            auto& [_, cache] = *cache_entry_it;
            if (const auto& latest_group = cache.Last(); latest_group && !latest_group->empty()) {
                const auto& latest_object = std::prev(latest_group->end());
                largest_location = { latest_object->headers.group_id, latest_object->headers.object_id };
            }
        }

        const std::uint32_t ttl =
          attrs.delivery_timeout != std::chrono::milliseconds::zero() ? attrs.delivery_timeout.count() : 50000;

        const auto pub_track_h =
          std::make_shared<MyPublishTrackHandler>(track_full_name, quicr::TrackMode::kStream, attrs.priority, ttl);

        const auto track_alias = th.track_fullname_hash;

        ResolveSubscribe(connection_handle,
                         request_id,
                         track_alias,
                         {
                           quicr::SubscribeResponse::ReasonCode::kOk,
                           std::nullopt,
                           largest_location,
                         });

        qserver_vars::subscribes[track_alias][connection_handle] = pub_track_h;
        qserver_vars::subscribe_alias_req_id[connection_handle][request_id] = track_alias;

        // record subscribe as active from this subscriber
        qserver_vars::subscribe_active[track_full_name.name_space][th.track_name_hash].emplace(
          qserver_vars::SubscribeInfo{ connection_handle, request_id, track_alias });

        // Create a subscribe track that will be used by the relay to send to subscriber for matching objects
        BindPublisherTrack(connection_handle, request_id, pub_track_h, false);

        // Resume publishers
        for (const auto& [pub_connection_handle, handler] : qserver_vars::pub_subscribes[track_alias]) {
            if (handler->IsPublisherInitiated()) {
                handler->Resume();
            }
        }

        // Subscribe to announcer if announcer is active
        bool success = false;
        for (auto& [ns, conns] : qserver_vars::announce_active) {
            if (!ns.HasSamePrefix(track_full_name.name_space)) {
                continue;
            }
            success = true;

            // Loop through connection handles
            for (auto& [conn_h, tracks] : conns) {
                // aggregate subscriptions
                if (tracks.find(track_alias) == tracks.end()) {
                    last_subscription_time_ = std::chrono::steady_clock::now();
                    SPDLOG_INFO("Sending subscribe to announcer connection handler: {0} subscribe track_alias: {1}",
                                conn_h,
                                track_alias);

                    tracks.insert(track_alias); // Add track alias to state

                    auto sub_track_h = std::make_shared<MySubscribeTrackHandler>(track_full_name);
                    auto copy_sub_track_h = sub_track_h;
                    SubscribeTrack(conn_h, sub_track_h);

                    SPDLOG_INFO("Sending subscription to announcer connection: {0} hash: {1}, handler: {2}",
                                conn_h,
                                th.track_fullname_hash,
                                track_alias);
                    qserver_vars::pub_subscribes[1][conn_h] = copy_sub_track_h;
                } else {
                    if (!last_subscription_time_.has_value()) {
                        last_subscription_time_ = std::chrono::steady_clock::now();
                    }
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed =
                      std::chrono::duration_cast<std::chrono::milliseconds>(now - last_subscription_time_.value())
                        .count();
                    if (elapsed > kSubscriptionDampenDurationMs_) {
                        // send subscription update
                        auto& sub_track_h = qserver_vars::pub_subscribes[track_alias][conn_h];
                        if (sub_track_h == nullptr) {
                            return;
                        }
                        SPDLOG_INFO("Sending subscription update to announcer connection: hash: {0} request: {1}",
                                    th.track_namespace_hash,
                                    request_id);
                        UpdateTrackSubscription(conn_h, sub_track_h);
                        last_subscription_time_ = std::chrono::steady_clock::now();
                    }
                }
            }
        }

        if (not success) {
            SPDLOG_INFO("Subscribe to track namespace hash: {0}, does not have any announcements.",
                        th.track_namespace_hash);
        }
    }

    std::optional<quicr::messages::Location> GetLargestAvailable(const quicr::FullTrackName& track_name) override
    {
        // Get the largest object from the cache.
        std::optional<quicr::messages::Location> largest_location = std::nullopt;

        const auto& th = quicr::TrackHash(track_name);
        const auto cache_entry_it = qserver_vars::cache.find(th.track_fullname_hash);
        if (cache_entry_it != qserver_vars::cache.end()) {
            auto& [_, cache] = *cache_entry_it;
            if (const auto& latest_group = cache.Last(); latest_group && !latest_group->empty()) {
                const auto& latest_object = std::prev(latest_group->end());
                largest_location = { latest_object->headers.group_id, latest_object->headers.object_id };
            }
        }

        return largest_location;
    }

    bool FetchReceived(quicr::ConnectionHandle connection_handle,
                       uint64_t request_id,
                       const quicr::FullTrackName& track_full_name,
                       const quicr::messages::FetchAttributes& attributes) override
    {
        // lookup Announcer/Publisher for this Fetch request
        auto anno_ns_it = qserver_vars::announce_active.find(track_full_name.name_space);

        if (anno_ns_it == qserver_vars::announce_active.end()) {
            return false;
        }

        auto setup_fetch_handler = [&](quicr::ConnectionHandle pub_connection_handle) {
            auto pub_fetch_h = quicr::PublishFetchHandler::Create(
              track_full_name, attributes.priority, request_id, attributes.group_order, 50000);
            BindFetchTrack(connection_handle, pub_fetch_h);

            auto fetch_track_handler =
              MyFetchTrackHandler::Create(pub_fetch_h,
                                          track_full_name,
                                          attributes.priority,
                                          attributes.group_order,
                                          attributes.start_location.group,
                                          attributes.start_location.object,
                                          attributes.end_group,
                                          attributes.end_object.has_value() ? attributes.end_object.value() : 0);

            FetchTrack(pub_connection_handle, fetch_track_handler);
            return true;
        };

        // Handle announcer case
        for (auto& [pub_connection_handle, _] : anno_ns_it->second) {
            return setup_fetch_handler(pub_connection_handle);
        }

        return false;
    }

    bool OnFetchOk(quicr::ConnectionHandle connection_handle,
                   uint64_t subscribe_id,
                   const quicr::FullTrackName& track_full_name,
                   const quicr::messages::FetchAttributes& attrs) override
    {
        auto pub_fetch_h =
          quicr::PublishFetchHandler::Create(track_full_name, attrs.priority, subscribe_id, attrs.group_order, 50000);
        BindFetchTrack(connection_handle, pub_fetch_h);

        const auto th = quicr::TrackHash(track_full_name);

        qserver_vars::stop_fetch.try_emplace({ connection_handle, subscribe_id }, false);

        const auto cache_entries = [&] {
            std::lock_guard lock(moq_example::main_mutex);
            return qserver_vars::cache.at(th.track_fullname_hash).Get(attrs.start_location.group, attrs.end_group + 1);
        }();

        if (cache_entries.empty())
            return false;

        std::thread retrieve_cache_thread([=, this, cache_entries = cache_entries] {
            defer(UnbindFetchTrack(connection_handle, pub_fetch_h));

            for (const auto& cache_entry : cache_entries) {
                for (const auto& object : *cache_entry) {
                    if (qserver_vars::stop_fetch[{ connection_handle, subscribe_id }]) {
                        qserver_vars::stop_fetch.erase({ connection_handle, subscribe_id });
                        return;
                    }

                    /*
                     * Stop when reached end group and end object, unless end object is zero. End object of
                     * zero indicates all objects within end group
                     */
                    if (attrs.end_object.has_value() && *attrs.end_object != 0 &&
                        object.headers.group_id == attrs.end_group && object.headers.object_id > *attrs.end_object)
                        break; // Done, reached end object within end group

                    SPDLOG_DEBUG("Fetching group: {} object: {}", object.headers.group_id, object.headers.object_id);

                    try {
                        pub_fetch_h->PublishObject(object.headers, object.data);
                    } catch (const std::exception& e) {
                        SPDLOG_ERROR("Caught exception trying to publish. (error={})", e.what());
                    }
                }
            }
        });

        retrieve_cache_thread.detach();
        return true;
    }

    void FetchCancelReceived(quicr::ConnectionHandle connection_handle, uint64_t subscribe_id) override
    {
        SPDLOG_INFO("Canceling fetch for connection handle: {} subscribe_id: {}", connection_handle, subscribe_id);

        if (qserver_vars::stop_fetch.count({ connection_handle, subscribe_id }) == 0)
            qserver_vars::stop_fetch[{ connection_handle, subscribe_id }] = true;
    }

    void NewGroupRequested(quicr::ConnectionHandle conn_id, uint64_t subscribe_id, uint64_t track_alias) override
    {
        for (auto [_, handler] : qserver_vars::pub_subscribes[track_alias]) {
            if (!handler) {
                continue;
            }
            SPDLOG_DEBUG(
              "Received New Group Request for conn: {} sub_id: {} track_alias: {}", conn_id, subscribe_id, track_alias);
            handler->RequestNewGroup();
        }
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

    if (cli_opts.count("ssl_keylog") && cli_opts["ssl_keylog"].as<bool>() == true) {
        SPDLOG_INFO("SSL Keylog enabled");
    }

    config.endpoint_id = cli_opts["endpoint_id"].as<std::string>();

    config.server_bind_ip = cli_opts["bind_ip"].as<std::string>();
    config.server_port = cli_opts["port"].as<uint16_t>();

    config.transport_config.debug = cli_opts["debug"].as<bool>();
    config.transport_config.ssl_keylog = cli_opts["ssl_keylog"].as<bool>();
    config.transport_config.tls_cert_filename = cli_opts["cert"].as<std::string>();
    config.transport_config.tls_key_filename = cli_opts["key"].as<std::string>();
    config.transport_config.use_reset_wait_strategy = false;
    config.transport_config.time_queue_max_duration = 50000;
    config.transport_config.quic_qlog_path = qlog_path;
    config.transport_config.max_connections = 1000;

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
      ("v,version", "QuicR Version") // a bool parameter
      ("b,bind_ip", "Bind IP", cxxopts::value<std::string>()->default_value("127.0.0.1"))(
        "p,port", "Listening port", cxxopts::value<uint16_t>()->default_value("1234"))(
        "e,endpoint_id", "This relay/server endpoint ID", cxxopts::value<std::string>()->default_value("moq-server"))(
        "c,cert", "Certificate file", cxxopts::value<std::string>()->default_value("./server-cert.pem"))(
        "k,key", "Certificate key file", cxxopts::value<std::string>()->default_value("./server-key.pem"))(
        "q,qlog", "Enable qlog using path", cxxopts::value<std::string>())(
        "s,ssl_keylog", "Enable SSL Keylog for transport debugging"); // end of options

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
