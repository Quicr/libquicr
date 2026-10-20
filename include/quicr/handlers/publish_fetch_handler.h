// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/handlers/publish_track_handler.h"
#include "quicr/messages/message_serialisation.h"

namespace quicr {
    /**
     * @brief Publishes the ordered object stream that answers a FETCH.
     *
     * @details Unlike ordinary track publication, all objects are encoded onto one reliable FETCH
     * stream using delta state and the requested group order. The session owns the active handler
     * jointly and closes it through `EndFetch()` when it is unbound.
     */
    class PublishFetchHandler : public PublishTrackHandler
    {
      protected:
        /**
         * @brief Constructs a server-side FETCH publisher.
         * @param full_track_name Track being fetched.
         * @param priority Publisher and transport scheduling priority.
         * @param subscribe_id Request identifier written in the FETCH header.
         * @param group_order Serialization order promised for returned objects.
         * @param ttl Default object queue lifetime used when an object has no TTL.
         */
        PublishFetchHandler(const FullTrackName& full_track_name,
                            uint8_t priority,
                            uint64_t subscribe_id,
                            messages::GroupOrder group_order,
                            uint32_t ttl)
          : PublishTrackHandler(full_track_name, TrackMode::kStream, priority, priority, ttl)
          , group_order_(group_order)
          , serialization_state_(group_order)
        {
            SetRequestId(subscribe_id);
        }

      public:
        /**
         * @brief Creates a FETCH publisher with shared ownership.
         * @param full_track_name Track being fetched.
         * @param priority Publisher and transport scheduling priority.
         * @param subscribe_id Request identifier written in the FETCH header.
         * @param group_order Serialization order promised for returned objects.
         * @param ttl Default object queue lifetime used when an object has no TTL.
         * @return Shared ownership of the new handler.
         */
        static std::shared_ptr<PublishFetchHandler> Create(const FullTrackName& full_track_name,
                                                           uint8_t priority,
                                                           uint64_t subscribe_id,
                                                           messages::GroupOrder group_order,
                                                           uint32_t ttl)
        {
            return std::shared_ptr<PublishFetchHandler>(
              new PublishFetchHandler(full_track_name, priority, subscribe_id, group_order, ttl));
        }

        /**
         * @brief Queues one object on the FETCH response stream.
         * @param object_headers Object identity, status, extensions, and optional TTL.
         * @param data Borrowed payload bytes; the method copies them before returning.
         * @param stream_mode Ignored for FETCH framing.
         * @return `kOk` when queued, `kInternalError` without an attached session, or
         * `kNoSubscribers` without a request ID.
         * @throws TransportException if stream creation or enqueueing fails.
         *
         * @details The first call creates the stream and emits its FETCH header. Serialization
         * state advances only after the object is successfully queued. Calls must be externally
         * serialized and objects must follow `GetGroupOrder()`.
         */
        PublishObjectStatus PublishObject(
          const ObjectHeaders& object_headers,
          BytesSpan data,
          std::optional<messages::StreamHeaderProperties> stream_mode = std::nullopt) override;

        /// @return The object order encoded on this FETCH stream.
        constexpr messages::GroupOrder GetGroupOrder() const noexcept { return group_order_; }

      private:
        /**
         * @brief Finish the fetch: let whatever is queued drain, then close the stream.
         *
         * @details The empty object carries no payload; it exists to mark the stream close-on-empty
         *      once the transport reaches it, so objects already queued still go out. Does nothing if
         *      the fetch never sent an object, since there is no stream to close.
         */
        void EndFetch();

        messages::GroupOrder group_order_;
        messages::FetchObjectSerializationState serialization_state_;
        bool sent_first_header_{ false };
        std::shared_ptr<Stream> stream_; ///< Stream for the fetch, created on sent_first_header

        friend class Session;
    };

} // namespace moq
