/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */
#pragma once

#include "cantina/logger.h"
#include <quicr/quicr_common.h>

namespace quicr {

    /**
     * @brief MoQ track base handler for tracks (subscribe/publish)
     *
     * @details Base MoQ track handler
     */
    class MoQBaseTrackHandler
    {
      public:
        virtual ~MoQBaseTrackHandler() = default;
        enum class SendError : uint8_t
        {
            OK = 0,
            INTERNAL_ERROR,
            NOT_AUTHORIZED,
            NOT_ANNOUNCED,
            NO_SUBSCRIBERS,
        };

        enum class TrackSendStatus : uint8_t
        {
            OK = 0,
            NOT_CONNECTED,
            NOT_ANNOUNCED,
            PENDING_ANNOUNCE_RESPONSE,
            ANNOUNCE_NOT_AUTHORIZED,
            NO_SUBSCRIBERS,
        };

        enum class TrackMode : uint8_t
        {
            DATAGRAM,
            STREAM_PER_OBJECT,
            STREAM_PER_GROUP,
            STREAM_PER_TRACK
        };

        // --------------------------------------------------------------------------
        // Public API methods that normally should not be overridden
        // --------------------------------------------------------------------------

        MoQBaseTrackHandler() = delete;

        /**
         * @brief Track delegate constructor
         */
      protected:
        MoQBaseTrackHandler(const bytes& track_namespace,
                            const bytes& track_name,
                            const cantina::LoggerPointer& logger)
          : _logger(std::make_shared<cantina::Logger>("MTD", logger))
          , _track_namespace(track_namespace)
          , _track_name(track_name)
        {
        }

        // --------------------------------------------------------------------------
        // Public Virtual API callback event methods to be overridden
        // --------------------------------------------------------------------------
      public:
        /**
         * @brief Notification that data can not be sent
         * @details Notification that data cannot be sent yet with a reason. This will
         *   be called as it transitions through send states.
         *
         * @param status        Indicates the reason for why data cannot be sent [yet]
         */
        virtual void cb_sendNotReady(TrackSendStatus status) = 0;

        /**
         * @brief Notification that the send queue is congested
         * @details Notification indicates that send queue is backlogged and sending more
         *   will likely cause more congestion.
         *
         * @param cleared             Indicates if congestion has cleared
         * @param objects_in_queue    Number of objects still pending to be sent at time of notification
         */
        virtual void cb_sendCongested(bool cleared, uint64_t objects_in_queue) = 0;

        // --------------------------------------------------------------------------
        // Internal API methods used by MOQ instance and peering session
        // --------------------------------------------------------------------------

        /**
         * @brief Set the track alias
         * @details MOQ Instance session will set the track alias when the track has
         *   been assigned.
         *
         * @param track_alias       MOQT track alias for track namespace+name that
         *                          is relative to the sesssion
         */
        void setTrackAlias(uint64_t track_alias) { _track_alias = track_alias; }

        /**
         * @brief Get the track alias
         * @returns Track alias as an optional. Track alias may not be set yet. If not
         *   set, nullopt will be returned.
         */
        std::optional<uint64_t> getTrackAlias() { return _track_alias; }

        /**
         * @brief Sets the subscribe ID
         * @details MoQ instance sets the subscribe id based on subscribe track method call. Subscribe
         *      id is specific to the connection, so it must be set by the moq instance/connection.
         *
         * @param subscribe_id          62bit subscribe ID
         */
        void setSubscribeId(std::optional<uint64_t> subscribe_id) { _subscribe_id = subscribe_id; }

        /**
         * @brief Get the subscribe ID
         *
         * @return nullopt if not subscribed, otherwise the subscribe ID
         */
        std::optional<uint64_t> getSubscribeId() { return _subscribe_id; }

        /**
         * @brief Get the track namespace
         * @return span of track namespace
         */
        std::span<uint8_t const> getTrackNamespace() { return std::span(_track_namespace); }

        /**
         * @brief Get the track name
         * @return span of track name
         */
        std::span<uint8_t const> getTrackName() { return std::span(_track_name); }

        /**
         * @brief Set the connection ID
         *
         * @details The MOQ Handler sets the connection ID
         */
        void setConnectionId(uint64_t conn_id) { _mi_conn_id = conn_id; };

        /**
         * @brief Get the connection ID
         */
        uint64_t getConnectionId() { return _mi_conn_id; };

        // --------------------------------------------------------------------------
        // MOQ Implementation specific variables/methods
        // --------------------------------------------------------------------------
      private:
        cantina::LoggerPointer _logger;
        const bytes _track_namespace;
        const bytes _track_name;
        std::optional<uint64_t> _track_alias;

        uint64_t _mi_conn_id;          // Set by moq implementation

        /**
         * _subscribe_id is the primary index/key for subscribe subscribe context/delegate storage.
         *   It is use as the subscribe_id in MOQT related subscribes.  Subscribe ID will adapt
         *   to received subscribe IDs, so the value will reflect either the received subscribe ID
         *   or the next one that increments from last received ID.
         */
        std::optional<uint64_t> _subscribe_id;

        uint64_t _prev_group_id{ 0 };
    };

} // namespace quicr
