// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/errors.h"
#include "quicr/handlers/subscribe_track_handler.h"
#include "quicr/track_name.h"

#include <map>
#include <memory>

namespace quicr {

    class Session;

    class SubscribeNamespaceHandler : public TrackHandler
    {
      public:
        enum class Mode
        {
            // Receive NAMESPACE notifications of matching namespaces.
            kNamespaces,
            // Receive PUBLISH notifications of matching tracks.
            kTracks
        };

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
        SubscribeNamespaceHandler(const TrackNamespace& prefix,
                                  Mode mode,
                                  const messages::Filter& filter = std::monostate{});

      public:
        static auto Create(const TrackNamespace& prefix,
                           const Mode mode,
                           const messages::Filter& filter = std::monostate{})
        {
            return std::shared_ptr<SubscribeNamespaceHandler>(new SubscribeNamespaceHandler(prefix, mode, filter));
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

        const TrackNamespace& GetPrefix() const noexcept { return prefix_; }

        messages::FilterType GetFilterType() const noexcept { return messages::GetFilterType(filter_); }

        constexpr const messages::Filter& GetFilter() const noexcept { return filter_; }

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
        std::optional<Error<ErrorCode>> GetError() const noexcept { return error_; }

        /**
         * @brief Get the mode this handler is operating in.
         * @return Subscribe mode for this namespace handler.
         */
        constexpr Mode GetMode() const noexcept { return mode_; }

      protected:
        /**
         * @brief Set the subscribe status
         * @param status                Status of the subscribe
         */
        void SetStatus(const Status status) noexcept
        {
            status_ = status;
            if (status == Status::kError && !error_.has_value()) {
                error_ = Error<ErrorCode>{ ErrorCode::kInternalError, "Unknown error" };
            }
            StatusChanged(status);
        }

        void SetError(const Error<ErrorCode>& error)
        {
            error_ = error;
            SetStatus(Status::kError);
        }

        void RequestOkReceived(const messages::Parameters&) override;

        Reply<messages::Parameters, ErrorCode> RequestUpdateReceived(const messages::Parameters& params) override;

        void RequestError(ErrorCode error_code, std::string reason) override
        {
            SetError(Error<ErrorCode>{ error_code, std::move(reason) });
        }

      private:
        /// Mode in use.
        const Mode mode_;

        /// Prefix namespace for contained handlers.
        const TrackNamespace prefix_;

        /// Filter value for namespace subscription.
        messages::Filter filter_;

        Status status_{ Status::kNotSubscribed };

        std::optional<Error<ErrorCode>> error_{};

        friend class Session;
    };
}
