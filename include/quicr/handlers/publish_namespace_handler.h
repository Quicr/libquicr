// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/errors.h"
#include "quicr/handlers/publish_track_handler.h"
#include "quicr/track_name.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>

namespace quicr {

    class Session;

    /**
     * @brief Publishes and manages tracks below one namespace prefix.
     *
     * @details The handler owns shared references to registered track publishers. Calls on the same
     * handler should be serialized with session/application callbacks.
     */
    class PublishNamespaceHandler : public TrackHandler
    {
      public:
        /**
         * @brief  Status codes for the Publish track
         */
        enum class Status : uint8_t
        {
            kOk = 0,               ///< Namespace is accepted and active.
            kNotConnected,         ///< Session transport is disconnected.
            kNotPublished,         ///< Initial/inactive state.
            kPendingResponse,      ///< Waiting for the peer's response.
            kPublishNotAuthorized, ///< Peer denied publication.
            kSendingDone,          ///< In this state, callbacks will not be called
            kError,                ///< Request failed.
        };

      protected:
        /**
         * @brief Constructs an inactive namespace publisher.
         * @param prefix Prefix that every contained track must share.
         */
        PublishNamespaceHandler(const TrackNamespace& prefix);

        /**
         * @brief Notification of Publish status
         *
         * @details Notification of the Publish status
         *
         * @param status Indicates status of the Publish
         * @note Default implementation does nothing. Called synchronously on a session path.
         */
        virtual void StatusChanged(Status status);

      public:
        /**
         * @brief Creates a namespace publisher with shared ownership.
         * @param prefix Prefix that every contained track must share.
         * @return Shared ownership of the new handler.
         */
        static auto Create(const TrackNamespace& prefix)
        {
            return std::shared_ptr<PublishNamespaceHandler>(new PublishNamespaceHandler(prefix));
        }

        /**
         * @brief Unpublishes all retained tracks when an attached session still exists.
         * @note Destruction may call into the session; retained track handlers are released afterward.
         */
        virtual ~PublishNamespaceHandler();

        /**
         * @brief Publish a new track
         *
         * @details Creates a new publish track handler that will be used to send data for a track.
         *      Selection and filters may be applied here.
         *
         * @param handler Publish Track Handler for the publish
         * @throws std::invalid_argument if the handler's namespace does not share this prefix.
         * @throws std::runtime_error if this namespace handler has no live session.
         * @details Retains shared ownership under the track-name hash and asks the session to
         * publish it. `handler` must not be null.
         */
        virtual void PublishTrack(std::shared_ptr<PublishTrackHandler> handler);

        /**
         * @brief Removes and unpublishes a track.
         * @param handler Track publisher to remove; must not be null.
         * @throws std::runtime_error if this namespace handler has no live session.
         */
        virtual void UnPublishTrack(std::shared_ptr<PublishTrackHandler> handler);

        /**
         * @brief Passthrough PublishObject to Publish Track matching full name hash
         *
         * @details Passthrough to send to the publish handler. Selection and filters maybe applied.
         *
         * @param track_alias Track alias
         * @param object_headers Object headers, must include group and object Ids
         * @param data Full complete payload data for the object
         * @param stream_mode MOQT stream mode, stream mode
         *
         * @returns PublishObjectStatus from publish handler
         * @details The alias is looked up in `handlers_`. No handler yields `kInternalError`.
         * The payload view is borrowed only for this call; the selected handler copies it before
         * asynchronous transmission.
         * @throws TransportException Propagated if the selected publisher cannot enqueue data.
         */
        virtual PublishTrackHandler::PublishObjectStatus PublishObject(
          uint64_t track_alias,
          const ObjectHeaders& object_headers,
          BytesSpan data,
          std::optional<messages::StreamHeaderProperties> stream_mode = std::nullopt);

        /**
         * @brief Passthrough to Forward received object data to each publish handler
         *
         * @details Passthrough to send to the publish handlers. Selection and filters can be applied.
         *
         * @note This method must be overridden to be useful. The default always returns
         * `kInternalError` and does not inspect its arguments or forward data.
         *
         * @param track_alias Track alias
         * @param is_new_stream Indicates if this data starts a new stream
         * @param group_id Group ID for stream
         * @param subgroup_id Subgroup ID for stream
         * @param data MoQ data to send
         *
         * @returns PublishObjectStatus from publish handler
         */
        virtual PublishTrackHandler::PublishObjectStatus ForwardPublishedData(
          [[maybe_unused]] uint64_t track_alias,
          [[maybe_unused]] bool is_new_stream,
          [[maybe_unused]] uint64_t group_id,
          [[maybe_unused]] uint64_t subgroup_id,
          [[maybe_unused]] std::shared_ptr<const std::vector<uint8_t>> data)
        {
            return PublishTrackHandler::PublishObjectStatus::kInternalError;
        }

        /// @return Borrowed immutable prefix, valid for this handler's lifetime.
        const TrackNamespace& GetPrefix() const noexcept { return prefix_; }

        /**
         * @brief Get the status of the Publish
         *
         * @return Status of the Publish
         */
        Status GetStatus() const noexcept { return status_.load(std::memory_order_relaxed); }

      protected:
        /**
         * @brief Set the Publish status
         * @param status Status of the Publish
         * @details Invokes `StatusChanged` even if the value did not change.
         */
        void SetStatus(const Status status) noexcept
        {
            status_.store(status, std::memory_order_relaxed);
            StatusChanged(status);
        }

        /**
         * @brief Handles peer acceptance by entering `kOk`.
         * @param params Response parameters, which are ignored without validation.
         */
        void RequestOkReceived(const messages::Parameters& params) override;

        /**
         * @brief Rejects updates to a namespace publication.
         * @param params Unused update parameters.
         * @return Never returns.
         * @throws messages::ProtocolViolationException Always.
         */
        Reply<messages::Parameters, ErrorCode> RequestUpdateReceived(const messages::Parameters& params) override;

        /**
         * @brief Enters `kError` after a rejected request.
         */
        void RequestError(ErrorCode, std::string) override { SetStatus(Status::kError); }

        /**
         * @brief Track publishers keyed by the full-track-name hash.
         *
         * @details Entries retain shared ownership until explicitly unpublished or this namespace
         * handler is destroyed. Derived classes may inspect or customize routing through this map.
         */
        std::map<std::uint64_t, std::shared_ptr<PublishTrackHandler>> handlers_;

      private:
        void UnPublishAllTracks() noexcept;

        std::mutex handlers_mutex_;

        const TrackNamespace prefix_; ///< Prefix namespace for contained handlers.

        std::atomic<Status> status_{ Status::kNotPublished };

        friend class Session;
    };
}
