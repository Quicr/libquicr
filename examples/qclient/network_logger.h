// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "spdlog_logger.h"

#include <nlohmann/json.hpp>
#include <quicr/handlers/publish_track_handler.h>
#include <quicr/handlers/subscribe_track_handler.h>
#include <quicr/log.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

/**
 * @brief Publishes log records onto a MoQ track.
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

    /// Objects per group, so a subscriber joining late doesn't wait long for a group boundary.
    static constexpr std::uint64_t kObjectsPerGroup = 100;

    /// Records handed to the transport before pausing to let it drain, and how long to pause.
    /// Together these cap the flush at a few thousand records a second.
    static constexpr std::size_t kFlushBurst = 32;
    static constexpr std::chrono::milliseconds kFlushPause{ 10 };

  public:
    NetworkLogger(const quicr::FullTrackName& full_track_name, uint8_t default_priority, uint32_t default_ttl)
      : PublishTrackHandler(full_track_name, quicr::TrackMode::kStream, default_priority, default_ttl)
      , SpdlogLogger(full_track_name.NamespaceStr() + "/" + std::string(full_track_name.NameStr()))
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

        auto record = MakeRecord(level, msg, location);
        if (record.empty()) {
            return;
        }

        {
            std::lock_guard lock(queue_mutex_);
            while (queue_.size() >= kMaxQueued) {
                queue_.pop_front();
                ++dropped_;
            }
            queue_.push_back(std::move(record));
        }

        queue_cv_.notify_one();
    }

    void StatusChanged(Status) override
    {
        {
            // Taking the queue lock closes the window between the drain thread testing
            // CanPublish() and going to sleep, so it cannot miss becoming publishable.
            std::lock_guard lock(queue_mutex_);

            // Data streams don't survive losing the subscriber, so resume in a fresh group
            // rather than reopening one the peer considers finished.
            restart_group_ = restart_group_ || !CanPublish();
        }

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
                return;
            }

            if (dropped_ != 0) {
                // Tell the subscriber about the hole instead of leaving it to infer one.
                auto notice = MakeRecord(Level::Warn,
                                         std_or_fmt::format("network logger dropped {} records", dropped_),
                                         std::source_location::current());
                dropped_ = 0;

                if (!notice.empty()) {
                    queue_.push_front(std::move(notice));
                }
            }

            auto record = std::move(queue_.front());
            queue_.pop_front();
            const bool restart_group = std::exchange(restart_group_, false);

            lock.unlock();
            const auto status = PublishRecord(record, restart_group);
            lock.lock();

            if (IsRetryable(status)) {
                // Put it back rather than lose it, so the backlog stays in order.
                queue_.push_front(std::move(record));
                restart_group_ = restart_group_ || restart_group;
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

    PublishObjectStatus PublishRecord(const quicr::Bytes& data, bool restart_group) noexcept
    try {
        if (object_id_ != 0 && (restart_group || object_id_ == kObjectsPerGroup)) {
            EndSubgroup(group_id_, 0);
            ++group_id_;
            object_id_ = 0;
        }

        const quicr::ObjectHeaders hdrs{
            .group_id = group_id_,
            .object_id = object_id_,
            .subgroup_id = 0,
            .payload_length = data.size(),
            .status = quicr::ObjectStatus::kAvailable,
            .priority = std::nullopt,
            .ttl = std::nullopt,
            .track_mode = std::nullopt,
            .extensions = std::nullopt,
            .immutable_extensions = std::nullopt,
        };

        const auto status = PublishObject(hdrs, data);
        if (status == PublishObjectStatus::kOk) {
            ++object_id_;
        }

        return status;
    } catch (const std::exception& e) {
        SpdlogLogger::Log(Level::Error, std_or_fmt::format("failed to publish log record (error={})", e.what()));
        return PublishObjectStatus::kInternalError;
    }

    /// @returns The encoded record, or empty if it can't be represented.
    quicr::Bytes MakeRecord(quicr::Logger::Level level, std::string_view msg, std::source_location location) noexcept
    try {
        json info;

        info["level"] = static_cast<int>(level);
        info["location"] = {
            { "line", location.line() },
            { "column", location.column() },
            { "file_name", location.file_name() },
            { "function_name", location.function_name() },
        };
        info["msg"] = std::string(msg);

        return json::to_bson(info);
    } catch (const std::exception& e) {
        // to_bson rejects strings that aren't valid UTF-8, which a log line carrying raw bytes
        // can easily be. It has already gone to stderr, so drop it rather than take the process
        // down from a noexcept context.
        SpdlogLogger::Log(Level::Warn, std_or_fmt::format("log record is not encodable (error={})", e.what()));
        return {};
    }

  private:
    /// Set on the drain thread, so the publish path's own logging isn't published recursively.
    static inline thread_local bool in_publish_ = false;

    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<quicr::Bytes> queue_;
    std::uint64_t dropped_{ 0 };
    bool restart_group_{ false };
    bool stop_{ false };

    /// Owned by the drain thread.
    std::uint64_t group_id_{ 0 };
    std::uint64_t object_id_{ 0 };
    std::size_t handed_off_{ 0 };

    std::thread publish_thread_;
};
