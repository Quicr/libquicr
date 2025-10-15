// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/multi_track_subscribe_handler.h"

namespace quicr {

    // --------------------------------------------------------------------------
    // Track query methods
    // --------------------------------------------------------------------------

    std::vector<FullTrackName> MultiTrackSubscribeHandler::GetActiveTracks()
    {
        return {};
    }

    std::optional<MultiTrackSubscribeHandler::Status> MultiTrackSubscribeHandler::GetTrackStatus(
      const FullTrackName& track)
    {
        return std::nullopt;
    }

    std::optional<uint64_t> MultiTrackSubscribeHandler::GetTrackAlias(const FullTrackName& track)
    {
        return std::nullopt;
    }

    std::optional<SubscribeTrackMetrics> MultiTrackSubscribeHandler::GetTrackMetrics(const FullTrackName& track)
    {
        return std::nullopt;
    }

    std::optional<messages::SubscriberPriority> MultiTrackSubscribeHandler::GetTrackPriority(const FullTrackName& track)
    {
        return std::nullopt;
    }

    std::optional<messages::GroupOrder> MultiTrackSubscribeHandler::GetTrackGroupOrder(const FullTrackName& track)
    {
        return std::nullopt;
    }

    std::optional<messages::FilterType> MultiTrackSubscribeHandler::GetTrackFilterType(const FullTrackName& track)
    {
        return std::nullopt;
    }

    // --------------------------------------------------------------------------
    // Per-track control methods
    // --------------------------------------------------------------------------

    void MultiTrackSubscribeHandler::Pause([[maybe_unused]] const std::optional<FullTrackName>& track) noexcept
    {
        // TODO: Implement
    }

    void MultiTrackSubscribeHandler::Resume([[maybe_unused]] const std::optional<FullTrackName>& track) noexcept
    {
        // TODO: Implement
    }

    void MultiTrackSubscribeHandler::Unsubscribe([[maybe_unused]] const FullTrackName& track) noexcept
    {
        // TODO: Implement
    }

    void MultiTrackSubscribeHandler::RequestNewGroup(
      [[maybe_unused]] const std::optional<FullTrackName>& track) noexcept
    {
        // TODO: Implement
    }

    // --------------------------------------------------------------------------
    // Public callback implementations
    // --------------------------------------------------------------------------

    void MultiTrackSubscribeHandler::ObjectReceived([[maybe_unused]] const FullTrackName& track,
                                                    [[maybe_unused]] const ObjectHeaders& object_headers,
                                                    [[maybe_unused]] BytesSpan data)
    {
    }

    void MultiTrackSubscribeHandler::StreamDataRecv([[maybe_unused]] const FullTrackName& track,
                                                    [[maybe_unused]] bool is_start,
                                                    [[maybe_unused]] uint64_t stream_id,
                                                    [[maybe_unused]] std::shared_ptr<const std::vector<uint8_t>> data)
    {
    }

    void MultiTrackSubscribeHandler::DgramDataRecv([[maybe_unused]] const FullTrackName& track,
                                                   [[maybe_unused]] std::shared_ptr<const std::vector<uint8_t>> data)
    {
    }

    // --------------------------------------------------------------------------
    // Protected methods
    // --------------------------------------------------------------------------

    void MultiTrackSubscribeHandler::SetTrackStatus(const uint64_t track_alias, const Status status) {}

    // --------------------------------------------------------------------------
    // Private methods for Transport layer
    // --------------------------------------------------------------------------

    PublishResponse MultiTrackSubscribeHandler::AddTrack(const FullTrackName& full_name,
                                                         uint64_t track_alias,
                                                         uint64_t request_id)
    {
        const auto response = TrackAdded(full_name);
        return { .reason_code = response.reason_code, .error_reason = response.error_reason };
    }

    void MultiTrackSubscribeHandler::RemoveTrack(const uint64_t track_alias) {}

    MultiTrackSubscribeHandler::TrackContext* MultiTrackSubscribeHandler::GetTrackByAlias(const uint64_t track_alias)
    {
        const auto it = tracks_.find(track_alias);
        if (it == tracks_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    MultiTrackSubscribeHandler::TrackContext* MultiTrackSubscribeHandler::GetTrackByName(const FullTrackName& track)
    {
        for (auto& [alias, ctx] : tracks_) {
            if (ctx.full_track_name.name_space == track.name_space && ctx.full_track_name.name == track.name) {
                return &ctx;
            }
        }
        return nullptr;
    }

} // namespace quicr
