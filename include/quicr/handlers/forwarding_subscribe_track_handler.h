// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/handlers/subscribe_track_handler.h"
#include "quicr/messages/messages.h"

namespace quicr {
    /**
     * @brief MOQ track handler that takes a subscribed track's streams as bytes
     *
     * @details A relay passing a subgroup on writes it to another byte stream, so what it needs is
     *      the bytes rather than the objects in them: where one object ends and the next begins
     *      does not matter to it, and working that out is the cost it is trying to avoid. A handler
     *      of this kind is given each subgroup's header once, so that it can write an equivalent
     *      one of its own, and then the bytes that follow as they arrive.
     *
     *      Only the track alias stands in the way of passing a subgroup on untouched, since it is
     *      agreed per session and so has to be rewritten. Everything after the header can go as it
     *      came: an object says only how far it is from the one before it, which holds for as long
     *      as the whole subgroup is passed on in order.
     *
     *      What is given up is what needs an object to have been read. Nothing arriving on a
     *      stream is reported to `ObjectReceived`, and nothing counts the objects in it. A track
     *      wanting either of those wants a plain `SubscribeTrackHandler`.
     *
     *      Only streams are passed on as bytes, a datagram being a whole object with no stream to
     *      pass on. Datagrams are still reported to `ObjectReceived` and still counted, and which
     *      of the two an object arrives as is the publisher's to choose per object, so a track can
     *      use both at once. Overriding `ObjectReceived` on one of these therefore reports the
     *      track's datagrams and only its datagrams, and leaving it alone drops them; a relay
     *      whose publisher may send any wants it implemented.
     */
    class ForwardingSubscribeTrackHandler : public SubscribeTrackHandler
    {
      protected:
        using SubscribeTrackHandler::SubscribeTrackHandler;

      public:
        /**
         * @brief Notification that a subgroup has started arriving on a stream of its own
         *
         * @details Called once per stream, before any of its bytes are, with what the subgroup
         *      header said. Enough to write an equivalent header on whatever this is passed on to.
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
         *      subgroup header. A run does not fall on any particular boundary, so it may hold
         *      several objects, part of one, or the end of one and the start of the next.
         *
         *      A track's subgroups arrive on streams of their own and so interleave here, which
         *      is what names the subgroup on every call rather than only when it starts.
         *
         *      The bytes are the handler's to take, nothing here wanting them afterwards, so
         *      handing them to something that holds them, as publishing them on does, copies none
         *      of them. A handler that only reads them can leave them alone and let them go.
         *
         * @param group_id      Group the subgroup belongs to
         * @param subgroup_id   Subgroup the bytes belong to
         * @param data          Bytes to pass on, to take if it wants them
         */
        virtual void StreamBytesForwarded(std::uint64_t group_id, std::uint64_t subgroup_id, Bytes&& data) = 0;
    };

} // namespace quicr
