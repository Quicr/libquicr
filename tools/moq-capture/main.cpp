// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "helper_functions.h"
#include "spdlog_logger.h"

#include <cxxopts.hpp>
#include <nlohmann/json.hpp>
#include <quicr/handlers/publish_track_handler.h>
#include <quicr/session_callbacks.h>
#include <quicr/session_manager.h>

#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>

using json = nlohmann::json; // NOLINT
using namespace std::chrono_literals;

namespace {
    std::string ToString(const quicr::FullTrackName& ftn)
    {
        std::string str;
        const std::regex pattern("[<>:\"/\\|?*]+");
        const auto& entries = ftn.name_space.GetEntries();
        for (const auto& entry : entries) {
            str += std::regex_replace(std::string(entry.begin(), entry.end()), pattern, "_") + '_';
        }

        str += std::string(ftn.name.begin(), ftn.name.end());

        return str;
    }
}

namespace base64 {
    // From https://gist.github.com/williamdes/308b95ac9ef1ee89ae0143529c361d37;

    constexpr std::string_view kValues = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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

class CaptureClientCallbacks : public quicr::Session::ClientCallbacks
{
    struct Private
    {
        explicit Private() = default;
    };

  public:
    CaptureClientCallbacks(Private) {}

    static std::shared_ptr<CaptureClientCallbacks> Create()
    {
        return std::make_shared<CaptureClientCallbacks>(Private{});
    }

    quicr::Expected<void, quicr::Error<quicr::ErrorCode>> SetupReceived(
      const std::shared_ptr<quicr::Session>&,
      const quicr::SetupAttributes& setup_attributes) override
    {
        std::cerr << "Setup received from '" << setup_attributes.endpoint_id << "'" << std::endl;
        return {};
    }
};

class RecordingTrackHandler : public quicr::SubscribeTrackHandler
{
    struct Private
    {
        explicit Private() = default;
    };

  public:
    RecordingTrackHandler(Private,
                          const quicr::FullTrackName& full_track_name,
                          std::uint8_t priority,
                          const std::filesystem::path& dir)
      : quicr::SubscribeTrackHandler(full_track_name, priority, quicr::messages::GroupOrder::kAscending)
    {
        std::filesystem::create_directory(dir);

        const std::string name_str = ::ToString(full_track_name);

        data_file_.open(dir / (name_str + ".dat"), std::ios::in | std::ios::out | std::ios::trunc);
        if (!data_file_.is_open()) {
            throw std::runtime_error("Failed to open/create data file");
        }

        moq_file_.open(dir / (name_str + ".moq"), std::ios::in | std::ios::out | std::ios::trunc);
        moq_file_ << json::array();
    }

    virtual ~RecordingTrackHandler()
    {
        data_file_ << std::endl;
        data_file_.close();

        moq_file_ << std::endl;
        moq_file_.close();
    }

    static std::shared_ptr<RecordingTrackHandler> Create(const quicr::FullTrackName& full_track_name,
                                                         std::uint8_t priority,
                                                         const std::filesystem::path& dir)
    {
        return std::make_shared<RecordingTrackHandler>(Private{}, full_track_name, priority, dir);
    }

    void ObjectReceived(const quicr::ObjectHeaders& hdr,
                        std::span<const std::uint8_t> data,
                        std::optional<quicr::messages::StreamHeaderProperties>) override
    {
        const quicr::FullTrackName& ftn = GetFullTrackName();

        const std::size_t data_offset = data_file_.tellp();
        data_file_ << std::string(data.begin(), data.end());

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
        j["subGroupID"] = hdr.subgroup_id;
        j["publisherPriority"] = hdr.priority.value();
        j["maxCacheDuration"] = 0;
        j["publisherDeliveryTimeout"] = 0;
        j["receiveTime"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::high_resolution_clock::now().time_since_epoch())
                             .count();
        j["dataFile"] = data_filename;
        j["dataOffset"] = data_offset;
        j["dataLength"] = data.size();

        moq_file_.clear();
        moq_file_.seekg(0);
        json moq_j = json::parse(moq_file_);
        moq_j.push_back(j);

        moq_file_.clear();
        moq_file_.seekg(0);
        moq_file_ << moq_j.dump();
    }

  private:
    std::fstream moq_file_;
    std::ofstream data_file_;
};

std::deque<std::pair<quicr::ObjectHeaders, quicr::Bytes>>
BuildReplayMessages(const std::shared_ptr<quicr::PublishTrackHandler>& handler,
                    std::ifstream& moq_file,
                    std::ifstream& data_file)
{
    const json moq_arr_j = json::parse(moq_file);
    const std::string data = [](auto& file) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }(data_file);

    std::deque<std::pair<quicr::ObjectHeaders, quicr::Bytes>> messages;
    for (const auto& moq_j : moq_arr_j) {
        quicr::ObjectHeaders hdr{
            .group_id = moq_j["groupID"],
            .object_id = moq_j["objectID"],
            .subgroup_id = moq_j["subGroupID"],
            .payload_length = moq_j["dataLength"],
            .status = quicr::ObjectStatus::kAvailable,
            .priority = moq_j["publisherPriority"],
            .ttl = std::nullopt,
            .track_mode = std::nullopt,
            .extensions = std::nullopt,
            .immutable_extensions = std::nullopt,
        };

        const std::size_t data_offset = moq_j["dataOffset"];
        quicr::Bytes bytes{
            std::next(data.begin(), data_offset),
            std::next(data.begin(), data_offset + hdr.payload_length),
        };
        messages.emplace_back(std::make_pair(hdr, std::move(bytes)));
    }

    return messages;
}

volatile std::sig_atomic_t stop = 0;

void
HandleSignal(int)
{
    stop = 1;
}

int
main(int argc, char** argv)
try {
    cxxopts::Options options(
      "moq-capture", std::string("MOQ Replay and Record client using QuicR Version: ") + std::string(QUICR_VERSION));

    // clang-format off
    options.set_width(75)
      .set_tab_expansion()
      .add_options()
        ("h,help", "Print help")
        ("r,url", "Relay URL", cxxopts::value<std::string>()->default_value("moq://localhost:1234"))
        ("e,endpoint_id", "This client endpoint ID", cxxopts::value<std::string>()->default_value("moq-capture"))
        ("record", "Record track objects")
        ("replay", "Replay track objects")
        ("d,data_dir", "Directory for data file to be recorded to and replayed from", cxxopts::value<std::string>()->default_value((std::filesystem::current_path() / "moq_data").string()))
        ("replay_speed_ms", "Replay speed in ms", cxxopts::value<std::uint64_t>()->default_value("20"))
        ("namespace", "Track namespace delimited by /", cxxopts::value<std::string>())
        ("name", "Track name", cxxopts::value<std::string>())
    ;
    // clang-format on

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cerr << options.help() << std::endl;
        return EXIT_SUCCESS;
    }

    std::shared_ptr<quicr::Logger> logger = std::make_shared<SpdlogLogger>("CAPTURE");

    if (result.count("replay") == 0 && result.count("record") == 0) {
        QUICR_LOGGER_ERROR(logger, "Must run with either replay or record");
        return EXIT_FAILURE;
    }

    if (result.count("namespace") == 0 || result.count("name") == 0) {
        QUICR_LOGGER_ERROR(logger, "Missing namespace and/or name");
        return EXIT_FAILURE;
    }

    quicr::ClientConfig config;
    config.connect_uri = result["url"].as<std::string>();
    config.transport_config.time_queue_max_duration = 5000;
    config.transport_config.tls_cert_filename = "";
    config.transport_config.tls_key_filename = "";

    quicr::SessionManager session_mgr(logger);
    auto weak_session = session_mgr.AddTransport(config, CaptureClientCallbacks::Create());

    auto session = weak_session.lock();
    if (!session) {
        QUICR_LOGGER_ERROR(logger, "Failed to start session");
        return EXIT_FAILURE;
    }

    while (session->GetStatus() != quicr::Session::Status::kReady) {
        const auto status = session->GetStatus();
        if (status == quicr::Session::Status::kNotReady || status == quicr::Session::Status::kInternalError ||
            status == quicr::Session::Status::kInvalidParams || status == quicr::Session::Status::kNotConnected ||
            status == quicr::Session::Status::kFailedToConnect) {
            return EXIT_FAILURE;
        }

        std::this_thread::sleep_for(20ms);
    }

    const bool replay = static_cast<bool>(result.count("replay"));
    const bool record = static_cast<bool>(result.count("record"));

    const quicr::FullTrackName track_name{
        quicr::example::MakeTrackNamespace(result["namespace"].as<std::string>(), '/'),
        quicr::AsOwnedBytes(result["name"].as<std::string>()),
    };

    const std::filesystem::path data_dir = result["data_dir"].as<std::string>();
    std::shared_ptr<quicr::TrackHandler> handler;
    if (replay) {
        handler = quicr::PublishTrackHandler::Create(
          track_name, quicr::TrackMode::kStream, 127, 127, 5000, quicr::messages::Location{});
    } else if (record) {
        handler = RecordingTrackHandler::Create(track_name, 127, data_dir);
    }

    session_mgr.AddHandler(session, handler);

    std::signal(SIGINT, HandleSignal);

    if (record) {
        while (!stop) {
            std::this_thread::sleep_for(20ms);
        }

        return EXIT_SUCCESS;
    }

    const auto pub_handler = std::static_pointer_cast<quicr::PublishTrackHandler>(handler);

    {
        std::mutex m;
        std::condition_variable cv;
        std::unique_lock lock(m);

        if (!cv.wait_for(lock, 3s, [&] { return pub_handler->CanPublish(); })) {
            return EXIT_FAILURE;
        }
    }

    const std::chrono::milliseconds replay_speed_ms(result["replay_speed_ms"].as<std::uint64_t>());

    std::ifstream moq_file(data_dir / (::ToString(track_name) + ".moq"));
    std::ifstream data_file(data_dir / (::ToString(track_name) + ".dat"));

    auto messages = BuildReplayMessages(pub_handler, moq_file, data_file);
    while (!stop && pub_handler->CanPublish() && !messages.empty()) {
        try {
            const auto& [hdr, msg] = messages.front();
            auto status = pub_handler->PublishObject(hdr, msg);
            messages.pop_front();

            if (status != decltype(status)::kOk) {
                break;
            }
        } catch (const std::exception& e) {
        }

        std::this_thread::sleep_for(replay_speed_ms);
    }

    return EXIT_SUCCESS;
} catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
}
