// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/messages/ctrl_message_types.h"

#include <chrono>
#include <optional>

namespace quicr {
    /**
     * @brief Response to a received TRACK_STATUS.
     */
    struct TrackStatusResponse
    {
        /// @brief Largest object location known for the track, when available.
        std::optional<messages::Location> largest_location{};

        messages::TrackExtensions track_properties{}; ///< Track extension properties returned by the publisher.
    };

    /**
     * @brief Publish namespace attributes
     *
     * @details Various attributes relative to the publish namespace
     */
    struct PublishNamespaceAttributes
    {
        std::uint64_t request_id{ 0 }; ///< Request identifier assigned to the inbound PUBLISH_NAMESPACE request.
    };

    /**
     * @brief Peer setup attributes
     */
    struct SetupAttributes
    {
        /// @brief Endpoint identifier advertised by the peer, or an empty string if absent.
        std::string endpoint_id;
    };

    // TODO: Maybe split base attributes out from SUBSCRIBE / PUBLISH?
    // TODO: E.g priority, new_group_request_id.

    /// @brief Attributes received in response to a successful PUBLISH request.
    struct PublishOkAttributes
    {
        /// @brief Subscriber priority requested by the peer, when supplied.
        const std::optional<std::uint8_t> subscriber_priority;

        /// @brief Delivery order requested by the peer, when supplied.
        const std::optional<messages::GroupOrder> group_order;

        const messages::Filter filter; ///< Object filter selected by the peer.

        /// @brief Whether object forwarding is enabled, when explicitly supplied.
        const std::optional<bool> forward;

        /// @brief Maximum subgroup delivery time requested by the peer, when supplied.
        const std::optional<std::chrono::milliseconds> subgroup_delivery_timeout;

        /// @brief Maximum object delivery time requested by the peer, when supplied.
        const std::optional<std::chrono::milliseconds> object_delivery_timeout;

        /// @brief Requested next group identifier, when the peer asks to start a new group.
        const std::optional<std::uint64_t> new_group_request_id;
    };

    /**
     * @brief Subscribe attributes
     */
    struct SubscribeAttributes
    {
        std::uint8_t priority{ 0 };                      ///< Subscriber priority; lower values are higher priority.
        std::optional<messages::GroupOrder> group_order; ///< Subscriber-requested group order, if specified.
        messages::GroupOrder publisher_default_group_order{
            messages::GroupOrder::kAscending
        }; ///< Publisher's default group order.
        std::chrono::milliseconds delivery_timeout{ 0 }; ///< Requested object delivery timeout.
        std::chrono::milliseconds expires{ 0 };          ///< Requested subscription lifetime.
        messages::Filter filter{ std::monostate{} };     ///< Filter selecting the requested object range.
        std::uint8_t forward{ 0 };                       ///< Nonzero to forward objects; zero to pause forwarding.
        std::optional<uint64_t> new_group_request_id;    ///< Requested next group identifier, if present.
        bool is_publisher_initiated{ false }; ///< Whether the subscription originated from a PUBLISH exchange.
        messages::Location start_location{};  ///< First group and object selected by the filter.
        std::chrono::milliseconds rendezvous_timeout{ 0 }; ///< Maximum time to wait for a matching publisher.
    };

    /// @brief Attributes of an inbound PUBLISH request.
    struct PublishAttributes
    {
        const FullTrackName track_full_name; ///< Fully qualified name of the offered track.

        const std::uint64_t track_alias; ///< Connection-scoped alias assigned to the track by its publisher.

        const std::vector<messages::Token> auth_tokens; ///< Authorization tokens carried by the request.

        /// @brief Publish lifetime in milliseconds, when supplied.
        const std::optional<std::uint64_t> expires;

        /// @brief Largest object currently available, when advertised.
        const std::optional<messages::Location> largest_object;

        const bool forward; ///< Whether the publisher requests immediate object forwarding.

        const messages::GroupOrder default_publisher_group_order; ///< Publisher's default group delivery order.

        const bool dynamic_groups; ///< Whether the publisher may create groups dynamically.

        /// @brief Publisher priority; lower values are higher priority.
        const std::uint8_t publisher_priority;

        /// @brief Maximum cache duration in milliseconds, when supplied.
        const std::optional<std::uint64_t> max_cache_duration;

        /// @brief Object delivery timeout in milliseconds, when supplied.
        const std::optional<std::uint64_t> delivery_timeout;

        const messages::TrackExtensions track_properties; ///< Additional track properties carried by the request.
    };

    /// @brief Attributes of an inbound standalone FETCH request.
    struct StandaloneFetchAttributes
    {
        std::uint8_t priority{ 0 };                      ///< Fetch priority; lower values are higher priority.
        std::optional<messages::GroupOrder> group_order; ///< Requested group order, if specified.
        messages::GroupOrder publisher_default_group_order{
            messages::GroupOrder::kAscending
        }; ///< Publisher's default group order.
        messages::Location start_location{};       ///< Inclusive start of the requested range.
        messages::FetchEndLocation end_location{}; ///< Inclusive end of the requested range.
    };

    /// @brief Attributes of an inbound joining FETCH request.
    struct JoiningFetchAttributes
    {
        std::uint8_t priority{ 0 };                      ///< Fetch priority; lower values are higher priority.
        std::optional<messages::GroupOrder> group_order; ///< Requested group order, if specified.
        messages::GroupOrder publisher_default_group_order{
            messages::GroupOrder::kAscending
        }; ///< Publisher's default group order.
        std::uint64_t joining_request_id{ 0 }; ///< Request identifier of the subscription being joined.
        bool relative{ false };           ///< Whether @ref joining_start is relative to the largest available group.
        std::uint64_t joining_start{ 0 }; ///< Absolute group identifier or relative group offset.
    };

    /// @brief Attributes of an inbound namespace subscription request.
    struct SubscribeNamespaceAttributes
    {
        uint64_t request_id{ 0 }; ///< Request identifier assigned by the peer.

        messages::FilterType filter_type{
            messages::FilterType::kTrackFilter
        }; ///< Whether the request subscribes to namespace announcements or matching tracks.

        messages::Filter filter{ std::monostate{} }; ///< Filter supplied with the request.
    };

}
