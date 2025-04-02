// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <optional>
#include <quicr/common.h>
#include <quicr/detail/span.h>
#include <quicr/track_name.h>
#include <vector>

namespace quicr {

    /**
     * @brief Track mode object of object published or received
     *
     * @details QUIC stream handling mode used to send objects or how object was received
     */
    enum class TrackMode : uint8_t
    {
        kDatagram,
        kStream,
    };

    /**
     * @brief Response to received MOQT Subscribe message
     */
    struct SubscribeResponse
    {
        /**
         * @details **kOK** indicates that the subscribe is accepted and OK should be sent. Any other
         *       value indicates that the subscribe is not accepted and the reason code and other
         *       fields will be set.
         */
        enum class ReasonCode : uint8_t
        {
            kOk = 0,
            kInternalError,
            kInvalidRange,
            kRetryTrackAlias,
        };
        ReasonCode reason_code;

        std::optional<std::string> reason_phrase = std::nullopt;
        std::optional<uint64_t> track_alias = std::nullopt; ///< Set only when ResponseCode is kRetryTrackAlias

        std::optional<uint64_t> largest_group_id = std::nullopt;
        std::optional<uint64_t> largest_object_id = std::nullopt;
    };

    /**
     * @brief MoQ track base handler for tracks (subscribe/publish)
     *
     * @details Base MoQ track handler
     */
    class BaseTrackHandler
    {
      public:
        friend class Transport;
        friend class Server;

        virtual ~BaseTrackHandler() = default;

        // --------------------------------------------------------------------------
        // Public API methods that normally should not be overridden
        // --------------------------------------------------------------------------

        BaseTrackHandler() = delete;

      protected:
        /**
         * @brief Track delegate constructor
         *
         * @param full_track_name       Full track name struct
         */
        BaseTrackHandler(const FullTrackName& full_track_name)
          : full_track_name_(full_track_name)
        {
        }

        // --------------------------------------------------------------------------
        // Public Virtual API callback event methods to be overridden
        // --------------------------------------------------------------------------
      public:
        /**
         * @brief Set the track alias
         * @details MOQ transport instance will set the track alias when the track has
         *   been assigned.
         *
         * @param track_alias       MoQ track alias for track namespace+name that
         *                          is relative to the QUIC connection session
         */
        void SetTrackAlias(uint64_t track_alias) { full_track_name_.track_alias = track_alias; }

        /**
         * @brief Get the track alias
         * @returns Track alias as an optional. Track alias may not be set yet. If not
         *   set, nullopt will be returned.
         */
        std::optional<uint64_t> GetTrackAlias() const noexcept { return full_track_name_.track_alias; }

        /**
         * @brief Sets the subscribe ID
         * @details MoQ instance sets the subscribe id based on subscribe track method call. Subscribe
         *      id is specific to the connection, so it must be set by the moq instance/connection.
         *
         * @param subscribe_id          62bit subscribe ID
         */
        void SetSubscribeId(std::optional<uint64_t> subscribe_id) { subscribe_id_ = subscribe_id; }

        /**
         * @brief Get the subscribe ID
         *
         * @return nullopt if not subscribed, otherwise the subscribe ID
         */
        std::optional<uint64_t> GetSubscribeId() const noexcept { return subscribe_id_; }

        /**
         * @brief Get the full track name
         *
         * @details Gets the full track name
         *
         * @return FullTrackName
         */
        FullTrackName GetFullTrackName() const noexcept { return { full_track_name_ }; }

        /**
         * @brief Get the connection ID
         */
        uint64_t GetConnectionId() const noexcept { return connection_handle_; };

        // --------------------------------------------------------------------------
        // Internal
        // --------------------------------------------------------------------------
      private:
        /**
         * @brief Set the connection ID
         *
         * @details The MOQ Handler sets the connection ID
         */
        void SetConnectionId(uint64_t connection_handle) { connection_handle_ = connection_handle; };

        // --------------------------------------------------------------------------
        // Member variables
        // --------------------------------------------------------------------------

        FullTrackName full_track_name_;

        ConnectionHandle connection_handle_; // QUIC transport connection ID

        /**
         * subscribe_id_ is the primary index/key for subscribe subscribe context/delegate storage.
         *   It is use as the subscribe_id in MoQ related subscribes.  Subscribe ID will adapt
         *   to received subscribe IDs, so the value will reflect either the received subscribe ID
         *   or the next one that increments from last received ID.
         */
        std::optional<uint64_t> subscribe_id_;
    };

} // namespace moq
