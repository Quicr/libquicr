// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <nlohmann/json.hpp>
#include <oss/cxxopts.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <quicr/client.h>
#include <quicr/object.h>

#include "helper_functions.h"
#include "signal_handler.h"

#include <filesystem>
#include <fstream>

#include <quicr/publish_fetch_handler.h>

using json = nlohmann::json; // NOLINT

namespace qclient_vars {
    bool publish_clock{ false };
    std::optional<uint64_t> track_alias; /// Track alias to use for subscribe
    bool record = false;
    bool playback = false;
    bool new_group = false;
    std::chrono::milliseconds playback_speed_ms(20);
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
                            const std::filesystem::path& dir = qclient_consts::kMoqDataDir)
      : SubscribeTrackHandler(full_track_name, 3, quicr::messages::GroupOrder::kAscending, filter_type, joining_fetch)
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

        std::string msg(data.begin(), data.end());
        SPDLOG_INFO("Received message: Group:{0}, Object:{1} - {2}", hdr.group_id, hdr.object_id, msg);

        if (qclient_vars::new_group && not new_group_requested_) {
            SPDLOG_INFO("Track alias: {} requesting new group", GetTrackAlias().value());
            RequestNewGroup();
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
        const auto alias = GetTrackAlias().value();
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
          new MyFetchTrackHandler(full_track_name, start_group, end_group, start_object, end_object));
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
            default:
                break;
        }
    }
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

    void AnnounceReceived(const quicr::TrackNamespace& track_namespace,
                          const quicr::PublishAnnounceAttributes&) override
    {
        auto th = quicr::TrackHash({ track_namespace, {} });
        SPDLOG_INFO("Received announce for namespace_hash: {}", th.track_namespace_hash);
    }

    void UnannounceReceived(const quicr::TrackNamespace& track_namespace) override
    {
        auto th = quicr::TrackHash({ track_namespace, {} });
        SPDLOG_INFO("Received unannounce for namespace_hash: {}", th.track_namespace_hash);
    }

    void SubscribeAnnouncesStatusChanged(const quicr::TrackNamespace& track_namespace,
                                         std::optional<quicr::messages::SubscribeAnnouncesErrorCode> error_code,
                                         std::optional<quicr::messages::ReasonPhrase> reason) override
    {
        auto th = quicr::TrackHash({ track_namespace, {} });
        if (!error_code.has_value()) {
            SPDLOG_INFO("Subscribe announces namespace_hash: {} status changed to OK", th.track_namespace_hash);
            return;
        }

        std::string reason_str;
        if (reason.has_value()) {
            reason_str.assign(reason.value().begin(), reason.value().end());
        }

        SPDLOG_WARN("Subscribe announces to namespace_hash: {} has error {} with reason: {}",
                    th.track_namespace_hash,
                    static_cast<uint64_t>(error_code.value()),
                    reason_str);
    }

    bool FetchReceived(quicr::ConnectionHandle connection_handle,
                       uint64_t request_id,
                       const quicr::FullTrackName& track_full_name,
                       const quicr::messages::FetchAttributes& attributes) override
    {
        auto pub_fetch_h = quicr::PublishFetchHandler::Create(
          track_full_name, attributes.priority, request_id, attributes.group_order, 50000);
        BindFetchTrack(connection_handle, pub_fetch_h);

        for (uint64_t pub_group_number = attributes.start_location.group; pub_group_number < attributes.end_group;
             ++pub_group_number) {
            quicr::ObjectHeaders headers{ .group_id = pub_group_number,
                                          .object_id = 0,
                                          .subgroup_id = 0,
                                          .payload_length = 0,
                                          .status = quicr::ObjectStatus::kAvailable,
                                          .priority = attributes.priority,
                                          .ttl = 3000, // in milliseconds
                                          .track_mode = std::nullopt,
                                          .extensions = std::nullopt };

            std::string hello = "Hello:" + std::to_string(pub_group_number);
            std::vector<uint8_t> data_vec(hello.begin(), hello.end());
            quicr::BytesSpan data{ data_vec.data(), data_vec.size() };
            pub_fetch_h->PublishObject(headers, data);
        }

        return true;
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
      full_track_name, quicr::TrackMode::kStream /*mode*/, 2 /*priority*/, 3000 /*ttl*/);

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
            group_id,       object_id++,    subgroup_id,  msg.size(),  quicr::ObjectStatus::kAvailable,
            2 /*priority*/, 3000 /* ttl */, std::nullopt, std::nullopt
        };

        try {
            auto status =
              track_handler->PublishObject(obj_headers, { reinterpret_cast<uint8_t*>(msg.data()), msg.size() });

            if (status == decltype(status)::kPaused) {
                SPDLOG_INFO("Publish is paused");
            } else if (status == decltype(status)::kNoSubscribers) {
                SPDLOG_INFO("Publish has no subscribers");
            } else if (status != decltype(status)::kOk) {
                throw std::runtime_error("PublishObject returned status=" + std::to_string(static_cast<int>(status)));
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
// Subscriber thread to perform subscribe
/*===========================================================================*/

void
DoSubscriber(const quicr::FullTrackName& full_track_name,
             const std::shared_ptr<quicr::Client>& client,
             quicr::messages::FilterType filter_type,
             const bool& stop,
             bool join_fetch)
{
    typedef quicr::SubscribeTrackHandler::JoiningFetch Fetch;
    const auto joining_fetch =
      join_fetch ? Fetch{ 4, quicr::messages::GroupOrder::kAscending, {}, 0 } : std::optional<Fetch>(std::nullopt);
    const auto track_handler = std::make_shared<MySubscribeTrackHandler>(full_track_name, filter_type, joining_fetch);

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

    SPDLOG_INFO("Started fetch");

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
            SPDLOG_INFO("GetStatus() != quicr::FetchTrackHandler::Status::kOk {}", (int)track_handler->GetStatus());
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

    if (cli_opts.count("new_group")) {
        qclient_vars::new_group = true;
    }

    if (cli_opts.count("playback_speed_ms")) {
        qclient_vars::playback_speed_ms = std::chrono::milliseconds(cli_opts["playback_speed_ms"].as<uint64_t>());
    }

    if (cli_opts.count("ssl_keylog") && cli_opts["ssl_keylog"].as<bool>() == true) {
        SPDLOG_INFO("SSL Keylog enabled");
    }

    config.endpoint_id = cli_opts["endpoint_id"].as<std::string>();
    config.connect_uri = cli_opts["url"].as<std::string>();
    config.transport_config.debug = cli_opts["debug"].as<bool>();
    config.transport_config.ssl_keylog = cli_opts["ssl_keylog"].as<bool>();

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
      //.allow_unrecognised_options()
      .add_options()
        ("h,help", "Print help")
        ("d,debug", "Enable debugging") // a bool parameter
        ("v,version", "QuicR Version")                                        // a bool parameter
        ("r,url", "Relay URL", cxxopts::value<std::string>()->default_value("moq://localhost:1234"))
        ("e,endpoint_id", "This client endpoint ID", cxxopts::value<std::string>()->default_value("moq-client"))
        ("q,qlog", "Enable qlog using path", cxxopts::value<std::string>())
        ("s,ssl_keylog", "Enable SSL Keylog for transport debugging");

    options.add_options("Publisher")
        ("use_announce", "Use Announce flow instead of publish flow", cxxopts::value<bool>())
        ("track_alias", "Track alias to use", cxxopts::value<uint64_t>())
        ("pub_namespace", "Track namespace", cxxopts::value<std::string>())
        ("pub_name", "Track name", cxxopts::value<std::string>())
        ("clock", "Publish clock timestamp every second instead of using STDIN chat")
        ("playback", "Playback recorded data from moq and dat files", cxxopts::value<bool>())
        ("playback_speed_ms", "Playback speed in ms", cxxopts::value<std::uint64_t>());

    options.add_options("Subscriber")
        ("sub_namespace", "Track namespace", cxxopts::value<std::string>())
        ("sub_name", "Track name", cxxopts::value<std::string>())
        ("start_point", "Start point for Subscription - 0 for from the beginning, 1 from the latest object", cxxopts::value<uint64_t>())
        ("sub_announces", "Prefix namespace to subscribe announces to", cxxopts::value<std::string>())
        ("record", "Record incoming data to moq and dat files", cxxopts::value<bool>())
        ("new_group", "Request new group on subscribe", cxxopts::value<bool>())
        ("joining_fetch", "Subscribe with a joining fetch", cxxopts::value<bool>());

    options.add_options("Fetcher")
        ("fetch_namespace", "Track namespace", cxxopts::value<std::string>())
        ("fetch_name", "Track name", cxxopts::value<std::string>())
        ("start_group", "Starting group ID", cxxopts::value<uint64_t>())
        ("end_group", "One past the final group ID", cxxopts::value<uint64_t>())
        ("start_object", "The starting object ID within the group", cxxopts::value<uint64_t>())
        ("end_object", "One past the final object ID in the group", cxxopts::value<uint64_t>());

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

            client->SubscribeAnnounces(prefix_ns.name_space);
        }

        if (enable_pub) {
            const auto& pub_track_name = quicr::example::MakeFullTrackName(result["pub_namespace"].as<std::string>(),
                                                                           result["pub_name"].as<std::string>());

            pub_thread = std::thread(DoPublisher, pub_track_name, client, use_announce, std::ref(stop_threads));
        }
        if (enable_sub) {
            auto filter_type = quicr::messages::FilterType::kLargestObject;
            if (result.count("start_point")) {
                if (result["start_point"].as<uint64_t>() == 0) {
                    filter_type = quicr::messages::FilterType::kNextGroupStart;
                    SPDLOG_INFO("Setting subscription filter to Next Group Start");
                }
            }
            bool joining_fetch = result.count("joining_fetch") && result["joining_fetch"].as<bool>();

            const auto& sub_track_name = quicr::example::MakeFullTrackName(result["sub_namespace"].as<std::string>(),
                                                                           result["sub_name"].as<std::string>());

            sub_thread =
              std::thread(DoSubscriber, sub_track_name, client, filter_type, std::ref(stop_threads), joining_fetch);
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
