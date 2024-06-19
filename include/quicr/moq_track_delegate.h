/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */
#pragma once

#include <quicr/quicr_common.h>

namespace quicr {

    /**
     * @brief MOQ track delegate for subscribe and publish
     *
     * @details MOQ track delegate defines all track related callbacks and
     *  functions. Track delegate operates on a single track (namespace + name).
     *  It can be used for subscribe, publish, or both subscribe and publish. The
     *  only requirement is that the namespace and track alias be the same.
     */
    class MoQTrackDelegate
    {
      public:
        enum class ReadError : uint8_t
        {
            OK = 0,
            NOT_AUTHORIZED,
            NOT_SUBSCRIBED,
            NO_DATA,
        };

        enum class SendError : uint8_t
        {
            OK = 0,
            INTERNAL_ERROR,
            NOT_AUTHORIZED,
            NOT_ANNOUNCED,
            NO_SUBSCRIBERS,
        };

        enum class TrackReadStatus : uint8_t
        {
            OK = 0,
            NOT_AUTHORIZED,
            NOT_SUBSCRIBED,
            PENDING_SUBSCRIBE_RESPONSE,
            SUBSCRIBE_NOT_AUTHORIZED
        };

        enum class TrackSendStatus : uint8_t
        {
            OK = 0,
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

        /**
         * @brief Track delegate constructor
         */
        MoQTrackDelegate(const bytes& track_namespace,
                         const bytes& track_name,
                         TrackMode track_mode,
                         uint8_t default_priority,
                         uint32_t default_ttl,
                         const cantina::LoggerPointer& logger) :
              _mi_track_mode(track_mode)
             , _logger(std::make_shared<cantina::Logger>("MTD", logger))
             , _track_namespace(track_namespace)
             , _track_name(track_name)
        {
          this->setDefaultPriority(default_priority);
          this->setDefaultTTL(default_ttl);
        }

        /**
         * @brief Send object to announced track
         *
         * @details Send object to announced track that was previously announced.
         *   This will have an error if the track wasn't announced yet. Status will
         *   indicate if there are no subscribers. In this case, the object will
         *   not be sent.
         *
         * @param[in] group_id     Group ID of object
         * @param[in] object_id    Object ID of the object
         * @param[in] object       Object to send to track
         * @param[in] ttl          Expire TTL for object
         * @param[in] priority     Priority for object; will be set upon next qualifing stream object
         *
         * @returns SendError status of the send
         *
         */
        SendError sendObject(const uint64_t group_id, const uint64_t object_id, const std::span<const uint8_t>& object);
        SendError sendObject(const uint64_t group_id,
                             const uint64_t object_id,
                             const std::span<const uint8_t>& object,
                             uint32_t ttl);
        SendError sendObject(const uint64_t group_id,
                             const uint64_t object_id,
                             const std::span<const uint8_t>& object,
                             uint8_t priority);
        SendError sendObject(const uint64_t group_id,
                             const uint64_t object_id,
                             const std::span<const uint8_t>& object,
                             uint8_t priority,
                             uint32_t ttl);

        /**
         * @brief Current track read status
         *
         * @details Obtains the current track read status/state
         *
         * @returns current TrackReadStatus
         */
        TrackReadStatus statusRead() { return _read_status; }

        /**
         * @brief Current track send status
         *
         * @details Obtains the current track send status/state
         *
         * @returns current TrackSendStatus
         */
        TrackSendStatus statusSend() { return _send_status; }

        /**
         * @brief set/update the default priority for published objects
         */
        void setDefaultPriority(uint8_t priority) { _def_priority = priority; }

        /**
         * @brief set/update the default TTL expirty for published objects
         */
        void setDefaultTTL(uint32_t ttl) { _def_ttl = ttl; }

        /**
         * @brief set/update the track mode for sending
         */
        void setTrackMode(TrackMode track_mode) { _mi_track_mode = track_mode; }

        // --------------------------------------------------------------------------
        // Public Virtual API callback event methods to be overridden
        // --------------------------------------------------------------------------

        /**
         * @brief Notificaiton of received data object
         *
         * @details Event notification to provide the caller the received data object
         *
         * @param object    Data object received
         */
        virtual void cb_objectReceived(uint64_t group_id,
                                       uint64_t object_id,
                                       std::vector<uint8_t>&& object) = 0;

        /**
         * @brief Notification that data can be sent
         * @details Notification that an announcement has been successful and there is at least one
         *   subscriber for the track. Data can now be succesfully sent.
         */
        virtual void cb_sendReady() = 0;

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

        /**
         * @brief Notification to indicate reading is ready
         * @details Notification that an announcement has been successful and but
         * there are no subscribers, so data cannot be sent yet.
         */
        virtual void cb_readReady() = 0;

        /**
         * @brief Notification that read is not available
         *
         * @param status        Indicates the reason for why data cannot be sent [yet]
         */
        virtual void cb_readNotReady(TrackReadStatus status) = 0;

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
        void setSubscribeId(uint64_t subscribe_id) { _subscribe_id = subscribe_id; }

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
         * @brief Set the send status
         * @param status                Status of sending (aka publish objects)
         */
        void setSendStatus(TrackSendStatus status) { _send_status = status; }


        // --------------------------------------------------------------------------
        // MOQ Instance specific public variables/methods - Caller should not use these
        // --------------------------------------------------------------------------

        /**
         * @brief Send Object function via the MoQ instance
         *
         * @details This is set by the MoQInstance.
         *   Send object function provides direct access to the MoQInstance that will send
         *   the object based on the track mode.
         *
         * @param priority              Priority to use for object; set on next track change
         * @param ttl                   Expire time to live in milliseconds
         * @param stream_header_needed  Indicates if group or track header is needed before this data object
         * @param data                  Raw data/object that should be transmitted - MoQInstance serializes the data
         */
        std::function<SendError(uint8_t priority,
                                uint32_t ttl,
                                bool stream_header_needed,
                                uint64_t group_id,
                                uint64_t object_id,
                                bytes&& data)> _mi_sendObjFunc;

        uint64_t _mi_send_data_ctx_id;    // Set by moq instance
        uint64_t _mi_conn_id;             // Set by moq instance
        TrackMode _mi_track_mode;         // Set by moq instance
        bool _mi_stream_header_received { false }; // True if stream header is complete, false if pending

        uint8_t _def_priority;            // Set by caller and is used when priority is not specified
        uint32_t _def_ttl;                // Set by caller and is used when TTL is not specified

        // --------------------------------------------------------------------------
        // --------------------------------------------------------------------------

      protected:
        cantina::LoggerPointer _logger;
        const bytes _track_namespace;
        const bytes _track_name;
        std::optional<uint64_t> _track_alias;

        /**
         * _subscribe_id is the primary index/key for subscribe subscribe context/delegate storage.
         *   It is use as the subscribe_id in MOQT related subscribes.  Subscribe ID will adapt
         *   to received subscribe IDs, so the value will reflect either the received subscribe ID
         *   or the next one that increments from last received ID.
         */
        std::optional<uint64_t> _subscribe_id;

        TrackSendStatus _send_status { TrackSendStatus::NOT_ANNOUNCED };
        TrackReadStatus _read_status { TrackReadStatus::NOT_SUBSCRIBED };

      private:
        uint64_t _prev_group_id {0};
        bool _sent_track_header { false };  // Used only in stream per tarck mode
    };

} // namespace quicr
