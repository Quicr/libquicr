/*
*  Copyright (C) 2024
*  Cisco Systems, Inc.
*  All Rights Reserved
*/
#pragma once

#include "cantina/logger.h"
#include <quicr/moq_base_track_handler.h>
#include <quicr/quicr_common.h>

namespace quicr {

   /**
    * @brief MOQ track handler for subscribed track
    *
    * @details MOQ subscribe track handler defines all track related callbacks and
    *  functions for subscribe. Track handler operates on a single track (namespace + name).
    *
    *  This extends the base track handler to add subscribe (aka read) handling
    */
   class MoQSubscribeTrackHandler : public MoQBaseTrackHandler
   {
     public:
       enum class ReadError : uint8_t
       {
           OK = 0,
           NOT_AUTHORIZED,
           NOT_SUBSCRIBED,
           NO_DATA,
       };

       enum class TrackReadStatus : uint8_t
       {
           OK = 0,
           NOT_CONNECTED,
           SUBSCRIBE_ERROR,
           NOT_AUTHORIZED,
           NOT_SUBSCRIBED,
           PENDING_SUBSCRIBE_RESPONSE
       };

       // --------------------------------------------------------------------------
       // Public API methods that normally should not be overridden
       // --------------------------------------------------------------------------

       /**
        * @brief Track delegate constructor
        */
       MoQSubscribeTrackHandler(const bytes& track_namespace,
                                      const bytes& track_name,
                                      TrackMode track_mode,
                                      uint8_t default_priority,
                                      uint32_t default_ttl,
                                      const cantina::LoggerPointer& logger)
         : MoQBaseTrackHandler(track_namespace, track_name, track_mode, default_priority, default_ttl, logger) {}

       /**
        * @brief Set the read status
        * @param status                Status of reading (aka subscribe)
        */
       void setReadStatus(TrackReadStatus status) { _read_status = status; }
       TrackReadStatus getReadStatus() { return _read_status; }

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
        * @parma priority          Priority of the object received
        * @param object            Data object received
        * @param track_mode        Track mode the object was received
        */
       virtual void cb_objectReceived(uint64_t group_id,
                                      uint64_t object_id,
                                      uint8_t priority,
                                      std::vector<uint8_t>&& object,
                                      TrackMode track_mode) = 0;


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


     private:
       TrackReadStatus _read_status{ TrackReadStatus::NOT_SUBSCRIBED };
   };

} // namespace quicr
