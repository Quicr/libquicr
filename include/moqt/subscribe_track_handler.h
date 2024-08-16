/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */
#pragma once

#include "cantina/logger.h"
#include <moqt/core/base_track_handler.h>

namespace moq::transport {

    /**
     * @brief MOQ track handler for subscribed track
     *
     * @details MOQ subscribe track handler defines all track related callbacks and
     *  functions for subscribe. Track handler operates on a single track (namespace + name).
     *
     *  This extends the base track handler to add subscribe handling
     */
    class SubscribeTrackHandler : public BaseTrackHandler
    {
      public:
        friend class Transport;

        enum class Error : uint8_t
        {
            OK = 0,
            NOT_AUTHORIZED,
            NOT_SUBSCRIBED,
            NO_DATA,
        };

        enum class Status : uint8_t
        {
            OK = 0,
            NOT_CONNECTED,
            SUBSCRIBE_ERROR,
            NOT_AUTHORIZED,
            NOT_SUBSCRIBED,
            PENDING_SUBSCRIBE_RESPONSE,
            SENDING_UNSUBSCRIBE               // Triggers callbacks to not be called in this state
        };

        // --------------------------------------------------------------------------
        // Public API methods that normally should not be overridden
        // --------------------------------------------------------------------------

        /**
         * @brief Track delegate constructor
         */
        SubscribeTrackHandler(const bytes& track_namespace,
                                  const bytes& track_name,
                                  const cantina::LoggerPointer& logger)
          : BaseTrackHandler(track_namespace, track_name, logger)
        {
        }

        /**
         * @brief Get the status of the subscribe
         *
         * @return Status of the subscribe
         */
        Status getStatus() { return _status; }

        // --------------------------------------------------------------------------
        // Public Virtual API callback event methods to be overridden
        // --------------------------------------------------------------------------

        /**
         * @brief Notification of received data object
         *
         * @details Event notification to provide the caller the received data object
         *
         * @param group_id          Group ID of the object received
         * @param object_id         Object ID of the object received
         * @param priority          Priority of the object received
         * @param object            Data object received
         * @param track_mode        Track mode the object was received
         */
        virtual void objectReceived(uint64_t group_id,
                                    uint64_t object_id,
                                    uint8_t priority,
                                    std::vector<uint8_t>&& object,
                                    TrackMode track_mode) = 0;

        /**
         * @brief Notification of subscribe status
         *
         * @details Notification of the subscribe status
         *
         * @param status        Indicates status of the subscribe
         */
        virtual void statusChanged(Status status) = 0;

        // --------------------------------------------------------------------------
        // Internal
        // --------------------------------------------------------------------------
      private:
        /**
         * @brief Set the subscribe status
         * @param status                Status of the subscribe
         */
        void set_status(Status status) { _status = status; }

        // --------------------------------------------------------------------------
        // Member variables
        // --------------------------------------------------------------------------
        Status _status{ Status::NOT_SUBSCRIBED };
    };

} // namespace quicr
