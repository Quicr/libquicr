// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/publish_track_handler.h"
#include "quicr/track_name.h"

#include <map>
#include <memory>

namespace quicr {

    class Transport;

    class PublishNamespaceHandler
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

        virtual std::weak_ptr<PublishTrackHandler> PublishTrack(const FullTrackName& full_track_name,
                                                                TrackMode track_mode,
                                                                uint8_t default_priority,
                                                                uint32_t default_ttl);

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

        /**
         * @brief Sets the reqeust ID
         * @details MoQ instance sets the request id based on subscribe track method call. Request
         *      id is specific to the connection, so it must be set by the moq instance/connection.
         *
         * @param request_id          62bit request ID
         */
        void SetRequestId(std::optional<uint64_t> request_id) { request_id_ = request_id; }

        /**
         * @brief Get the request ID
         *
         * @return nullopt if not subscribed, otherwise the request ID
         */
        std::optional<uint64_t> GetRequestId() const noexcept { return request_id_; }

      protected:
        virtual std::shared_ptr<PublishTrackHandler> CreateHandler(const FullTrackName& full_track_name,
                                                                   TrackMode track_mode,
                                                                   uint8_t default_priority,
                                                                   uint32_t default_ttl);

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

      private:
        /// Prefix namespace for contained handlers.
        const TrackNamespace prefix_;

        /// Weak reference to the transport.
        std::weak_ptr<Transport> transport_;

        std::map<TrackFullNameHash, std::shared_ptr<PublishTrackHandler>> handlers_;

        Status status_{ Status::kNotPublished };

        std::optional<Error> error_{};

        quicr::ConnectionHandle connection_handle_{};

        std::optional<uint64_t> request_id_{};

        friend class Transport;
        friend class Client;
        friend class Server;
    };
}
