/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */
#pragma once

#include "cantina/logger.h"
#include <quicr/moqt_base_track_handler.h>
#include <quicr/quicr_common.h>

namespace quicr {

    /**
     * @brief MOQ track handler for published track
     *
     * @details MOQ publish track handler defines all track related callbacks and
     *  functions for publish. Track handler operates on a single track (namespace + name).
     *
     *  This extends the base track handler to add publish (aka send) handling
     */
    class MOQTPublishTrackHandler : protected MOQTBaseTrackHandler
    {
      public:
        friend class MOQTCore;

        enum class Error : uint8_t
        {
            OK = 0,
            INTERNAL_ERROR,
            NOT_AUTHORIZED,
            NOT_ANNOUNCED,
            NO_SUBSCRIBERS,
        };

        enum class Status : uint8_t
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
        MOQTPublishTrackHandler(const bytes& track_namespace,
                                const bytes& track_name,
                                TrackMode track_mode,
                                uint8_t default_priority,
                                uint32_t default_ttl,
                                const cantina::LoggerPointer& logger)
          : MOQTBaseTrackHandler(track_namespace, track_name, logger)
        {
            setTrackMode(track_mode);
            setDefaultPriority(default_priority);
            setDefaultTTL(default_ttl);
        }

        // --------------------------------------------------------------------------
        // Public Virtual API callback event methods to be overridden
        // --------------------------------------------------------------------------

        /**
         * @brief Notification of publish status
         * @details Notification of publishing status, such as when it's ready to publish or not ready to publish
         *
         * @param status        Indicates the status of being able to publish
         */
        virtual void statusCallback(MOQTPublishTrackHandler::Status status) = 0;

        /**
         * @brief Notification that the publish queue is congested
         * @details Notification indicates that publish queue is backlogged and publishing more
         *   will likely cause more congestion.
         *
         * @param cleared             Indicates if congestion has cleared
         * @param objects_in_queue    Number of objects still pending to be sent at time of notification
         */
        virtual void congestedCallback(bool cleared, uint64_t objects_in_queue) = 0;

        // --------------------------------------------------------------------------
        // Various getter/setters
        // --------------------------------------------------------------------------
        /**
         * @brief set/update the track mode for publishing
         */
        void setTrackMode(TrackMode track_mode) { _mi_track_mode = track_mode; }

        /**
         * @brief set/update the default priority for published objects
         */
        void setDefaultPriority(uint8_t priority) { _def_priority = priority; }

        /**
         * @brief set/update the default TTL expirty for published objects
         */
        void setDefaultTTL(uint32_t ttl) { _def_ttl = ttl; }

        /**
         * @brief Get the publish status
         *
         * @return Status of publish
         */
        Status getStatus() { return _publish_status; }

        // --------------------------------------------------------------------------
        // Methods that normally do not need to be overridden
        // --------------------------------------------------------------------------

        /**
         * @brief Publish object to announced track
         *
         * @details Publish object to announced track that was previously announced.
         *   This will have an error if the track wasn't announced yet. Status will
         *   indicate if there are no subscribers. In this case, the object will
         *   not be sent.
         *
         * @param[in] group_id     Group ID of object
         * @param[in] object_id    Object ID of the object
         * @param[in] object       Object to publish to track
         * @param[in] ttl          Expire TTL for object
         * @param[in] priority     Priority for object; will be set upon next qualifing stream object
         *
         * @returns PublishError status of the publish
         */
        Error publishObject(const uint64_t group_id,
                            const uint64_t object_id,
                            std::span<const uint8_t> object,
                            uint8_t priority,
                            uint32_t ttl);
        Error publishObject(const uint64_t group_id, const uint64_t object_id, std::span<const uint8_t> object);
        Error publishObject(const uint64_t group_id,
                            const uint64_t object_id,
                            std::span<const uint8_t> object,
                            uint32_t ttl);
        Error publishObject(const uint64_t group_id,
                            const uint64_t object_id,
                            std::span<const uint8_t> object,
                            uint8_t priority);


        // --------------------------------------------------------------------------
        // Internals
        // --------------------------------------------------------------------------
      private:
        /**
         * @brief Publish Object function via the MoQ instance
         *
         * @details This is set by the MoQInstance.
         *   Publish object function provides direct access to the MoQInstance that will publish
         *   the object based on the track mode.
         *
         * @param priority              Priority to use for object; set on next track change
         * @param ttl                   Expire time to live in milliseconds
         * @param stream_header_needed  Indicates if group or track header is needed before this data object
         * @param data                  Raw data/object that should be transmitted - MoQInstance serializes the data
         */
        using publishObjFunction = std::function<Error(uint8_t priority,
                                                       uint32_t ttl,
                                                       bool stream_header_needed,
                                                       uint64_t group_id,
                                                       uint64_t object_id,
                                                       std::span<const uint8_t> data)>;
        /**
         * @brief Set the Data context ID
         *
         * @details The MOQ Handler sets the data context ID
         */
        void set_data_context_id(uint64_t data_ctx_id) { _mi_publish_data_ctx_id = data_ctx_id; };

        /**
         * @brief Get the Data context ID
         */
        uint64_t get_data_context_id() { return _mi_publish_data_ctx_id; };

        void set_publish_object_function(publishObjFunction&& publish_func)
        {
            _mi_publishObjFunc = std::move(publish_func);
        }

        /**
         * @brief Set the publish status
         * @param status                Status of publishing (aka publish objects)
         */
        void set_status(Status status) { _publish_status = status; }

        // --------------------------------------------------------------------------
        // Member variables
        // --------------------------------------------------------------------------

        Status _publish_status{ Status::NOT_ANNOUNCED };
        TrackMode _mi_track_mode;
        uint8_t _def_priority;            // Set by caller and is used when priority is not specified
        uint32_t _def_ttl;                // Set by caller and is used when TTL is not specified
        uint64_t _mi_publish_data_ctx_id; // publishing data context ID
        publishObjFunction _mi_publishObjFunc;

        bool _sent_track_header{ false }; // Used only in stream per track mode
    };

} // namespace quicr
