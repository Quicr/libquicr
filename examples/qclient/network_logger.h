// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "spdlog_logger.h"

#include <nlohmann/json.hpp>
#include <quicr/handlers/publish_track_handler.h>
#include <quicr/handlers/subscribe_track_handler.h>
#include <quicr/log.h>

#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

/**
 * @brief Log record encoding from draft-jennings-moq-log-03.
 */
namespace moq_log {

    /// Seconds from the NTP era-zero epoch (1 Jan 1900) to the Unix epoch. Section 4 dates the
    /// `timestamp` field from 1972 in prose, but its worked example (3,155,587,200 for
    /// 31 Dec 1999) is era zero, so era zero is what's implemented here.
    inline constexpr std::uint64_t kNtpEraZeroToUnixSeconds = 2208988800;

    /// Section 3: the group carries the timestamp truncated to 62 bits.
    inline constexpr std::uint64_t kGroupIdMask = (std::uint64_t{ 1 } << 62) - 1;

    inline std::uint64_t Now() noexcept
    {
        const auto since_unix_epoch = std::chrono::duration_cast<std::chrono::microseconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count();

        return static_cast<std::uint64_t>(since_unix_epoch) + kNtpEraZeroToUnixSeconds * 1000000;
    }

    /// @returns The RFC 5424 severity name for a log level.
    inline std::string_view SeverityName(quicr::Logger::Level level) noexcept
    {
        switch (level) {
            case quicr::Logger::Level::Trace:
                [[fallthrough]]; // RFC 5424 has nothing finer than Debug.
            case quicr::Logger::Level::Debug:
                return "Debug";
            case quicr::Logger::Level::Info:
                return "Informational";
            case quicr::Logger::Level::Warn:
                return "Warning";
            case quicr::Logger::Level::Error:
                return "Error";
            case quicr::Logger::Level::Critical:
                return "Critical";
            case quicr::Logger::Level::Off:
                break;
        }

        return "Debug";
    }

    /// @returns The log level for an RFC 5424 severity name, collapsed onto what a logger has.
    inline quicr::Logger::Level SeverityLevel(std::string_view severity) noexcept
    {
        if (severity == "Emergency" || severity == "Alert" || severity == "Critical") {
            return quicr::Logger::Level::Critical;
        }
        if (severity == "Error") {
            return quicr::Logger::Level::Error;
        }
        if (severity == "Warning") {
            return quicr::Logger::Level::Warn;
        }
        if (severity == "Debug") {
            return quicr::Logger::Level::Debug;
        }

        // "Notice", "Informational", and the "Info" the draft's example uses.
        return quicr::Logger::Level::Info;
    }

    inline std::string Hostname() noexcept
    {
        char name[256] = { 0 };
        if (::gethostname(name, sizeof(name) - 1) != 0) {
            return "unknown";
        }

        return name;
    }

}

/**
 * @brief Publishes log records onto a MoQ track per draft-jennings-moq-log-03.
 *
 * @details libquicr logs from inside its own critical sections, and from the picoquic loop thread,
 *          so Log() must never re-enter the session. Records are handed to a dedicated thread that
 *          does the publishing; the drain thread marks itself so that the logs produced by the
 *          publish path are written locally only and not fed back into the queue.
 *
 *          Records are queued from construction, well before the session exists, and the drain
 *          thread sleeps until the track becomes publishable. Connecting and subscribing are the
 *          noisiest and most interesting part of a run, so the backlog is flushed in order once a
 *          subscriber arrives rather than being discarded.
 */
class NetworkLogger
  : public quicr::PublishTrackHandler
  , public SpdlogLogger

{
    using json = nlohmann::json; // NOLINT

    /// Backlog bound. Held entirely in memory until a subscriber arrives, so it caps the damage
    /// from a run that logs heavily and never gets one.
    static constexpr std::size_t kMaxQueued = 10000;

    /// Records handed to the transport before pausing to let it drain, and how long to pause.
    /// Together these cap the flush at a few thousand records a second.
    static constexpr std::size_t kFlushBurst = 32;
    static constexpr std::chrono::milliseconds kFlushPause{ 10 };

    /// The timestamp is taken when the event is logged, not when it is published, so that a
    /// backlog flushed seconds later still lands in the group its event belongs to.
    struct Record
    {
        std::uint64_t timestamp;
        quicr::Bytes payload;
    };

  public:
    NetworkLogger(const quicr::FullTrackName& full_track_name,
                  std::string appname,
                  uint8_t default_priority,
                  uint32_t default_ttl)
      : PublishTrackHandler(full_track_name, quicr::TrackMode::kStream, default_priority, default_ttl)
      , SpdlogLogger(full_track_name.NamespaceStr() + "/" + std::string(full_track_name.NameStr()))
      , appname_(std::move(appname))
      , hostname_(moq_log::Hostname())
      , procid_(std::to_string(::getpid()))
      , publish_thread_(&NetworkLogger::Drain, this)
    {
    }

    virtual ~NetworkLogger()
    {
        {
            std::lock_guard lock(queue_mutex_);
            stop_ = true;
        }
        queue_cv_.notify_all();

        if (publish_thread_.joinable()) {
            publish_thread_.join();
        }
    }

    void Log(quicr::Logger::Level level,
             std::string_view msg,
             std::source_location location = std::source_location::current()) override
    {
        SpdlogLogger::Log(level, msg, location);

        if (in_publish_) {
            return;
        }

        const auto timestamp = moq_log::Now();
        auto payload = MakeRecord(level, msg, location, timestamp);
        if (payload.empty()) {
            return;
        }

        {
            std::lock_guard lock(queue_mutex_);
            while (queue_.size() >= kMaxQueued) {
                queue_.pop_front();
                ++dropped_;
            }
            queue_.push_back(Record{ .timestamp = timestamp, .payload = std::move(payload) });
        }

        queue_cv_.notify_one();
    }

    void StatusChanged(Status) override
    {
        // Serialize with the drain thread's predicate: taking the lock guarantees it is either
        // already waiting, and so will see this notify, or has yet to test CanPublish().
        std::lock_guard lock(queue_mutex_);
        queue_cv_.notify_all();
    }

  private:
    void Drain()
    {
        in_publish_ = true;

        std::unique_lock lock(queue_mutex_);
        while (true) {
            queue_cv_.wait(lock, [this] { return stop_ || (!queue_.empty() && CanPublish()); });

            if (stop_ && (queue_.empty() || !CanPublish())) {
                // Nothing left to flush, or no way left to flush it.
                break;
            }

            if (dropped_ != 0) {
                // Tell the subscriber about the hole instead of leaving it to infer one.
                const auto timestamp = moq_log::Now();
                auto notice = MakeRecord(Level::Warn,
                                         std_or_fmt::format("network logger dropped {} records", dropped_),
                                         std::source_location::current(),
                                         timestamp);
                dropped_ = 0;

                if (!notice.empty()) {
                    queue_.push_front(Record{ .timestamp = timestamp, .payload = std::move(notice) });
                }
            }

            auto record = std::move(queue_.front());
            queue_.pop_front();

            lock.unlock();
            const auto status = PublishRecord(record);
            lock.lock();

            if (IsRetryable(status)) {
                // Put it back rather than lose it, so the backlog stays in order.
                queue_.push_front(std::move(record));
                continue;
            }

            /*
             * PublishObject() only queues, so flushing a backlog in a tight loop fills the
             * transport's TX queue far past what it can put on the wire. Objects that sit there
             * longer than their TTL are discarded and reset the stream carrying the group, so
             * hand off in bursts and let the transport catch up in between. Steady-state logging
             * never reaches the burst count with a backlog behind it, so it is never delayed.
             */
            if (++handed_off_ >= kFlushBurst) {
                handed_off_ = 0;

                if (!queue_.empty()) {
                    queue_cv_.wait_for(lock, kFlushPause, [this] { return stop_; });
                }
            }
        }

        if (last_group_.has_value()) {
            lock.unlock();
            EndSubgroup(*last_group_, 0);
        }
    }

    /// @returns True if the record can be sent later, false if it has to be given up on.
    static bool IsRetryable(PublishObjectStatus status) noexcept
    {
        switch (status) {
            case PublishObjectStatus::kNoSubscribers:
            case PublishObjectStatus::kNotAnnounced:
            case PublishObjectStatus::kPaused:
            case PublishObjectStatus::kPendingPublishOk:
                return true;
            default:
                return false;
        }
    }

    PublishObjectStatus PublishRecord(const Record& record) noexcept
    try {
        /*
         * Section 3 puts the timestamp in the group and uses the object only to separate records
         * that share a microsecond, so a group is normally one record and a new one starts on
         * nearly every publish.
         */
        const auto group_id = record.timestamp & moq_log::kGroupIdMask;
        const bool new_group = last_group_ != group_id;
        const auto object_id = new_group ? 0 : object_id_ + 1;

        if (new_group && last_group_.has_value()) {
            // Closed here rather than straight after publishing, so records sharing a microsecond
            // still reach the subgroup their group opened.
            EndSubgroup(*last_group_, 0);
        }

        const quicr::ObjectHeaders hdrs{
            .group_id = group_id,
            .object_id = object_id,
            .subgroup_id = 0,
            .payload_length = record.payload.size(),
            .status = quicr::ObjectStatus::kAvailable,
            .priority = std::nullopt,
            .ttl = std::nullopt,
            .track_mode = std::nullopt,
            .extensions = std::nullopt,
            .immutable_extensions = std::nullopt,
        };

        const auto status = PublishObject(hdrs, record.payload);
        if (status == PublishObjectStatus::kOk) {
            last_group_ = group_id;
            object_id_ = object_id;
        }

        return status;
    } catch (const std::exception& e) {
        SpdlogLogger::Log(Level::Error, std_or_fmt::format("failed to publish log record (error={})", e.what()));
        return PublishObjectStatus::kInternalError;
    }

    /// @returns The record as a section 4 JSON object, or empty if it can't be built.
    quicr::Bytes MakeRecord(quicr::Logger::Level level,
                            std::string_view msg,
                            std::source_location location,
                            std::uint64_t timestamp) noexcept
    try {
        json record;

        record["timestamp"] = timestamp;
        record["severity"] = moq_log::SeverityName(level);
        record["hostname"] = hostname_;
        record["appname"] = appname_;
        record["procid"] = procid_;
        record["msg"] = std::string(msg);

        // Section 4 treats anything else as structured data mapping to OpenTelemetry attributes,
        // so the call site travels under OpenTelemetry's code attribute names.
        record["code.filepath"] = location.file_name();
        record["code.lineno"] = location.line();
        record["code.column"] = location.column();
        record["code.function.name"] = location.function_name();

        // A log line can carry bytes that aren't valid UTF-8, and JSON can't. Substituting beats
        // throwing the record away, which is what dump() does by default.
        const auto text = record.dump(-1, ' ', false, json::error_handler_t::replace);

        return { text.begin(), text.end() };
    } catch (const std::exception& e) {
        SpdlogLogger::Log(Level::Warn, std_or_fmt::format("log record is not encodable (error={})", e.what()));
        return {};
    }

  private:
    /// Set on the drain thread, so the publish path's own logging isn't published recursively.
    static inline thread_local bool in_publish_ = false;

    const std::string appname_;
    const std::string hostname_;
    const std::string procid_;

    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<Record> queue_;
    std::uint64_t dropped_{ 0 };
    bool stop_{ false };

    /// Owned by the drain thread.
    std::optional<std::uint64_t> last_group_;
    std::uint64_t object_id_{ 0 };
    std::size_t handed_off_{ 0 };

    std::thread publish_thread_;
};

class SubLogger
  : public quicr::SubscribeTrackHandler
  , public SpdlogLogger
{
    using json = nlohmann::json; // NOLINT

  public:
    SubLogger(const quicr::FullTrackName& full_track_name,
              std::uint8_t priority,
              std::optional<quicr::messages::GroupOrder> group_order,
              const quicr::messages::Filter& filter = std::monostate{},
              bool publisher_initiated = false)
      : quicr::SubscribeTrackHandler(full_track_name, priority, group_order, filter, std::nullopt, publisher_initiated)
      , SpdlogLogger(full_track_name.NamespaceStr() + "/" + std::string(full_track_name.NameStr()))
    {
    }

    virtual ~SubLogger() = default;

    void ObjectReceived(const quicr::ObjectHeaders& object_headers,
                        quicr::BytesSpan data,
                        std::optional<quicr::messages::StreamHeaderProperties>) override
    try {
        const json record = json::parse(data);

        // spdlog keeps the pointers rather than copying, so these have to outlive the log call.
        const auto filepath = record.value("code.filepath", "");
        const auto function = record.value("code.function.name", "");

        logger_->log(spdlog::source_loc(filepath.c_str(), record.value("code.lineno", 0), function.c_str()),
                     ConvertLevelType(moq_log::SeverityLevel(record.value("severity", "Informational"))),
                     "{} {}[{}]: {}",
                     record.value("hostname", "-"),
                     record.value("appname", "-"),
                     record.value("procid", "-"),
                     record.value("msg", ""));
    } catch (const std::exception& e) {
        // A malformed record must not propagate: the session treats a throw out of here as a
        // protocol violation and tears the connection down.
        SpdlogLogger::Log(Level::Warn,
                          std_or_fmt::format("dropping malformed log record group: {} object: {} (error={})",
                                             object_headers.group_id,
                                             object_headers.object_id,
                                             e.what()));
    }
};
