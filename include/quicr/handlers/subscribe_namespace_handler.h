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

    /**
     * @brief Receives namespace or track publication notifications below a prefix.
     *
     * @details Create the handler, customize its protected callbacks in a derived class, and pass it
     * to the session. Callback and status mutation occur on session processing paths; getters are
     * not synchronized with those callbacks.
     */
    class SubscribeNamespaceHandler : public TrackHandler
    {
      public:
        /// @brief Selects which notification family the peer sends for the prefix.
        enum class Mode
        {
            kNamespaces, ///< Receive NAMESPACE notifications for matching namespaces.
            kTracks      ///< Receive PUBLISH notifications for matching tracks.
        };

        /**
         * @brief  Status codes for the subscribe track
         */
        enum class Status : uint8_t
        {
            kOk = 0,        ///< The peer accepted the subscription.
            kNotSubscribed, ///< Initial state; no active namespace subscription.
            kError,         ///< The peer or library rejected the request; inspect `GetError()`.
        };

      protected:
        /**
         * @brief Constructs a namespace subscription.
         * @param prefix Namespace prefix against which notifications are matched.
         * @param mode Kind of notification to receive.
         * @param filter Protocol filter sent with the subscription.
         */
        SubscribeNamespaceHandler(const TrackNamespace& prefix,
                                  Mode mode,
                                  const messages::Filter& filter = std::monostate{});

      public:
        /**
         * @brief Creates a namespace subscription with shared ownership.
         * @param prefix Namespace prefix against which notifications are matched.
         * @param mode Kind of notification to receive.
         * @param filter Protocol filter sent with the subscription.
         * @return Shared ownership of the new handler.
         */
        static auto Create(const TrackNamespace& prefix,
                           const Mode mode,
                           const messages::Filter& filter = std::monostate{})
        {
            return std::shared_ptr<SubscribeNamespaceHandler>(new SubscribeNamespaceHandler(prefix, mode, filter));
        }

        /// @brief Destroys the handler; destruction currently sends no protocol message.
        virtual ~SubscribeNamespaceHandler();

        /**
         * @brief Notification of subscribe status
         *
         * @details Notification of the subscribe status
         *
         * @param status Indicates status of the subscribe
         * @note The default implementation does nothing. Called synchronously on a session path.
         */
        virtual void StatusChanged(Status status);

        /// @return Borrowed immutable prefix, valid for this handler's lifetime.
        const TrackNamespace& GetPrefix() const noexcept { return prefix_; }

        /// @return The protocol discriminator for `GetFilter()`.
        messages::FilterType GetFilterType() const noexcept { return messages::GetFilterType(filter_); }

        /// @return Borrowed immutable subscription filter, valid for this handler's lifetime.
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

        /**
         * @brief Fully qualifies a NAMESPACE suffix relative to the subscribed prefix.
         * @param suffix Relative namespace entries received from the peer.
         * @return A new namespace containing the prefix followed by the suffix.
         */
        TrackNamespace ExpandSuffix(const TrackNamespace& suffix) const;

      protected:
        /**
         * @brief Reports a NAMESPACE notification in `kNamespaces` mode.
         * @param name_space Namespace relative to `GetPrefix()`; use `ExpandSuffix()` to qualify it.
         * @note The default implementation does nothing. Called synchronously on a session path.
         */
        virtual void NamespaceReceived([[maybe_unused]] const TrackNamespace& name_space) {}

        /**
         * @brief Reports a NAMESPACE_DONE notification in `kNamespaces` mode.
         * @param name_space Namespace relative to `GetPrefix()`.
         * @note The default implementation does nothing. Called synchronously on a session path.
         */
        virtual void NamespaceDoneReceived([[maybe_unused]] const TrackNamespace& name_space) {}

        /**
         * @brief Set the subscribe status
         * @param status Status of the subscribe
         * @details Entering `kError` without a preceding `SetError()` installs an internal-error
         * fallback, then invokes `StatusChanged` even if the value did not change.
         */
        void SetStatus(const Status status) noexcept
        {
            status_ = status;
            if (status == Status::kError && !error_.has_value()) {
                error_ = Error<ErrorCode>{ ErrorCode::kInternalError, "Unknown error" };
            }
            StatusChanged(status);
        }

        /**
         * @brief Stores an error and transitions to `kError`.
         * @param error Error code and optional reason, copied into the handler.
         */
        void SetError(const Error<ErrorCode>& error)
        {
            error_ = error;
            SetStatus(Status::kError);
        }

        /**
         * @brief Handles acceptance by validating empty parameters and entering `kOk`.
         * @param params Response parameters, which must be empty.
         * @throws messages::ProtocolViolationException if unsupported parameters are present.
         */
        void RequestOkReceived(const messages::Parameters& params) override;

        /**
         * @brief Rejects updates to a namespace subscription.
         * @param params Unused update parameters.
         * @return Never returns.
         * @throws messages::ProtocolViolationException Always.
         */
        Reply<messages::Parameters, ErrorCode> RequestUpdateReceived(const messages::Parameters& params) override;

        /**
         * @brief Stores a request error and enters `kError`.
         * @param error_code Peer-provided error code.
         * @param reason Peer-provided reason, moved into the stored error.
         */
        void RequestError(ErrorCode error_code, std::string reason) override
        {
            SetError(Error<ErrorCode>{ error_code, std::move(reason) });
        }

      private:
        const Mode mode_; ///< Mode in use.

        const TrackNamespace prefix_; ///< Prefix namespace for contained handlers.

        messages::Filter filter_; ///< Filter value for namespace subscription.

        Status status_{ Status::kNotSubscribed };

        std::optional<Error<ErrorCode>> error_{};

        friend class Session;
    };
}
