// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "helper_functions.h"
#include "signal_handler.h"

#include <nlohmann/json.hpp>
#include <oss/cxxopts.hpp>
#include <quicr/cache.h>
#include <quicr/client.h>
#include <quicr/defer.h>
#include <quicr/object.h>
#include <quicr/publish_fetch_handler.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>

using json = nlohmann::json; // NOLINT

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

namespace qclient_vars {
    bool publish_clock{ false };
    std::optional<uint64_t> track_alias; /// Track alias to use for subscribe
    bool record = false;
    bool playback = false;
    std::optional<uint64_t> new_group_request_id;
    bool add_gaps = false;
    bool req_track_status = false;
    bool subgroup_test{ false };
    std::size_t subgroup_test_num_groups{ 2 };
    std::size_t subgroup_test_num_subgroups{ 3 };
    std::size_t subgroup_test_messages_per_phase{ 10 };
    std::chrono::milliseconds subgroup_test_interval_ms(100);
    std::chrono::milliseconds playback_speed_ms(20);
    std::chrono::milliseconds cache_duration_ms(180000);
    std::unordered_map<quicr::messages::TrackAlias, quicr::Cache<quicr::messages::GroupId, std::set<CacheObject>>>
      cache;
    std::shared_ptr<quicr::ThreadedTickService> tick_service = std::make_shared<quicr::ThreadedTickService>();

}

namespace qclient_consts {
    const std::filesystem::path kMoqDataDir = std::filesystem::current_path() / "moq_data";
}

namespace {
    // TODO: Escape/drop invalid filename characters.
    std::string ToString(const quicr::FullTrackName& ftn)
    {
        std::string str;
        const auto& entries = ftn.name_space.GetEntries();
        for (const auto& entry : entries) {
            str += std::string(entry.begin(), entry.end()) + '_';
        }

        str += std::string(ftn.name.begin(), ftn.name.end());

        return str;
    }
}

namespace base64 {
    // From https://gist.github.com/williamdes/308b95ac9ef1ee89ae0143529c361d37;

    constexpr std::string_view kValues = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; //=
    static std::string Encode(const std::string& in)
    {
        std::string out;

        int val = 0;
        int valb = -6;

        for (std::uint8_t c : in) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                out += kValues[(val >> valb) & 0x3F];
                valb -= 6;
            }
        }

        if (valb > -6) {
            out += kValues[((val << 8) >> (valb + 8)) & 0x3F];
        }

        while (out.size() % 4) {
            out += '=';
        }

        return out;
    }

    [[maybe_unused]] static std::string Decode(const std::string& in)
    {
        std::string out;

        std::vector<int> values(256, -1);
        for (int i = 0; i < 64; i++) {
            values[kValues[i]] = i;
        }

        int val = 0;
        int valb = -8;

        for (std::uint8_t c : in) {
            if (values[c] == -1) {
                break;
            }

            val = (val << 6) + values[c];
            valb += 6;

            if (valb >= 0) {
                out += char((val >> valb) & 0xFF);
                valb -= 8;
            }
        }

        return out;
    }
}

/**
 * @brief  Subscribe track handler
 * @details Subscribe track handler used for the subscribe command line option.
 */
class MySubscribeTrackHandler : public quicr::SubscribeTrackHandler
{
  public:
    MySubscribeTrackHandler(const quicr::FullTrackName& full_track_name,
                            quicr::messages::FilterType filter_type,
                            const std::optional<JoiningFetch>& joining_fetch,
                            bool publisher_initiated = false,
                            const std::filesystem::path& dir = qclient_consts::kMoqDataDir)
      : SubscribeTrackHandler(full_track_name,
                              128,
                              quicr::messages::GroupOrder::kAscending,
                              filter_type,
                              joining_fetch,
                              publisher_initiated)
    {
        if (qclient_vars::record) {
            std::filesystem::create_directory(dir);

            const std::string name_str = ToString(full_track_name);
            data_fs_.open(dir / (name_str + ".dat"), std::ios::in | std::ios::out | std::ios::trunc);

            moq_fs_.open(dir / (name_str + ".moq"), std::ios::in | std::ios::out | std::ios::trunc);
            moq_fs_ << json::array();
        }
    }

    virtual ~MySubscribeTrackHandler()
    {
        data_fs_ << std::endl;
        data_fs_.close();

        moq_fs_ << std::endl;
        moq_fs_.close();
    }

    void ObjectReceived(const quicr::ObjectHeaders& hdr, quicr::BytesSpan data) override
    {
        if (qclient_vars::record) {
            RecordObject(GetFullTrackName(), hdr, data);
        }

        std::stringstream ext;

        if (hdr.extensions) {
            ext << "mutable hdrs: ";

            for (const auto& [type, values] : hdr.extensions.value()) {
                for (const auto& value : values) {
                    ext << std::hex << std::setfill('0') << std::setw(2) << type;
                    ext << " = " << std::dec << std::setw(0) << uint64_t(quicr::UintVar(value)) << " ";
                }
            }
        }

        if (hdr.immutable_extensions) {
            ext << "immutable hdrs: ";

            for (const auto& [type, values] : hdr.immutable_extensions.value()) {
                for (const auto& value : values) {
                    ext << std::hex << std::setfill('0') << std::setw(2) << type;
                    ext << " = " << std::dec << std::setw(0) << uint64_t(quicr::UintVar(value)) << " ";
                }
            }
        }

        std::string msg(data.begin(), data.end());

        SPDLOG_INFO("Received message: {} Group:{}, Subgroup: {} Object:{} - {}",
                    ext.str(),
                    hdr.group_id,
                    hdr.subgroup_id,
                    hdr.object_id,
                    msg);

        if (qclient_vars::new_group_request_id.has_value() && not new_group_requested_) {
            SPDLOG_INFO("Track alias: {} requesting new group {}",
                        GetTrackAlias().value(),
                        qclient_vars::new_group_request_id.value());
            RequestNewGroup(qclient_vars::new_group_request_id.value());
            new_group_requested_ = true;
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

            default:
                break;
        }
    }

  private:
    void RecordObject(const quicr::FullTrackName& ftn, const quicr::ObjectHeaders& hdr, quicr::BytesSpan data)
    {
        const std::size_t data_offset = data_fs_.tellp();
        data_fs_ << std::string(data.begin(), data.end());

        std::vector<std::string> ns_entries;
        for (const auto& entry : ftn.name_space.GetEntries()) {
            ns_entries.push_back(base64::Encode({ entry.begin(), entry.end() }));
        }

        const std::string name_str = ToString(GetFullTrackName());
        const std::string data_filename = name_str + ".dat";

        json j;
        j["nameSpace"] = ns_entries;
        j["trackName"] = base64::Encode(std::string(ftn.name.begin(), ftn.name.end()));
        j["objectID"] = hdr.object_id;
        j["groupID"] = hdr.group_id;
        j["subGroup"] = hdr.subgroup_id;
        j["publisherPriority"] = hdr.priority.value();
        j["maxCacheDuration"] = 0;
        j["publisherDeliveryTimeout"] = 0;
        j["receiveTime"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::high_resolution_clock::now().time_since_epoch())
                             .count();
        j["dataFile"] = data_filename;
        j["dataOffset"] = data_offset;
        j["dataLength"] = hdr.payload_length;

        moq_fs_.clear();
        moq_fs_.seekg(0);
        json moq_j = json::parse(moq_fs_);
        moq_j.push_back(j);

        moq_fs_.clear();
        moq_fs_.seekg(0);
        moq_fs_ << moq_j.dump();
    }

  private:
    std::ofstream data_fs_;
    std::fstream moq_fs_;
    bool new_group_requested_ = false;
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
        auto track_alias_opt = GetTrackAlias();
        if (!track_alias_opt.has_value()) {
            SPDLOG_WARN("StatusChanged called but track alias not available, status: {}", static_cast<int>(status));
            return;
        }
        const auto alias = track_alias_opt.value();
        switch (status) {
            case Status::kOk: {
                SPDLOG_INFO("Publish track alias: {0} is ready to send", alias);
                break;
            }
            case Status::kNoSubscribers: {
                SPDLOG_INFO("Publish track alias: {0} has no subscribers", alias);
                break;
            }
            case Status::kNewGroupRequested: {
                SPDLOG_INFO("Publish track alias: {0} has new group request", alias);
                break;
            }
            case Status::kSubscriptionUpdated: {
                SPDLOG_INFO("Publish track alias: {0} has updated subscription", alias);
                break;
            }
            case Status::kPaused: {
                SPDLOG_INFO("Publish track alias: {0} is paused", alias);
                break;
            }
            case Status::kPendingPublishOk: {
                SPDLOG_INFO("Publish track alias: {0} is pending publish ok", alias);
                break;
            }

            default:
                SPDLOG_INFO("Publish track alias: {0} has status {1}", alias, static_cast<int>(status));
                break;
        }
    }

    PublishObjectStatus PublishObject(const quicr::ObjectHeaders& object_headers, quicr::BytesSpan data) override
    {
        auto track_alias = GetTrackAlias();

        // Cache Object
        if (!qclient_vars::cache.contains(*track_alias)) {
            qclient_vars::cache.emplace(
              *track_alias,
              quicr::Cache<quicr::messages::GroupId, std::set<CacheObject>>{
                static_cast<std::size_t>(qclient_vars::cache_duration_ms.count()), 1000, qclient_vars::tick_service });
        }

        CacheObject object{ object_headers, { data.begin(), data.end() } };

        if (auto group = qclient_vars::cache.at(*track_alias).Get(object_headers.group_id)) {
            group->insert(std::move(object));
        } else {
            qclient_vars::cache.at(*track_alias)
              .Insert(object_headers.group_id, { std::move(object) }, qclient_vars ::cache_duration_ms.count());
        }

        return quicr::PublishTrackHandler::PublishObject(object_headers, data);
    }
};

class MyFetchTrackHandler : public quicr::FetchTrackHandler
{
    MyFetchTrackHandler(const quicr::FullTrackName& full_track_name,
                        uint64_t start_group,
                        uint64_t start_object,
                        uint64_t end_group,
                        uint64_t end_object)
      : FetchTrackHandler(full_track_name,
                          3,
                          quicr::messages::GroupOrder::kAscending,
                          start_group,
                          end_group,
                          start_object,
                          end_object)
    {
    }

  public:
    static auto Create(const quicr::FullTrackName& full_track_name,
                       uint64_t start_group,
                       uint64_t start_object,
                       uint64_t end_group,
                       uint64_t end_object)
    {
        return std::shared_ptr<MyFetchTrackHandler>(
          new MyFetchTrackHandler(full_track_name, start_group, start_object, end_group, end_object));
    }

    void ObjectReceived(const quicr::ObjectHeaders& headers, quicr::BytesSpan data) override
    {
        std::string msg(data.begin(), data.end());
        SPDLOG_INFO(
          "Received fetched object group_id: {} object_id: {} value: {}", headers.group_id, headers.object_id, msg);
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
            case Status::kDoneByFin: {
                SPDLOG_INFO("Fetch completed");
                break;
            }

            case Status::kDoneByReset: {
                SPDLOG_INFO("Fetch failed");
                break;
            }
            default:
                break;
        }
    }
};

class MySubscribeNamespaceHandler : public quicr::SubscribeNamespaceHandler
{
    MySubscribeNamespaceHandler(const quicr::TrackNamespace& prefix)
      : quicr::SubscribeNamespaceHandler(prefix)
    {
    }

  public:
    static auto Create(const quicr::TrackNamespace& prefix)
    {
        return std::shared_ptr<MySubscribeNamespaceHandler>(new MySubscribeNamespaceHandler(prefix));
    }

    virtual bool IsTrackAcceptable(const quicr::FullTrackName& name) const override
    {
        return GetPrefix().HasSamePrefix(name.name_space);
    }

    virtual std::shared_ptr<quicr::SubscribeTrackHandler> CreateHandler(
      const quicr::messages::PublishAttributes& attrs) override
    {
        return std::make_shared<MySubscribeTrackHandler>(
          attrs.track_full_name, quicr::messages::FilterType::kLargestObject, std::nullopt, true);
    }

  private:
};

/**
 * @brief MoQ client
 * @details Implementation of the MoQ Client
 */
class MyClient : public quicr::Client
{
    MyClient(const quicr::ClientConfig& cfg, bool& stop_threads)
      : quicr::Client(cfg)
      , stop_threads_(stop_threads)
    {
    }

  public:
    static std::shared_ptr<MyClient> Create(const quicr::ClientConfig& cfg, bool& stop_threads)
    {
        return std::shared_ptr<MyClient>(new MyClient(cfg, stop_threads));
    }

    void StatusChanged(Status status) override
    {
        switch (status) {
            case Status::kReady:
                SPDLOG_INFO("Connection ready");
                break;
            case Status::kConnecting:
                break;
            case Status::kPendingServerSetup:
                SPDLOG_INFO("Connection connected and now pending server setup");
                break;
            default:
                SPDLOG_INFO("Connection failed {0}", static_cast<int>(status));
                stop_threads_ = true;
                moq_example::terminate = true;
                moq_example::termination_reason = "Connection failed";
                moq_example::cv.notify_all();
                break;
        }
    }

    void PublishNamespaceReceived(const quicr::TrackNamespace& track_namespace,
                                  const quicr::PublishNamespaceAttributes&) override
    {
        auto th = quicr::TrackHash({ track_namespace, {} });
        SPDLOG_INFO("Received announce for namespace_hash: {}", th.track_namespace_hash);
    }

    void PublishNamespaceDoneReceived(const quicr::TrackNamespace& track_namespace) override
    {
        auto th = quicr::TrackHash({ track_namespace, {} });
        SPDLOG_INFO("Received unannounce for namespace_hash: {}", th.track_namespace_hash);
    }

    std::optional<quicr::messages::Location> GetLargestAvailable(const quicr::FullTrackName& track_full_name)
    {
        std::optional<quicr::messages::Location> largest_location = std::nullopt;
        auto th = quicr::TrackHash(track_full_name);

        auto cache_entry_it = qclient_vars::cache.find(th.track_fullname_hash);
        if (cache_entry_it != qclient_vars::cache.end()) {
            auto& [_, cache] = *cache_entry_it;
            if (const auto& latest_group = cache.Last(); latest_group && !latest_group->empty()) {
                const auto& latest_object = std::prev(latest_group->end());
                largest_location = { latest_object->headers.group_id, latest_object->headers.object_id };
            }
        }

        return largest_location;
    }

    void FetchReceived(quicr::ConnectionHandle connection_handle,
                       uint64_t request_id,
                       const quicr::FullTrackName& track_full_name,
                       quicr::messages::SubscriberPriority priority,
                       quicr::messages::GroupOrder group_order,
                       quicr::messages::Location start,
                       std::optional<quicr::messages::Location> end)
    {
        auto reason_code = quicr::FetchResponse::ReasonCode::kOk;
        std::optional<quicr::messages::Location> largest_location = std::nullopt;
        auto th = quicr::TrackHash(track_full_name);

        auto cache_entry_it = qclient_vars::cache.find(th.track_fullname_hash);
        if (cache_entry_it != qclient_vars::cache.end()) {
            auto& [_, cache] = *cache_entry_it;
            if (const auto& latest_group = cache.Last(); latest_group && !latest_group->empty()) {
                const auto& latest_object = *std::prev(latest_group->end());
                largest_location = { latest_object.headers.group_id, latest_object.headers.object_id };
            }
        }

        if (!largest_location.has_value()) {
            // TODO: This changes to send an empty object instead of REQUEST_ERROR
            reason_code = quicr::FetchResponse::ReasonCode::kNoObjects;
        } else {
            SPDLOG_INFO("Fetch received request id: {} largest group: {} object: {}",
                        request_id,
                        largest_location.value().group,
                        largest_location.value().object);
        }

        if (largest_location.has_value() &&
            (start.group > end->group || largest_location.value().group < start.group)) {
            reason_code = quicr::FetchResponse::ReasonCode::kInvalidRange;
        }

        const auto& cache_entries =
          cache_entry_it->second.Get(start.group, end->group != 0 ? end->group : cache_entry_it->second.Size());

        if (cache_entries.empty()) {
            reason_code = quicr::FetchResponse::ReasonCode::kInvalidRange;
        }

        ResolveFetch(connection_handle,
                     request_id,
                     priority,
                     group_order,
                     {
                       reason_code,
                       reason_code == quicr::FetchResponse::ReasonCode::kOk
                         ? std::nullopt
                         : std::make_optional("Cannot process fetch"),
                       largest_location,
                     });

        if (reason_code != quicr::FetchResponse::ReasonCode::kOk) {
            return;
        }

        // TODO: Adjust the TTL
        auto pub_fetch_h =
          quicr::PublishFetchHandler::Create(track_full_name, priority, request_id, group_order, 50000);
        BindFetchTrack(connection_handle, pub_fetch_h);

        std::thread retrieve_cache_thread([=, cache_entries = std::move(cache_entries), this] {
            defer(UnbindFetchTrack(connection_handle, pub_fetch_h));

            for (const auto& entry : cache_entries) {
                for (const auto& object : *entry) {
                    if (end->object && object.headers.group_id == end->group &&
                        object.headers.object_id >= end->object) {
                        return;
                    }

                    SPDLOG_DEBUG(
                      "Fetch sending group: {} object: {}", object.headers.group_id, object.headers.object_id);

                    pub_fetch_h->PublishObject(object.headers, object.data);
                }
            }
        });

        retrieve_cache_thread.detach();
    }

    void StandaloneFetchReceived(quicr::ConnectionHandle connection_handle,
                                 uint64_t request_id,
                                 const quicr::FullTrackName& track_full_name,
                                 const quicr::messages::StandaloneFetchAttributes& attributes) override
    {
        FetchReceived(connection_handle,
                      request_id,
                      track_full_name,
                      attributes.priority,
                      attributes.group_order,
                      attributes.start_location,
                      attributes.end_location);
    }

    void JoiningFetchReceived(quicr::ConnectionHandle connection_handle,
                              uint64_t request_id,
                              const quicr::FullTrackName& track_full_name,
                              const quicr::messages::JoiningFetchAttributes& attributes) override
    {
        uint64_t joining_start = 0;

        if (attributes.relative) {
            if (const auto largest = GetLargestAvailable(track_full_name)) {
                if (largest->group > attributes.joining_start)
                    joining_start = largest->group - attributes.joining_start;
            }
        } else {
            joining_start = attributes.joining_start;
        }

        FetchReceived(connection_handle,
                      request_id,
                      track_full_name,
                      attributes.priority,
                      attributes.group_order,
                      { joining_start, 0 },
                      std::nullopt);
    }

    void TrackStatusResponseReceived(quicr::ConnectionHandle,
                                     uint64_t request_id,
                                     const quicr::SubscribeResponse& response) override
    {
        switch (response.reason_code) {
            case quicr::SubscribeResponse::ReasonCode::kOk:
                SPDLOG_INFO("Request track status OK response request_id: {} largest group: {} object: {}",
                            request_id,
                            response.largest_location.has_value() ? response.largest_location->group : 0,
                            response.largest_location.has_value() ? response.largest_location->object : 0);
                break;
            default:
                SPDLOG_INFO("Request track status response ERROR request_id: {} error: {} reason: {}",
                            request_id,
                            static_cast<int>(response.reason_code),
                            response.error_reason.has_value() ? response.error_reason.value() : "");
                break;
        }
    }

    void PublishReceived(quicr::ConnectionHandle connection_handle,
                         uint64_t request_id,
                         const quicr::messages::PublishAttributes& publish_attributes) override
    {
        auto th = quicr::TrackHash(publish_attributes.track_full_name);
        SPDLOG_INFO(
          "Received PUBLISH from relay for track namespace_hash: {} name_hash: {} track_hash: {} request_id: {}",
          th.track_namespace_hash,
          th.track_name_hash,
          th.track_fullname_hash,
          request_id);

        // Accept the PUBLISH.
        ResolvePublish(connection_handle,
                       request_id,
                       publish_attributes,
                       { .reason_code = quicr::PublishResponse::ReasonCode::kOk });

        SPDLOG_INFO(
          "Accepted PUBLISH and subscribed to track_hash: {} request_id: {}", th.track_fullname_hash, request_id);
    }

  private:
    bool& stop_threads_;
};

/*===========================================================================*/
// Publisher Thread to perform publishing
/*===========================================================================*/

void
DoPublisher(const quicr::FullTrackName& full_track_name,
            const std::shared_ptr<quicr::Client>& client,
            bool use_announce,
            bool& stop)
{
    auto track_handler = std::make_shared<MyPublishTrackHandler>(
      full_track_name, quicr::TrackMode::kStream /*mode*/, 128 /*priority*/, 3000 /*ttl*/);

    track_handler->SetUseAnnounce(use_announce);

    if (qclient_vars::track_alias.has_value()) {
        track_handler->SetTrackAlias(*qclient_vars::track_alias);
    }

    SPDLOG_INFO("Started publisher track");

    bool published_track{ false };
    bool sending{ false };
    uint64_t group_id{ 0 };
    uint64_t object_id{ 0 };
    uint64_t subgroup_id{ 0 };

    std::ifstream moq_fs;
    std::ifstream data_fs;

    std::deque<std::pair<quicr::ObjectHeaders, quicr::Bytes>> messages;
    if (qclient_vars::playback) {
        const std::string name_str = ToString(full_track_name);
        moq_fs.open(qclient_consts::kMoqDataDir / (name_str + ".moq"), std::ios::in);
        data_fs.open(qclient_consts::kMoqDataDir / (name_str + ".dat"), std::ios::in);

        std::string data;
        std::getline(data_fs, data);

        json moq_arr_j = json::parse(moq_fs);

        for (const auto& moq_j : moq_arr_j) {
            quicr::ObjectHeaders hdr{
                .group_id = moq_j["groupID"],
                .object_id = moq_j["objectID"],
                .subgroup_id = moq_j["subGroup"],
                .payload_length = moq_j["dataLength"],
                .status = quicr::ObjectStatus::kAvailable,
                .priority = moq_j["publisherPriority"],
                .ttl = std::nullopt,
                .track_mode = std::nullopt,
                .extensions = std::nullopt,
                .immutable_extensions = std::nullopt,
            };

            std::size_t data_offset = moq_j["dataOffset"];

            auto& msg = messages.emplace_back(std::make_pair(hdr, quicr::Bytes{}));
            msg.second.assign(std::next(data.begin(), data_offset),
                              std::next(data.begin(), data_offset + hdr.payload_length));
        }
    }

    while (not stop) {
        if ((!published_track) && (client->GetStatus() == MyClient::Status::kReady)) {
            SPDLOG_INFO("Publish track ");
            client->PublishTrack(track_handler);
            published_track = true;
        }

        switch (track_handler->GetStatus()) {
            case MyPublishTrackHandler::Status::kOk:
                break;
            case MyPublishTrackHandler::Status::kNewGroupRequested:
                if (object_id) {
                    group_id++;
                    object_id = 0;
                    subgroup_id = 0;
                }
                SPDLOG_INFO("New Group Requested: Now using group {0}", group_id);

                break;
            case MyPublishTrackHandler::Status::kSubscriptionUpdated:
                SPDLOG_INFO("subscribe updated");
                break;
            case MyPublishTrackHandler::Status::kNoSubscribers:
                // Start a new group when a subscriber joins
                if (object_id) {
                    group_id++;
                    object_id = 0;
                    subgroup_id = 0;
                }
                [[fallthrough]];
            default:
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
        }

        if (!sending) {
            SPDLOG_INFO("--------------------------------------------------------------------------");

            if (qclient_vars::publish_clock) {
                SPDLOG_INFO(" Publishing clock timestamp every second");
            } else {
                SPDLOG_INFO(" Type message and press enter to send");
            }

            SPDLOG_INFO("--------------------------------------------------------------------------");
            sending = true;
        }

        if (qclient_vars::playback) {
            const auto [hdr, msg] = messages.front();
            messages.pop_front();

            SPDLOG_INFO("Send message: {0}", std::string(msg.begin(), msg.end()));

            try {
                auto status = track_handler->PublishObject(hdr, msg);
                if (status != decltype(status)::kOk) {
                    throw std::runtime_error("PublishObject returned status=" +
                                             std::to_string(static_cast<int>(status)));
                }
            } catch (const std::exception& e) {
                SPDLOG_ERROR("Caught exception trying to publish. (error={})", e.what());
            }

            std::this_thread::sleep_for(qclient_vars::playback_speed_ms);

            if (messages.empty()) {
                break;
            }

            continue;
        }

        if (object_id && object_id % 15 == 0) { // Set new group
            object_id = 0;
            subgroup_id = 0;
            group_id++;
        }

        if (qclient_vars::add_gaps && group_id && group_id % 4 == 0) {
            group_id += 1;
        }

        if (qclient_vars::add_gaps && object_id && object_id % 8 == 0) {
            object_id += 2;
        }

        std::string msg;
        if (qclient_vars::publish_clock) {
            std::this_thread::sleep_for(std::chrono::milliseconds(999));
            msg = quicr::example::GetTimeStr();
            SPDLOG_INFO("Group:{0} Object:{1}, Msg:{2}", group_id, object_id, msg);
        } else { // stdin
            getline(std::cin, msg);
            SPDLOG_INFO("Send message: {0}", msg);
        }

        quicr::ObjectHeaders obj_headers = {
            group_id,         object_id++,    subgroup_id,  msg.size(),   quicr::ObjectStatus::kAvailable,
            128 /*priority*/, 3000 /* ttl */, std::nullopt, std::nullopt, std::nullopt
        };

        try {
            if (track_handler->CanPublish()) {
                auto status =
                  track_handler->PublishObject(obj_headers, { reinterpret_cast<uint8_t*>(msg.data()), msg.size() });

                if (status == decltype(status)::kPaused) {
                    SPDLOG_INFO("Publish is paused");
                } else if (status == decltype(status)::kNoSubscribers) {
                    SPDLOG_INFO("Publish has no subscribers");
                } else if (status != decltype(status)::kOk) {
                    throw std::runtime_error("PublishObject returned status=" +
                                             std::to_string(static_cast<int>(status)));
                }
            }
        } catch (const std::exception& e) {
            SPDLOG_ERROR("Caught exception trying to publish. (error={})", e.what());
        }
    }

    client->UnpublishTrack(track_handler);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    SPDLOG_INFO("Publisher done track");
    moq_example::terminate = true;
}

/*===========================================================================*/
// Subgroup/Stream Test Publisher Thread
/*===========================================================================*/

void
DoSubgroupTest(const quicr::FullTrackName& full_track_name,
               const std::shared_ptr<quicr::Client>& client,
               bool use_announce,
               bool& stop)
{
    auto track_handler = std::make_shared<MyPublishTrackHandler>(
      full_track_name, quicr::TrackMode::kStream /*mode*/, 128 /*priority*/, 3000 /*ttl*/);

    track_handler->SetUseAnnounce(use_announce);

    if (qclient_vars::track_alias.has_value()) {
        track_handler->SetTrackAlias(*qclient_vars::track_alias);
    }

    SPDLOG_INFO("Started subgroup/stream test publisher");

    bool published_track{ false };

    // Wait for connection and publish track
    while (not stop) {
        if ((!published_track) && (client->GetStatus() == MyClient::Status::kReady)) {
            SPDLOG_INFO("Publish track for subgroup test");
            client->PublishTrack(track_handler);
            published_track = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Wait for track handler to be ready
    while (not stop) {
        if (track_handler->GetStatus() == MyPublishTrackHandler::Status::kOk ||
            track_handler->GetStatus() == MyPublishTrackHandler::Status::kSubscriptionUpdated ||
            track_handler->GetStatus() == MyPublishTrackHandler::Status::kNewGroupRequested) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    SPDLOG_INFO("--------------------------------------------------------------------------");
    SPDLOG_INFO(" Subgroup/Stream Test: {} groups, {} subgroups, {} messages/phase",
                qclient_vars::subgroup_test_num_groups,
                qclient_vars::subgroup_test_num_subgroups,
                qclient_vars::subgroup_test_messages_per_phase);
    SPDLOG_INFO(" Test will repeat until stopped (Ctrl+C)");
    SPDLOG_INFO("--------------------------------------------------------------------------");

    const std::size_t num_groups = qclient_vars::subgroup_test_num_groups;
    const std::size_t num_subgroups = qclient_vars::subgroup_test_num_subgroups;
    const std::size_t messages_per_phase = qclient_vars::subgroup_test_messages_per_phase;

    // Helper to publish an object
    auto publish_object = [&](uint64_t group_id,
                              uint64_t subgroup_id,
                              uint64_t object_id,
                              bool end_of_subgroup = false,
                              bool end_of_group = false) {
        std::string timestamp = quicr::example::GetTimeStr();
        std::string msg = "G" + std::to_string(group_id) + "S" + std::to_string(subgroup_id) + "O" +
                          std::to_string(object_id) + " " + timestamp;

        quicr::ObjectHeaders headers = { .group_id = group_id,
                                         .object_id = object_id,
                                         .subgroup_id = subgroup_id,
                                         .payload_length = msg.size(),
                                         .status = quicr::ObjectStatus::kAvailable,
                                         .priority = 128,
                                         .ttl = 3000,
                                         .track_mode = quicr::TrackMode::kStream,
                                         .extensions = std::nullopt,
                                         .immutable_extensions = std::nullopt,
                                         .end_of_subgroup = std::nullopt,
                                         .end_of_group = end_of_group };

        if (end_of_subgroup) {
            headers.end_of_subgroup = quicr::ObjectHeaders::CloseStream::kFin;
        }

        try {
            if (track_handler->CanPublish()) {
                auto status =
                  track_handler->PublishObject(headers, { reinterpret_cast<uint8_t*>(msg.data()), msg.size() });

                if (status == decltype(status)::kOk) {
                    SPDLOG_INFO("Published: group={} subgroup={} object={} end_subgroup={} end_group={}",
                                group_id,
                                subgroup_id,
                                object_id,
                                end_of_subgroup,
                                end_of_group);
                } else if (status == decltype(status)::kNoSubscribers) {
                    SPDLOG_WARN("No subscribers for group={} subgroup={}", group_id, subgroup_id);
                } else {
                    SPDLOG_ERROR("Publish failed with status={}", static_cast<int>(status));
                }
            }
        } catch (const std::exception& e) {
            SPDLOG_ERROR("Exception publishing: {}", e.what());
        }
    };

    uint64_t iteration = 0;
    uint64_t base_group_id = 0;

    // Repeat the test until stopped
    while (!stop) {
        iteration++;
        SPDLOG_INFO("========== Starting Test Iteration {} ==========", iteration);

        // Track object IDs per group+subgroup (reset each iteration)
        std::map<std::pair<uint64_t, uint64_t>, uint64_t> next_object_id;
        for (uint64_t group = 0; group < num_groups; ++group) {
            for (uint64_t subgroup = 0; subgroup < num_subgroups; ++subgroup) {
                next_object_id[{ group, subgroup }] = 0;
            }
        }

        // Helper to get and increment object ID for a group+subgroup
        auto get_next_obj_id = [&](uint64_t group, uint64_t subgroup) -> uint64_t {
            return next_object_id[{ group, subgroup }]++;
        };

        // Track which subgroups are still active per group (reset each iteration)
        std::map<uint64_t, std::set<uint64_t>> active_subgroups;
        for (uint64_t group = 0; group < num_groups; ++group) {
            for (uint64_t subgroup = 0; subgroup < num_subgroups; ++subgroup) {
                active_subgroups[group].insert(subgroup);
            }
        }

        // Run through phases: close one subgroup per phase (from lowest to highest)
        for (std::size_t phase = 0; phase < num_subgroups && !stop; ++phase) {
            uint64_t subgroup_to_close = phase;
            bool is_last_subgroup = (phase == num_subgroups - 1);

            SPDLOG_INFO("=== Iteration {} Phase {} ===", iteration, phase + 1);
            SPDLOG_INFO(
              "Publishing {} messages to {} active subgroups per group", messages_per_phase, num_subgroups - phase);

            // Publish messages_per_phase messages to all active subgroups
            for (std::size_t msg = 0; msg < messages_per_phase && !stop; ++msg) {
                bool is_last_in_phase = (msg == messages_per_phase - 1);

                for (uint64_t group = 0; group < num_groups; ++group) {
                    uint64_t actual_group_id = base_group_id + group;
                    for (uint64_t subgroup : active_subgroups[group]) {
                        bool close_subgroup = is_last_in_phase && (subgroup == subgroup_to_close);
                        bool close_group = close_subgroup && is_last_subgroup;

                        publish_object(
                          actual_group_id, subgroup, get_next_obj_id(group, subgroup), close_subgroup, close_group);
                    }
                }

                std::this_thread::sleep_for(qclient_vars::subgroup_test_interval_ms);
            }

            // Remove closed subgroup from active set
            for (uint64_t group = 0; group < num_groups; ++group) {
                active_subgroups[group].erase(subgroup_to_close);
            }

            SPDLOG_INFO(
              "Closed subgroup {} in all groups. {} subgroups remain.", subgroup_to_close, active_subgroups[0].size());
        }

        // Calculate and report totals for this iteration
        std::size_t total_messages = 0;
        for (uint64_t subgroup = 0; subgroup < num_subgroups; ++subgroup) {
            std::size_t messages_for_subgroup = messages_per_phase * (subgroup + 1);
            total_messages += messages_for_subgroup * num_groups;
        }

        SPDLOG_INFO("=== Iteration {} Complete ===", iteration);
        SPDLOG_INFO("Messages published this iteration: {}", total_messages);
        SPDLOG_INFO("Groups used: {} - {}", base_group_id, base_group_id + num_groups - 1);

        // Move to next set of group IDs for next iteration
        base_group_id += num_groups;

        // Brief pause between iterations
        SPDLOG_INFO("Pausing before next iteration...");
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    // Wait a bit for any remaining data to be sent
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    client->UnpublishTrack(track_handler);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    SPDLOG_INFO("Subgroup test publisher done after {} iterations", iteration);
    moq_example::terminate = true;
    moq_example::cv.notify_all();
}

/*===========================================================================*/
// Subscriber thread to perform subscribe
/*===========================================================================*/

void
DoSubscriber(const quicr::FullTrackName& full_track_name,
             const std::shared_ptr<quicr::Client>& client,
             quicr::messages::FilterType filter_type,
             const bool& stop,
             const std::optional<std::uint64_t> join_fetch,
             const bool absolute)
{
    typedef quicr::SubscribeTrackHandler::JoiningFetch Fetch;
    const auto joining_fetch = join_fetch.has_value()
                                 ? Fetch{ 128, quicr::messages::GroupOrder::kAscending, {}, *join_fetch, absolute }
                                 : std::optional<Fetch>(std::nullopt);
    const auto track_handler = std::make_shared<MySubscribeTrackHandler>(full_track_name, filter_type, joining_fetch);
    track_handler->SetPriority(128);

    SPDLOG_INFO("Started subscriber");

    bool subscribe_track{ false };

    while (not stop) {
        if ((!subscribe_track) && (client->GetStatus() == MyClient::Status::kReady)) {
            SPDLOG_INFO("Subscribing to track");
            client->SubscribeTrack(track_handler);
            subscribe_track = true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    client->UnsubscribeTrack(track_handler);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    SPDLOG_INFO("Subscriber done track");
    moq_example::terminate = true;
}

/*===========================================================================*/
// Fetch thread to perform fetch
/*===========================================================================*/

struct Range
{
    uint64_t start;
    uint64_t end;
};

void
DoFetch(const quicr::FullTrackName& full_track_name,
        const Range& group_range,
        const Range& object_range,
        const std::shared_ptr<quicr::Client>& client,
        const bool& stop)
{
    auto track_handler = MyFetchTrackHandler::Create(
      full_track_name, group_range.start, object_range.start, group_range.end, object_range.end);

    SPDLOG_INFO("Started fetch start: {}.{} end: {}.{}",
                group_range.start,
                object_range.start,
                group_range.end,
                object_range.end);

    bool fetch_track{ false };

    while (not stop) {
        if ((!fetch_track) && (client->GetStatus() == MyClient::Status::kReady)) {
            SPDLOG_INFO("Fetching track");
            client->FetchTrack(track_handler);
            fetch_track = true;
        }

        if (track_handler->GetStatus() == quicr::FetchTrackHandler::Status::kPendingResponse) {
            // do nothing...
        } else if (!fetch_track || (track_handler->GetStatus() != quicr::FetchTrackHandler::Status::kOk)) {
            SPDLOG_DEBUG("GetStatus() != quicr::FetchTrackHandler::Status::kOk {}", (int)track_handler->GetStatus());
            moq_example::terminate = true;
            moq_example::cv.notify_all();
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    client->CancelFetchTrack(track_handler);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    moq_example::terminate = true;
}

/*===========================================================================*/
// Main program
/*===========================================================================*/

quicr::ClientConfig
InitConfig(cxxopts::ParseResult& cli_opts, bool& enable_pub, bool& enable_sub, bool& enable_fetch, bool& use_announce)
{
    quicr::ClientConfig config;

    std::string qlog_path;
    if (cli_opts.count("qlog")) {
        qlog_path = cli_opts["qlog"].as<std::string>();
    }

    if (cli_opts.count("debug") && cli_opts["debug"].as<bool>() == true) {
        SPDLOG_INFO("setting debug level");
        spdlog::set_level(spdlog::level::debug);
    }

    if (cli_opts.count("version") && cli_opts["version"].as<bool>() == true) {
        SPDLOG_INFO("QuicR library version: {}", QUICR_VERSION);
        exit(0);
    }

    if (cli_opts.count("pub_namespace") && cli_opts.count("pub_name")) {
        enable_pub = true;
        SPDLOG_INFO("Publisher enabled using track namespace: {0} name: {1}",
                    cli_opts["pub_namespace"].as<std::string>(),
                    cli_opts["pub_name"].as<std::string>());
    }

    if (cli_opts.count("use_announce")) {
        use_announce = true;
        SPDLOG_INFO("Publisher will use announce flow");
    }

    if (cli_opts.count("clock") && cli_opts["clock"].as<bool>() == true) {
        SPDLOG_INFO("Running in clock publish mode");
        qclient_vars::publish_clock = true;
    }

    if (cli_opts.count("sub_namespace") && cli_opts.count("sub_name")) {
        enable_sub = true;
        SPDLOG_INFO("Subscriber enabled using track namespace: {0} name: {1}",
                    cli_opts["sub_namespace"].as<std::string>(),
                    cli_opts["sub_name"].as<std::string>());
    }

    if (cli_opts.count("fetch_namespace") && cli_opts.count("fetch_name")) {
        enable_fetch = true;
        SPDLOG_INFO("Subscriber enabled using track namespace: {0} name: {1}",
                    cli_opts["fetch_namespace"].as<std::string>(),
                    cli_opts["fetch_name"].as<std::string>());
    }

    if (cli_opts.count("track_alias")) {
        qclient_vars::track_alias = cli_opts["track_alias"].as<uint64_t>();
    }

    if (cli_opts.count("record")) {
        qclient_vars::record = true;
    }

    if (cli_opts.count("playback")) {
        qclient_vars::playback = true;
    }

    if (cli_opts.count("gaps") && cli_opts["gaps"].as<bool>() == true) {
        SPDLOG_INFO("Adding gaps to group and objects");
        qclient_vars::add_gaps = true;
    }

    if (cli_opts.count("new_group")) {
        qclient_vars::new_group_request_id = cli_opts["new_group"].as<uint64_t>();
    }

    if (cli_opts.count("track_status")) {
        qclient_vars::req_track_status = true;
    }

    if (cli_opts.count("subgroup_test")) {
        qclient_vars::subgroup_test = true;
        qclient_vars::publish_clock = true; // Enable clock mode for timing
        SPDLOG_INFO("Subgroup/stream test mode enabled");
    }

    if (cli_opts.count("subgroup_num_groups")) {
        qclient_vars::subgroup_test_num_groups = cli_opts["subgroup_num_groups"].as<uint64_t>();
    }

    if (cli_opts.count("subgroup_num_subgroups")) {
        qclient_vars::subgroup_test_num_subgroups = cli_opts["subgroup_num_subgroups"].as<uint64_t>();
    }

    if (cli_opts.count("subgroup_messages_per_phase")) {
        qclient_vars::subgroup_test_messages_per_phase = cli_opts["subgroup_messages_per_phase"].as<uint64_t>();
    }

    if (cli_opts.count("subgroup_interval_ms")) {
        qclient_vars::subgroup_test_interval_ms =
          std::chrono::milliseconds(cli_opts["subgroup_interval_ms"].as<uint64_t>());
    }

    if (cli_opts.count("playback_speed_ms")) {
        qclient_vars::playback_speed_ms = std::chrono::milliseconds(cli_opts["playback_speed_ms"].as<uint64_t>());
    }

    if (cli_opts.count("cache_duration_ms")) {
        qclient_vars::cache_duration_ms = std::chrono::milliseconds(cli_opts["cache_duration_ms"].as<uint64_t>());
    }

    if (cli_opts.count("ssl_keylog") && cli_opts["ssl_keylog"].as<bool>() == true) {
        SPDLOG_INFO("SSL Keylog enabled");
    }

    config.endpoint_id = cli_opts["endpoint_id"].as<std::string>();
    config.connect_uri = cli_opts["url"].as<std::string>();
    config.transport_config.debug = cli_opts["debug"].as<bool>();
    config.transport_config.ssl_keylog = cli_opts["ssl_keylog"].as<bool>();

    // Handle transport protocol override
    std::string transport_type = cli_opts["transport"].as<std::string>();
    if (transport_type == "webtransport") {
        // Convert URL scheme to https:// for WebTransport
        if (config.connect_uri.starts_with("moq://") || config.connect_uri.starts_with("moqt://")) {
            size_t scheme_end = config.connect_uri.find("://");
            if (scheme_end != std::string::npos) {
                config.connect_uri = "https://" + config.connect_uri.substr(scheme_end + 3);
                SPDLOG_INFO("Using WebTransport with URL: {}", config.connect_uri);
            }
        } else if (!config.connect_uri.starts_with("https://")) {
            SPDLOG_WARN("WebTransport requires https:// URL scheme");
        }
    } else if (transport_type == "quic") {
        // Convert URL scheme to moq:// for raw QUIC if needed
        if (config.connect_uri.starts_with("https://")) {
            size_t scheme_end = config.connect_uri.find("://");
            if (scheme_end != std::string::npos) {
                config.connect_uri = "moq://" + config.connect_uri.substr(scheme_end + 3);
                SPDLOG_INFO("Using raw QUIC with URL: {}", config.connect_uri);
            }
        }
    } else {
        SPDLOG_ERROR("Invalid transport type: {}. Valid options: quic, webtransport", transport_type);
        exit(-1);
    }

    config.transport_config.use_reset_wait_strategy = false;
    config.transport_config.time_queue_max_duration = 5000;
    config.transport_config.tls_cert_filename = "";
    config.transport_config.tls_key_filename = "";
    config.transport_config.quic_qlog_path = qlog_path;

    return config;
}

int
main(int argc, char* argv[])
{
    int result_code = EXIT_SUCCESS;

    cxxopts::Options options("qclient",
                             std::string("MOQ Example Client using QuicR Version: ") + std::string(QUICR_VERSION));

    // clang-format off
    options.set_width(75)
      .set_tab_expansion()
      .add_options()
        ("h,help", "Print help")
        ("d,debug", "Enable debugging") // a bool parameter
        ("v,version", "QuicR Version")                                        // a bool parameter
        ("r,url", "Relay URL", cxxopts::value<std::string>()->default_value("moq://localhost:1234"))
        ("e,endpoint_id", "This client endpoint ID", cxxopts::value<std::string>()->default_value("moq-client"))
        ("q,qlog", "Enable qlog using path", cxxopts::value<std::string>())
        ("s,ssl_keylog", "Enable SSL Keylog for transport debugging")
        ("t,transport", "Transport protocol: quic, webtransport", cxxopts::value<std::string>()->default_value("quic"));

    options.add_options("Publisher")
        ("use_announce", "Use Announce flow instead of publish flow", cxxopts::value<bool>())
        ("track_alias", "Track alias to use", cxxopts::value<uint64_t>())
        ("pub_namespace", "Track namespace", cxxopts::value<std::string>())
        ("pub_name", "Track name", cxxopts::value<std::string>())
        ("clock", "Publish clock timestamp every second instead of using STDIN chat")
        ("playback", "Playback recorded data from moq and dat files", cxxopts::value<bool>())
        ("playback_speed_ms", "Playback speed in ms", cxxopts::value<std::uint64_t>())
        ("cache_duration_ms", "TTL of objects in the cache", cxxopts::value<std::uint64_t>()->default_value("50000"))
        ("gaps", "Add gaps to groups and objects")
        ("subgroup_test", "Run subgroup/stream test mode with multiple groups and subgroups")
        ("subgroup_num_groups", "Number of groups for subgroup test", cxxopts::value<std::uint64_t>()->default_value("2"))
        ("subgroup_num_subgroups", "Number of subgroups per group for subgroup test", cxxopts::value<std::uint64_t>()->default_value("3"))
        ("subgroup_messages_per_phase", "Messages per phase for subgroup test", cxxopts::value<std::uint64_t>()->default_value("10"))
        ("subgroup_interval_ms", "Interval between messages in subgroup test (ms)", cxxopts::value<std::uint64_t>()->default_value("100"));

    options.add_options("Subscriber")
        ("sub_namespace", "Track namespace", cxxopts::value<std::string>())
        ("sub_name", "Track name", cxxopts::value<std::string>())
        ("start_point", "Start point for Subscription - 0 for from the beginning, 1 from the latest object", cxxopts::value<uint64_t>())
        ("sub_announces", "Prefix namespace to subscribe announces to", cxxopts::value<std::string>())
        ("record", "Record incoming data to moq and dat files", cxxopts::value<bool>())
        ("new_group", "Request new group on subscribe", cxxopts::value<uint64_t>()->default_value("0"))
        ("joining_fetch", "Subscribe with a joining fetch using this joining start", cxxopts::value<std::uint64_t>())
        ("absolute", "Joining fetch will be absolute not relative", cxxopts::value<bool>())
        ("track_status", "Request track status using sub_namespace and sub_name options", cxxopts::value<bool>());

    options.add_options("Fetcher")
        ("fetch_namespace", "Track namespace", cxxopts::value<std::string>())
        ("fetch_name", "Track name", cxxopts::value<std::string>())
        ("start_group", "Starting group ID", cxxopts::value<uint64_t>())
        ("end_group", "End Group ID", cxxopts::value<uint64_t>())
        ("start_object", "The starting object ID within the group", cxxopts::value<uint64_t>())
        ("end_object", "One past the final object ID in the group, 0 for all", cxxopts::value<uint64_t>());

    // clang-format on

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help({ "", "Publisher", "Subscriber", "Fetcher" }) << std::endl;
        return EXIT_SUCCESS;
    }

    // Install a signal handlers to catch operating system signals
    installSignalHandlers();

    // Lock the mutex so that main can then wait on it
    std::unique_lock lock(moq_example::main_mutex);

    bool enable_pub{ false };
    bool enable_sub{ false };
    bool enable_fetch{ false };
    bool use_announce{ false };
    quicr::ClientConfig config = InitConfig(result, enable_pub, enable_sub, enable_fetch, use_announce);

    try {
        bool stop_threads{ false };
        auto client = MyClient::Create(config, stop_threads);

        if (client->Connect() != quicr::Transport::Status::kConnecting) {
            SPDLOG_ERROR("Failed to connect to server due to invalid params, check URI");
            exit(-1);
        }

        while (not stop_threads) {
            if (client->GetStatus() == MyClient::Status::kReady) {
                SPDLOG_INFO("Connected to server");
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        std::thread pub_thread;
        std::thread sub_thread;
        std::thread fetch_thread;

        if (result.count("sub_announces")) {
            const auto& prefix_ns = quicr::example::MakeFullTrackName(result["sub_announces"].as<std::string>(), "");

            auto th = quicr::TrackHash(prefix_ns);

            SPDLOG_INFO("Sending subscribe announces for prefix '{}' namespace_hash: {}",
                        result["sub_announces"].as<std::string>(),
                        th.track_namespace_hash);

            client->SubscribeNamespace(MySubscribeNamespaceHandler::Create(prefix_ns.name_space));
        }

        if (enable_pub) {
            const auto& pub_track_name = quicr::example::MakeFullTrackName(result["pub_namespace"].as<std::string>(),
                                                                           result["pub_name"].as<std::string>());

            if (qclient_vars::subgroup_test) {
                pub_thread = std::thread(DoSubgroupTest, pub_track_name, client, use_announce, std::ref(stop_threads));
            } else {
                pub_thread = std::thread(DoPublisher, pub_track_name, client, use_announce, std::ref(stop_threads));
            }
        }
        if (enable_sub) {
            auto filter_type = quicr::messages::FilterType::kLargestObject;
            if (result.count("start_point")) {
                if (result["start_point"].as<uint64_t>() == 0) {
                    filter_type = quicr::messages::FilterType::kNextGroupStart;
                    SPDLOG_INFO("Setting subscription filter to Next Group Start");
                }
            }
            std::optional<std::uint64_t> joining_fetch;
            if (result.count("joining_fetch")) {
                joining_fetch = result["joining_fetch"].as<uint64_t>();
            }
            bool absolute = result.count("absolute") && result["absolute"].as<bool>();

            const auto& sub_track_name = quicr::example::MakeFullTrackName(result["sub_namespace"].as<std::string>(),
                                                                           result["sub_name"].as<std::string>());

            if (qclient_vars::req_track_status) {
                client->RequestTrackStatus(sub_track_name);
            }

            sub_thread = std::thread(
              DoSubscriber, sub_track_name, client, filter_type, std::ref(stop_threads), joining_fetch, absolute);
        }
        if (enable_fetch) {
            const auto& fetch_track_name = quicr::example::MakeFullTrackName(
              result["fetch_namespace"].as<std::string>(), result["fetch_name"].as<std::string>());

            fetch_thread =
              std::thread(DoFetch,
                          fetch_track_name,
                          Range{ result["start_group"].as<uint64_t>(), result["end_group"].as<uint64_t>() },
                          Range{ result["start_object"].as<uint64_t>(), result["end_object"].as<uint64_t>() },
                          client,
                          std::ref(stop_threads));
        }

        // Wait until told to terminate
        moq_example::cv.wait(lock, [&]() { return moq_example::terminate; });

        stop_threads = true;
        SPDLOG_INFO("Stopping threads...");

        if (pub_thread.joinable()) {
            pub_thread.join();
        }

        if (sub_thread.joinable()) {
            sub_thread.join();
        }

        if (fetch_thread.joinable()) {
            fetch_thread.join();
        }

        client->Disconnect();

        SPDLOG_INFO("Client done");
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));

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

    SPDLOG_INFO("Exit");

    return result_code;
}
