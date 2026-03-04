// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/publish_track_handler.h"
#include "quicr/track_name.h"

#include <map>
#include <memory>

namespace quicr {

    class Transport;

    class PublishNamespaceHandler : public BaseTrackHandler
    {
      public:
        using Error = std::pair<messages::ErrorCode, messages::ReasonPhrase>;

        /**
         * @brief  Status codes for the Publish track
         */
        enum class Status : uint8_t
        {
            kOk = 0,
            kNotConnected,
            kNotPublished,
            kPendingResponse,
            kPublishNotAuthorized,
            kSendingDone, ///< In this state, callbacks will not be called
            kError,
        };

      protected:
        PublishNamespaceHandler(const TrackNamespace& prefix);

        /**
         * @brief Notification of Publish status
         *
         * @details Notification of the Publish status
         *
         * @param status        Indicates status of the Publish
         */
        virtual void StatusChanged(Status status);

      public:
        static auto Create(const TrackNamespace& prefix)
        {
            return std::shared_ptr<PublishNamespaceHandler>(new PublishNamespaceHandler(prefix));
        }

        virtual ~PublishNamespaceHandler();

        /**
         * @brief Publish a new track
         *
         * @details Creates a new publish track handler that will be used to send data for a track.
         *      Selection and filters may be applied here.
         *
         * @param handler           Publish Track Handler for the publish
         */
        virtual void PublishTrack(std::shared_ptr<PublishTrackHandler> handler);

        /**
         * @brief Remove a publish track
         */
        virtual void UnPublishTrack(std::shared_ptr<PublishTrackHandler> handler);

        /**
         * @brief Passthrough PublishObject to Publish Track matching full name hash
         *
         * @details Passthrough to send to the publish handler. Selection and filters maybe applied.
         *
         * @param track_full_name_hash  Hash of track namespace and name
         * @param object_headers        Object headers, must include group and object Ids
         * @param data                  Full complete payload data for the object
         *
         * @returns PublishObjectStatus from publish handler
         */
        virtual PublishTrackHandler::PublishObjectStatus PublishObject(TrackFullNameHash track_full_name_hash,
                                                                       const ObjectHeaders& object_headers,
                                                                       BytesSpan data);

        /**
         * @brief Passthrough to Forward received object data to each publish handler
         *
         * @details Passthrough to send to the publish handlers. Selection and filters can be applied.
         *
         * @note This method must be overwritten to be used. Default does not forward any data.
         *
         * @param track_full_name_hash  Hash of track namespace and name
         * @param is_new_stream         Indicates if this data starts a new stream
         * @param group_id              Group ID for stream
         * @param subgroup_id           Subgroup ID for stream
         * @param data                  MoQ data to send
         *
         * @returns PublishObjectStatus from publish handler
         */
        virtual PublishTrackHandler::PublishObjectStatus ForwardPublishedData(
          [[maybe_unused]] TrackFullNameHash track_full_name_hash,
          [[maybe_unused]] bool is_new_stream,
          [[maybe_unused]] uint64_t group_id,
          [[maybe_unused]] uint64_t subgroup_id,
          [[maybe_unused]] std::shared_ptr<const std::vector<uint8_t>> data)
        {
            return PublishTrackHandler::PublishObjectStatus::kInternalError;
        }

        const TrackNamespace& GetPrefix() const noexcept { return prefix_; }

        const std::weak_ptr<Transport>& GetTransport() const noexcept { return transport_; }

        void SetTransport(const std::shared_ptr<Transport>& new_transport) noexcept { transport_ = new_transport; }

        /**
         * @brief Get the status of the Publish
         *
         * @return Status of the Publish
         */
        constexpr Status GetStatus() const noexcept { return status_; }

        /**
         * @brief Get the error code and reason for the Publish namespace, if any.
         * @return Publish namespace error code and reason.
         */
        std::optional<Error> GetError() const noexcept { return error_; }

      protected:
        /**
         * @brief Set the Publish status
         * @param status                Status of the Publish
         */
        void SetStatus(const Status status) noexcept
        {
            status_ = status;
            if (status == Status::kError && !error_.has_value()) {
                const std::string reason = "Unknown error";
                error_ = {
                    messages::ErrorCode::kInternalError,
                    Bytes{ reason.begin(), reason.end() },
                };
            }

            StatusChanged(status);
        }

        void SetError(const Error& error)
        {
            error_ = error;
            SetStatus(Status::kError);
        }

        void RequestOk(uint64_t, const messages::Parameters&) override { SetStatus(Status::kOk); }

        void RequestError(messages::ErrorCode error_code, std::string reason) override
        {
            SetError(Error{ error_code, Bytes{ reason.begin(), reason.end() } });
        }

        // Publish handlers used to transmit track data
        std::map<TrackFullNameHash, std::shared_ptr<PublishTrackHandler>> handlers_;

      private:
        /// Prefix namespace for contained handlers.
        const TrackNamespace prefix_;

        /// Weak reference to the transport.
        std::weak_ptr<Transport> transport_;

        Status status_{ Status::kNotPublished };

        std::optional<Error> error_{};

        ConnectionHandle connection_handle_{ 0 };

        friend class Transport;
        friend class Client;
        friend class Server;
    };
}
