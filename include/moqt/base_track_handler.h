/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */
#pragma once

#include "cantina/logger.h"

namespace moq::transport {

    using bytes = std::vector<uint8_t>;

    /**
     * @brief MoQ track base handler for tracks (subscribe/publish)
     *
     * @details Base MoQ track handler
     */
    class BaseTrackHandler
    {
      public:
        friend class Core;

        virtual ~BaseTrackHandler() = default;

        // --------------------------------------------------------------------------
        // Public API methods that normally should not be overridden
        // --------------------------------------------------------------------------

        BaseTrackHandler() = delete;

      protected:
        /**
         * @brief Track delegate constructor
         */
        BaseTrackHandler(const bytes& track_namespace, const bytes& track_name, const cantina::LoggerPointer& logger)
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
         * @brief Get the connection ID
         */
        uint64_t getConnectionId() { return _mi_conn_id; };

        // --------------------------------------------------------------------------
        // Internal
        // --------------------------------------------------------------------------
      private:
        /**
         * @brief Set the connection ID
         *
         * @details The MOQ Handler sets the connection ID
         */
        void set_connection_id(uint64_t conn_id) { _mi_conn_id = conn_id; };

        // --------------------------------------------------------------------------
        // Member variables
        // --------------------------------------------------------------------------

        cantina::LoggerPointer _logger;
        const bytes _track_namespace;
        const bytes _track_name;
        std::optional<uint64_t> _track_alias;

        uint64_t _mi_conn_id; // Set by moq implementation

        /**
         * _subscribe_id is the primary index/key for subscribe subscribe context/delegate storage.
         *   It is use as the subscribe_id in MOQT related subscribes.  Subscribe ID will adapt
         *   to received subscribe IDs, so the value will reflect either the received subscribe ID
         *   or the next one that increments from last received ID.
         */
        std::optional<uint64_t> _subscribe_id;

        uint64_t _prev_group_id{ 0 };
        uint64_t _prev_object_id{ 0 };
    };

} // namespace quicr
