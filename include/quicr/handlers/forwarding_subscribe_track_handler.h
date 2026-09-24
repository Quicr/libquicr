// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/handlers/subscribe_track_handler.h"
#include "quicr/messages/messages.h"

namespace quicr {
    /**
     * @brief MOQ track handler that takes a subscribed track's streams as bytes
     *
     * @details A relay writes a subgroup back out to another stream, so it wants the bytes rather
     *      than the objects in them, working out where each one ends being the cost it is avoiding.
     *      Each subgroup's header arrives once, to write an equivalent of, then its bytes as they
     *      come. Only the track alias has to be rewritten, being agreed per session; the rest holds
     *      as it came for as long as the subgroup is passed on in order.
     *
     *      Nothing on a stream reaches `ObjectReceived` or is counted, so a track wanting objects
     *      wants a plain `SubscribeTrackHandler`. Datagrams still do both, being whole objects with
     *      no stream to pass on, and a publisher chooses between the two per object, so a relay
     *      that might be sent datagrams still wants `ObjectReceived` implemented.
     */
    class ForwardingSubscribeTrackHandler : public SubscribeTrackHandler
    {
      protected:
        using SubscribeTrackHandler::SubscribeTrackHandler;

      public:
        /**
         * @brief Notification that a subgroup has started arriving on a stream of its own
         *
         * @details Called once per stream, before any of its bytes, with enough of the subgroup
         *      header to write an equivalent one.
         *
         * @param group_id      Group the subgroup belongs to
         * @param subgroup_id   Subgroup that has started
         * @param priority      Priority the publisher gave the subgroup, unset if it left it to
         *                      the framing's default
         * @param properties    How the subgroup is framed, to frame the one passed on the same way
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
         *      The bytes are the handler's to take, nothing here wanting them afterwards.
         *
         * @param group_id      Group the subgroup belongs to
         * @param subgroup_id   Subgroup the bytes belong to
         * @param data          Bytes to pass on, to take if it wants them
         */
        virtual void StreamBytesForwarded(std::uint64_t group_id, std::uint64_t subgroup_id, Bytes&& data) = 0;
    };

} // namespace quicr
