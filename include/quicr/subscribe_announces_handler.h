// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <quicr/detail/base_track_handler.h>
#include <quicr/metrics.h>
#include <quicr/publish_track_handler.h>

namespace quicr {

    class SubscribeAnnouncesHandler
    {
      public:
        enum class Error : uint8_t
        {
            kOk = 0,
            kNotAuthorized,
            kNotSubscribed,
            kNoData
        };

        enum class Status : uint8_t
        {
            kOk = 0,
            kNotConnected,
            kSubscribeError,
            kNotAuthorized,
            kNotSubscribed,
            kPendingSubscribeResponse,
            kSendingUnsubscribe ///< In this state, callbacks will not be called
        };

      protected:
        SubscribeAnnouncesHandler(const TrackNamespace& namespace_prefix)
          : track_namespace_prefix_(namespace_prefix)
        {
        }

      public:
        static std::shared_ptr<SubscribeAnnouncesHandler> Create(const TrackNamespace& namespace_prefix)
        {
            return std::shared_ptr<SubscribeAnnouncesHandler>(new SubscribeAnnouncesHandler(namespace_prefix));
        }

        constexpr Status GetStatus() const noexcept { return status_; }

        TrackNamespace GetTrackNamespacePrefix() const noexcept { return track_namespace_prefix_; }

        // --------------------------------------------------------------------------
        // Public Virtual API callback event methods
        // --------------------------------------------------------------------------
        /** @name Callbacks
         */
        ///@{

        virtual void MatchingTrackNamespaceReceived([[maybe_unused]] const quicr::TrackNamespace& track_namespace) {}

        virtual void StatusChanged([[maybe_unused]] Status status) {}

        ///@}

        // --------------------------------------------------------------------------
        // Metrics
        // --------------------------------------------------------------------------

        // --------------------------------------------------------------------------
        // Internal
        // --------------------------------------------------------------------------
      private:
        /**
         * @brief Set the subscribe status
         * @param status                Status of the subscribe
         */
        void SetStatus(Status status) noexcept
        {
            status_ = status;
            StatusChanged(status);
        }

        // --------------------------------------------------------------------------
        // Member variables
        // --------------------------------------------------------------------------
        Status status_{ Status::kNotSubscribed };

        TrackNamespace track_namespace_prefix_;

        friend class Transport;
        friend class Client;
        friend class Server;
    };

} // namespace moq
