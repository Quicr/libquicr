// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/attributes.h"
#include "quicr/detail/ctrl_message_types.h"
#include "quicr/detail/receive_track_handler.h"
#include "quicr/metrics.h"
#include "quicr/object.h"
#include "quicr/track_name.h"

#include <cstdint>
#include <map>
#include <memory>
#include <utility>

namespace quicr {

    /**
     * @brief MOQ track handler for subscribe namespace and associated tracks.
     *
     * @details MOQ subscribe namespace handler defines all track related callbacks and
     *  functions for subscribe namespace and accepted tracks.
     *  This Track handler notifies of available tracks, and handles object delivery of accepted
     *  ones.
     */
    class SubscribeNamespaceHandler : public ReceiveTrackHandler
    {
      public:
        using Error = std::pair<messages::SubscribeNamespaceErrorCode, messages::ReasonPhrase>;
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
        /**
         * @brief Subscribe track handler constructor
         *
         * @param namespace_prefix Namespace prefix to receive notifications for.
         */
        explicit SubscribeNamespaceHandler(const TrackNamespace& namespace_prefix)
          : ReceiveTrackHandler(FullTrackName{ .name_space = namespace_prefix, .name = {} })
          , namespace_prefix_(namespace_prefix)
          , request_id_{ 0 }
        {
        }

      public:
        /**
         * @brief Create shared Subscribe Namespace handler.
         *
         * @param namespace_prefix Namespace prefix to receive notifications for.
         */
        static std::shared_ptr<SubscribeNamespaceHandler> Create(const TrackNamespace& namespace_prefix)
        {
            return std::shared_ptr<SubscribeNamespaceHandler>(new SubscribeNamespaceHandler(namespace_prefix));
        }

        virtual ~SubscribeNamespaceHandler() = default;

        /**
         * @brief Get the namespace prefix this handler is interested in.
         * @return The namespace prefix.
         */
        TrackNamespace GetNamespacePrefix() const noexcept { return namespace_prefix_; }

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

        // --------------------------------------------------------------------------
        // Public Virtual API callback event methods
        // --------------------------------------------------------------------------

        /** @name Callbacks
         */
        ///@{

        /**
         * @brief Notification of subscribe status
         *
         * @details Notification of the subscribe status
         *
         * @param status        Indicates status of the subscribe
         */
        virtual void StatusChanged(Status status);

        /**
         * @brief
         * @param attrs
         */
        virtual bool TrackAvailable(const FullTrackName& track_name);

        /**
         * @brief Notification of received [full] data object
         *
         * @details Event notification to provide the caller the received full data object
         *
         * @warning This data will be invalided after return of this method
         *
         * @param object_headers    Object headers, must include group and object Ids
         * @param data              Object payload data received, **MUST** match ObjectHeaders::payload_length.
         */
        virtual void ObjectReceived(const messages::TrackAlias& track_alias,
                                    const ObjectHeaders& object_headers,
                                    BytesSpan data) override;

      protected:
        /**
         * @brief Set the subscribe status
         * @param status                Status of the subscribe
         */
        void SetStatus(const Status status) noexcept
        {
            status_ = status;
            StatusChanged(status);
        }

        void SetError(const Error& error)
        {
            error_ = error;
            SetStatus(Status::kError);
        }

      private:
        Status status_{ Status::kNotSubscribed };
        const TrackNamespace namespace_prefix_;
        messages::RequestID request_id_;
        std::optional<Error> error_{};

        friend class Transport;
        friend class Client;
        friend class Server;
    };

} // namespace moq
