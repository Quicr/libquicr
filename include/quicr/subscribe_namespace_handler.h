// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/subscribe_track_handler.h"
#include "quicr/track_name.h"

#include <map>
#include <memory>

namespace quicr {

    class Transport;

    class SubscribeNamespaceHandler : public BaseTrackHandler
    {
      public:
        using Error = std::pair<messages::ErrorCode, messages::ReasonPhrase>;

        /**
         * @brief  Status codes for the subscribe track
         */
        enum class Status : uint8_t
        {
            kOk = 0,
            kNotSubscribed,
            kError,
        };

      protected:
        SubscribeNamespaceHandler(const TrackNamespace& prefix);

      public:
        static auto Create(const TrackNamespace& prefix)
        {
            return std::shared_ptr<SubscribeNamespaceHandler>(new SubscribeNamespaceHandler(prefix));
        }

        virtual ~SubscribeNamespaceHandler();

        /**
         * @brief Notification of subscribe status
         *
         * @details Notification of the subscribe status
         *
         * @param status        Indicates status of the subscribe
         */
        virtual void StatusChanged(Status status);

        /**
         * @brief Callback for the app to inspect and accept or deny a new track
         *
         * @details The application implements the callback to accept a track. The
         *      application accepts the track by creating a subscribe track handler
         *      and returning that as a shared pointer. The application and
         *      set various subscribe track parameters as needed.
         *      If a subscribe track handler is returned, the handler will
         *      be updated to support PUBLISH initiated flow and
         *      will issue a libquicr SubscribeTrack(). The application does not
         *      call SubscribeTrack() for accepted tracks via the namespace.
         *      The application is responsible for UnsubscribeTrack() and
         *      other subscribe track related uses.
         *
         * @param attributes        Publish attributes, which includes the track fullname
         * @return
         */
        virtual std::shared_ptr<SubscribeTrackHandler> NewTrackReceived(
          const messages::PublishAttributes& attributes) const;

        const TrackNamespace& GetPrefix() const noexcept { return prefix_; }

        const std::weak_ptr<Transport>& GetTransport() const noexcept { return transport_; }
        void SetTransport(const std::shared_ptr<Transport>& new_transport) noexcept { transport_ = new_transport; }

        /**
         * @brief Get the status of the subscribe
         *
         * @return Status of the subscribe
         */
        constexpr Status GetStatus() const noexcept { return status_; }

        /**
         * @brief Get the error code and reason for the subscribe namespace, if any.
         * @return Subscribe namespace error code and reason.
         */
        std::optional<Error> GetError() const noexcept { return error_; }

      protected:
        /**
         * @brief Set the subscribe status
         * @param status                Status of the subscribe
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

      private:
        /// Prefix namespace for contained handlers.
        const TrackNamespace prefix_;

        /// Weak reference to the transport.
        std::weak_ptr<Transport> transport_;

        Status status_{ Status::kNotSubscribed };

        std::optional<Error> error_{};

        DataContextId data_ctx_id_{ 0 };

        friend class Transport;
        friend class Client;
        friend class Server;
    };
}
