// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/handlers/subscribe_track_handler.h"
#include "quicr/messages/message_serialisation.h"

namespace quicr {
    /**
     * @brief MOQ track handler that takes a subscribed track's streams as bytes
     *
     * @details Delivers each subgroup header followed by its raw stream bytes, allowing a relay to
     *      forward the subgroup without parsing individual objects. The track alias must be
     *      rewritten because it is scoped to the session.
     *
     *      Stream objects are not delivered to `ObjectReceived` or counted individually. Datagrams
     *      are still delivered to `ObjectReceived`, which a relay should override if it expects them.
     *      Callbacks run synchronously on the session receive path and may interleave across subgroup
     *      streams, but calls themselves are not concurrent from one session's receive processing.
     */
    class ForwardingSubscribeTrackHandler : public SubscribeTrackHandler
    {
      protected:
        /**
         * @brief Inherits the protected subscription constructors.
         *
         * @details Derived forwarding handlers supply the same subscription attributes and obey
         * the same session-ownership and state rules as `SubscribeTrackHandler`.
         * @param full_track_name Track identity copied into the handler.
         * @param priority Subscription scheduling priority.
         * @param group_order Requested group order, or no value for the publisher default.
         * @param filter Subscription selection filter.
         * @param joining_fetch Optional joining-fetch attributes.
         * @param publisher_initiated Whether the publisher initiated the subscription.
         */
        using SubscribeTrackHandler::SubscribeTrackHandler;

      public:
        /**
         * @brief Notification that a subgroup has started arriving on a stream of its own
         *
         * @details Called once per stream, before any of its bytes, with enough of the subgroup
         *      header to write an equivalent one.
         *
         * @param group_id Group the subgroup belongs to
         * @param subgroup_id Subgroup the bytes belong to
         * @param priority Priority the publisher gave the subgroup, unset if it left it to
         *                      the framing's default
         * @param properties How the subgroup is framed
         *
         * @note Called synchronously before the first `StreamBytesForwarded` call for this stream.
         */
        virtual void SubgroupStarted(std::uint64_t group_id,
                                     std::uint64_t subgroup_id,
                                     std::optional<std::uint8_t> priority,
                                     messages::StreamHeaderProperties properties) = 0;

        /**
         * @brief Notification of bytes arriving on a subgroup's stream
         *
         * @details Everything that has arrived since the last call, in order, starting after the
         *      subgroup header. A run falls on no particular boundary, so it may hold several
         *      objects, part of one, or the end of one and the start of the next. A track's
         *      subgroups arrive on streams of their own and so interleave here, which is why every
         *      call names one.
         *
         * @param group_id Group the subgroup belongs to
         * @param subgroup_id Subgroup the bytes belong to
         * @param data Owning byte buffer transferred to the callback; it may be moved
         *                      elsewhere and retained after return
         *
         * @note Called synchronously on the session receive path. The buffer contains only bytes
         * received since the preceding call for this subgroup.
         */
        virtual void StreamBytesForwarded(std::uint64_t group_id, std::uint64_t subgroup_id, Bytes&& data) = 0;
    };

} // namespace quicr
