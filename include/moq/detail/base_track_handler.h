/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */
#pragma once

#include <optional>
#include <moq/detail/span.h>
#include <moq/common.h>
#include <moq/track_name.h>
#include <moq/object.h>
#include <vector>

namespace moq {

    /**
     * @brief Track mode object of object published or received
     *
     * @details QUIC stream handling mode used to send objects or how object was received
     */
    enum class TrackMode : uint8_t
    {
        kDatagram,
        kStreamPerObject,
        kStreamPerGroup,
        kStreamPerTrack
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
        std::optional<uint64_t> GetTrackAlias() { return full_track_name_.track_alias; }

        /**
         * @brief Sets the subscribe ID
         * @details MoQ instance sets the subscribe id based on subscribe track method call. Subscribe
         *      id is specific to the connection, so it must be set by the moq instance/connection.
         *
         * @param subscribe_id          62bit subscribe ID
         */
        void GetSubscribeId(std::optional<uint64_t> subscribe_id) { subscribe_id_ = subscribe_id; }

        /**
         * @brief Get the subscribe ID
         *
         * @return nullopt if not subscribed, otherwise the subscribe ID
         */
        std::optional<uint64_t> GetSubscribeId() { return subscribe_id_; }

        /**
         * @brief Get the full track name
         *
         * @details Gets the full track name
         *
         * @return FullTrackName
         */
        FullTrackName GetFullTrackName() { return { full_track_name_ }; }

        /**
         * @brief Get the connection ID
         */
        uint64_t GetConnectionId() { return connection_handle_; };

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
        ConnectionHandle connection_handle_;

        /**
         * subscribe_id_ is the primary index/key for subscribe subscribe context/delegate storage.
         *   It is use as the subscribe_id in MoQ related subscribes.  Subscribe ID will adapt
         *   to received subscribe IDs, so the value will reflect either the received subscribe ID
         *   or the next one that increments from last received ID.
         */
        std::optional<uint64_t> subscribe_id_;
    };

} // namespace moq
