/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */
#pragma once

#include <optional>
#include <transport/span.h>
#include <vector>

namespace moq::transport {

    using Bytes = std::vector<uint8_t>;
    using BytesSpan = Span<uint8_t const>;

    /**
     * @brief MoQ track base handler for tracks (subscribe/publish)
     *
     * @details Base MoQ track handler
     */
    class BaseTrackHandler
    {
      public:
        friend class Transport;

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

        virtual ~BaseTrackHandler() = default;

        // --------------------------------------------------------------------------
        // Public API methods that normally should not be overridden
        // --------------------------------------------------------------------------

        BaseTrackHandler() = delete;

      protected:
        /**
         * @brief Track delegate constructor
         *
         * @param track_namespace       Opaque binary array of bytes track namespace
         * @param track_name            Opaque binary array of bytes track name
         */
        BaseTrackHandler(const Bytes& track_namespace, const Bytes& track_name)
          : track_namespace_(track_namespace)
          , track_name_(track_name)
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
         * @param track_alias       MoQT track alias for track namespace+name that
         *                          is relative to the QUIC connection session
         */
        void SetTrackAlias(uint64_t track_alias) { track_alias_ = track_alias; }

        /**
         * @brief Get the track alias
         * @returns Track alias as an optional. Track alias may not be set yet. If not
         *   set, nullopt will be returned.
         */
        std::optional<uint64_t> GetTrackAlias() { return track_alias_; }

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
         * @brief Get the track namespace
         * @return span of track namespace
         */
        BytesSpan GetTrackNamespace() { return { track_namespace_ }; }

        /**
         * @brief Get the track name
         * @return span of track name
         */
        BytesSpan GetTrackName() { return { track_name_ }; }

        /**
         * @brief Get the connection ID
         */
        uint64_t GetConnectionId() { return conn_id_; };

        // --------------------------------------------------------------------------
        // Internal
        // --------------------------------------------------------------------------
      private:
        /**
         * @brief Set the connection ID
         *
         * @details The MOQ Handler sets the connection ID
         */
        void SetConnectionId(uint64_t conn_id) { conn_id_ = conn_id; };

        // --------------------------------------------------------------------------
        // Member variables
        // --------------------------------------------------------------------------

        const Bytes track_namespace_;
        const Bytes track_name_;
        std::optional<uint64_t> track_alias_;

        uint64_t conn_id_;

        /**
         * subscribe_id_ is the primary index/key for subscribe subscribe context/delegate storage.
         *   It is use as the subscribe_id in MoQT related subscribes.  Subscribe ID will adapt
         *   to received subscribe IDs, so the value will reflect either the received subscribe ID
         *   or the next one that increments from last received ID.
         */
        std::optional<uint64_t> subscribe_id_;

        uint64_t prev_group_id_{ 0 };
        uint64_t prev_object_id_{ 0 };
    };

} // namespace moq::transport
