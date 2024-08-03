/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */
#pragma once

#include "cantina/logger.h"
#include <quicr/quicr_common.h>
#include <quicr/moq_base_track_handler.h>

namespace quicr {

    /**
     * @brief MOQ track handler for published track
     *
     * @details MOQ publish track handler defines all track related callbacks and
     *  functions for publish. Track handler operates on a single track (namespace + name).
     *
     *  This extends the base track handler to add publish (aka send) handling
     */
    class MoQPublishTrackHandler : protected MoQBaseTrackHandler
    {
      public:
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

        // --------------------------------------------------------------------------
        // Public API methods that normally should not be overridden
        // --------------------------------------------------------------------------

        /**
         * @brief Track delegate constructor
         */
        MoQPublishTrackHandler(const bytes& track_namespace,
                               const bytes& track_name,
                               TrackMode track_mode,
                               uint8_t default_priority,
                               uint32_t default_ttl,
                               const cantina::LoggerPointer& logger)
          : MoQBaseTrackHandler(track_namespace, track_name, track_mode, default_priority, default_ttl, logger) {}

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
        SendError sendObject(const uint64_t group_id, const uint64_t object_id, std::span<const uint8_t> object);
        SendError sendObject(const uint64_t group_id,
                             const uint64_t object_id,
                             std::span<const uint8_t> object,
                             uint32_t ttl);
        SendError sendObject(const uint64_t group_id,
                             const uint64_t object_id,
                             std::span<const uint8_t> object,
                             uint8_t priority);
        SendError sendObject(const uint64_t group_id,
                             const uint64_t object_id,
                             std::span<const uint8_t> object,
                             uint8_t priority,
                             uint32_t ttl);

        /**
         * @brief Set the send status
         * @param status                Status of sending (aka publish objects)
         */
        void setSendStatus(TrackSendStatus status) { _send_status = status; }
        TrackSendStatus getSendStatus() { return _send_status; }

        // --------------------------------------------------------------------------
        // Public Virtual API callback event methods to be overridden
        // --------------------------------------------------------------------------

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


      private:
        TrackSendStatus _send_status{ TrackSendStatus::NOT_ANNOUNCED };
        bool _sent_track_header{ false }; // Used only in stream per track mode
    };

} // namespace quicr
