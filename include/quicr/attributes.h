// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/messages/ctrl_message_types.h"

#include <chrono>
#include <optional>

namespace quicr {
    /**
     * @brief Publish namespace attributes
     *
     * @details Various attributes relative to the publish namespace
     */
    struct PublishNamespaceAttributes
    {
        std::uint64_t request_id{ 0 };
    };

    /**
     * @brief Client Setup Attributes
     */
    struct ClientSetupAttributes
    {
        const std::string endpoint_id;
    };

    /**
     * @brief Server Setup Attributes
     */
    struct ServerSetupAttributes
    {
        const uint64_t moqt_version;
        const std::string server_id;
    };

    // TODO: Maybe split base attributes out from SUBSCRIBE / PUBLISH?
    // TODO: E.g priority, new_group_request_id.

    struct PublishOkAttributes
    {
        const std::optional<std::uint8_t> subscriber_priority;
        const std::optional<messages::GroupOrder> group_order;
        const messages::Filter filter;
        const std::optional<bool> forward;
        const std::optional<std::chrono::milliseconds> subgroup_delivery_timeout;
        const std::optional<std::chrono::milliseconds> object_delivery_timeout;
        const std::optional<std::uint64_t> new_group_request_id;
    };

    /**
     * @brief Subscribe attributes
     */
    struct SubscribeAttributes
    {
        std::uint8_t priority{ 0 };                      ///< Subscriber priority
        std::optional<messages::GroupOrder> group_order; ///< Subscriber group order
        messages::GroupOrder publisher_default_group_order{
            messages::GroupOrder::kAscending
        }; ///< Publisher track default group order
        std::chrono::milliseconds delivery_timeout{ 0 }; ///< Subscriber delivery timeout
        std::chrono::milliseconds expires{ 0 };          ///< Subscriber expiry in ms
        messages::Filter filter{ std::monostate{} };     /// Subscriber filter
        std::uint8_t forward{ 0 };                       ///< True to Resume/forward data, False to pause/stop data
        std::optional<uint64_t> new_group_request_id;    ///< Indicates new group id is requested
        bool is_publisher_initiated{ false };            ///< True will not send SUBSCRIBE_OK.
        messages::Location start_location{};             ///< Start location of group and object
    };

    struct PublishAttributes
    {
        const FullTrackName track_full_name;
        const std::uint64_t track_alias;
        const std::vector<messages::Token> auth_tokens;
        const std::optional<std::uint64_t> expires;
        const std::optional<messages::Location> largest_object;
        const bool forward;
        const messages::GroupOrder default_publisher_group_order;
        const bool dynamic_groups;
        const std::uint8_t default_publisher_priority;
        const std::optional<std::uint64_t> max_cache_duration;
        const std::optional<std::uint64_t> delivery_timeout;
        const messages::TrackExtensions track_properties;
    };

    struct StandaloneFetchAttributes
    {
        std::uint8_t priority{ 0 };                      ///< Fetch priority
        std::optional<messages::GroupOrder> group_order; ///< Fetch group order
        messages::GroupOrder publisher_default_group_order{
            messages::GroupOrder::kAscending
        }; ///< Publisher track default group order
        messages::Location start_location{};       ///< Fetch starting location in range
        messages::FetchEndLocation end_location{}; ///< Fetch final location.
    };

    struct JoiningFetchAttributes
    {
        std::uint8_t priority{ 0 };                      ///< Fetch priority
        std::optional<messages::GroupOrder> group_order; ///< Fetch group order
        messages::GroupOrder publisher_default_group_order{
            messages::GroupOrder::kAscending
        }; ///< Publisher track default group order
        std::uint64_t joining_request_id{ 0 }; ///< Fetch joining request_id
        bool relative{ false };                ///< True indicates relative to largest, False indicates absolute
        std::uint64_t joining_start{ 0 };      ///< Fetch joining start
    };

    struct SubscribeNamespaceAttributes
    {
        uint64_t request_id{ 0 };
        messages::FilterType filter_type{ messages::FilterType::kTrackFilter };
        messages::Filter filter{ std::monostate{} };
    };

}
