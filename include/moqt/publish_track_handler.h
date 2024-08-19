/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */
#pragma once

#include <moqt/core/base_track_handler.h>

namespace moq::transport {

    /**
     * @brief MOQ track handler for published track
     *
     * @details MOQ publish track handler defines all track related callbacks and
     *  functions for publish. Track handler operates on a single track (namespace + name).
     *
     *  This extends the base track handler to add publish (aka send) handling
     */
    class PublishTrackHandler : protected BaseTrackHandler
    {
      public:
        friend class Transport;

        /**
         * @brief Error codes
         */
        enum class Error : uint8_t
        {
            kOk = 0,
            kInternalError,
            kNotAuthorized,
            kNotAnnounced,
            kNoSubscribers
        };

        /**
         * @brief  Status codes
         */
        enum class Status : uint8_t
        {
            kOK = 0,
            kNotConnected,
            kNotAnnounced,
            kPendingAnnounceResponse,
            kAnnounceNotAuthorized,
            kNoSubscribers,
            kSendingUnannounce
        };

        struct SendParams
        {
         std::optional<uint64_t> group_id;      ///< Object group ID - Should be derived using time in microseconds
         std::optional<uint64_t> object_id;     ///< Object ID - Start at zero and increment for each object in group
         std::optional<uint32_t> prioirty;      ///< Priority of the object, lower value is better
         std::optional<uint16_t> ttl;           ///< Object time to live in milliseconds

        };

        // --------------------------------------------------------------------------
        // Public API methods that normally should not be overridden
        // --------------------------------------------------------------------------

        /**
         * @brief Publish track handler constructor
         *
         * @param track_namespace       Opaque binary array of bytes track namespace
         * @param track_name            Opaque binary array of bytes track name
         * @param track_mode            The track mode to operate using
         * @param default_priority      Default priority for objects if not specified in SendParams
         * @param default_ttl           Default TTL for objects if not specified in SendParams
         */
        PublishTrackHandler(const Bytes& track_namespace,
                            const Bytes& track_name,
                            TrackMode track_mode,
                            uint8_t default_priority,
                            uint32_t default_ttl)
          : BaseTrackHandler(track_namespace, track_name)
          , track_mode_(track_mode)
          , def_priority_(default_priority)
          , def_ttl_(default_ttl)
        {
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
        virtual void StatusChanged(PublishTrackHandler::Status status) = 0;

        // --------------------------------------------------------------------------
        // Various getter/setters
        // --------------------------------------------------------------------------
        /**
         * @brief set/update the default priority for published objects
         */
        void SetDefaultPriority(uint8_t priority) { def_priority_ = priority; }

        /**
         * @brief set/update the default TTL expiry for published objects
         */
        void SetDefaultTTL(uint32_t ttl) { def_ttl_ = ttl; }

        /**
         * @brief Get the publish status
         *
         * @return Status of publish
         */
        Status GetStatus() { return publish_status_; }

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
         * @param
         * @param[in] object_id    Object ID of the object
         * @param[in] object       Object to publish to track
         * @param[in] ttl          Expire TTL for object
         * @param[in] priority     Priority for object; will be set upon next qualifing stream object
         *
         * @returns Error status of the publish
         */
        Error PublishObject(const SendParams send_params, BytesSpan object);

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
        using PublishObjFunction = std::function<Error(uint8_t priority,
                                                       uint32_t ttl,
                                                       bool stream_header_needed,
                                                       uint64_t group_id,
                                                       uint64_t object_id,
                                                       BytesSpan data)>;
        /**
         * @brief Set the Data context ID
         *
         * @details The MOQ Handler sets the data context ID
         */
        void SetDataContextId(uint64_t data_ctx_id) { publish_data_ctx_id_ = data_ctx_id; };

        /**
         * @brief Get the Data context ID
         */
        uint64_t GetDataContextId() { return publish_data_ctx_id_; };

        void SetPublishObjectFunction(PublishObjFunction&& publish_func)
        {
            publish_object_func_ = std::move(publish_func);
        }

        /**
         * @brief Set the publish status
         * @param status                Status of publishing (aka publish objects)
         */
        void SetStatus(Status status) { publish_status_ = status; }

        // --------------------------------------------------------------------------
        // Member variables
        // --------------------------------------------------------------------------

        Status publish_status_{ Status::kNotAnnounced };
        TrackMode track_mode_;
        uint8_t def_priority_;              // Set by caller and is used when priority is not specified
        uint32_t def_ttl_;                  // Set by caller and is used when TTL is not specified

        uint64_t publish_data_ctx_id_;      // publishing data context ID
        PublishObjFunction publish_object_func_;

        bool sent_track_header_ { false };  // Used only in stream per track mode
    };

} // namespace moq::transport
