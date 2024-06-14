/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */
#pragma once

#include <quicr/moq_messages.h>
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
              _track_namespace(track_namespace)
             , _track_name(track_name)
             , _track_mode(track_mode)
             , _logger(std::make_shared<cantina::Logger>("MTD", logger))
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
         * @param[in] object   Object to send to track
         *
         * @returns SendError status of the send
         *
         */
        SendError sendObject(const std::span<const uint8_t>& object);
        SendError sendObject(const std::span<const uint8_t>& object, uint32_t ttl);
        SendError sendObject(const std::span<const uint8_t>& object, uint8_t priority);
        SendError sendObject(const std::span<const uint8_t>& object, uint8_t priority, uint32_t ttl);

        /**
         * @brief Read object from track
         *
         * @details Reads an object from the subscribed track
         *
         * @param[out] object   Refence to object to be updated. Will be cleared.
         *
         * @returns ReadError status of the read
         */
        ReadError readObject(std::vector<const uint8_t>& object);

        /**
         * @brief Current track read status
         *
         * @details Obtains the current track read status/state
         *
         * @returns current TrackReadStatus
         */
        TrackReadStatus statusRead();

        /**
         * @brief Current track send status
         *
         * @details Obtains the current track send status/state
         *
         * @returns current TrackSendStatus
         */
        TrackSendStatus statusSend();

        /**
         * @brief set/update the default priority for published objects
         */
        void setDefaultPriority(uint8_t priority) { _def_priority = priority; }

        /**
         * @brief set/update the default TTL expirty for published objects
         */
        void setDefaultTTL(uint32_t ttl) { _def_ttl = ttl; }

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
        virtual void cb_objectReceived(std::vector<uint8_t>&& object) = 0;

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

        // --------------------------------------------------------------------------
        // --------------------------------------------------------------------------

      private:
        const bytes _track_namespace;
        const bytes _track_name;
        [[maybe_unused]] TrackMode _track_mode;
        uint8_t _def_priority;
        uint32_t _def_ttl;
        std::optional<uint64_t> _track_alias;
        cantina::LoggerPointer _logger;
    };

} // namespace quicr