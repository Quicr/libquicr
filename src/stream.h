// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/containers/stream_buffer.h"
#include "quicr/utilities/thread_safety.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace quicr {

    class SubscribeTrackHandler;

    /**
     * @brief A single stream within a connection
     *
     * @details A stream carries an ordered byte sequence for one flow of data, such as a subgroup or
     *      a request control exchange. What arrives on it, and the handler consuming that, live here
     *      rather than in a context beside it, since the stream is what every caller is handed.
     *      Transport implementations derive from this to add their own send state.
     *
     *      Streams are created by the transport and handed out as shared handles. Holding a handle
     *      keeps the stream alive for as long as it is needed, even if the transport tears the stream
     *      down concurrently, which is what makes it safe for an application thread to keep one for
     *      the lifetime of a subgroup. Transport internals that hold only a raw pointer can recover a
     *      handle via `shared_from_this()`.
     *
     *      This definition is internal to the library. Public headers only forward declare Stream, so
     *      handles are opaque to applications: they can be held and handed back to the transport, but
     *      not inspected beyond the accessors here.
     */
    class Stream : public std::enable_shared_from_this<Stream>
    {
      public:
        /// Streams are referenced by raw pointer from transport internals, so they must never be
        /// copied or relocated.
        Stream(const Stream&) = delete;
        Stream(Stream&&) = delete;
        Stream& operator=(const Stream&) = delete;
        Stream& operator=(Stream&&) = delete;

        virtual ~Stream() = default;

        /// @returns The QUIC stream ID, which every transport call and log line still needs
        std::uint64_t GetStreamId() const noexcept { return stream_id_; }

        /// @returns ID of the connection this stream belongs to
        std::uint64_t GetConnectionID() const noexcept { return conn_id_; }

        /// @returns True if the stream carries both directions, which QUIC encodes in its ID
        bool IsBidirectional() const noexcept { return (stream_id_ & 0x2) == 0; }

        /**
         * @returns True while the transport still has this stream registered
         *
         * @details A handle keeps a stream alive but says nothing about whether the transport still
         *      owns it. This is cleared once the transport commits to tearing the stream down, after
         *      which queuing data on it does nothing. Callers holding a handle across time should
         *      check this rather than assume the stream is still usable.
         */
        bool IsOpen() const noexcept { return open_.load(std::memory_order_acquire); }

        /// Called by the transport when the stream leaves its container.
        void MarkClosed() noexcept { open_.store(false, std::memory_order_release); }

        /// @returns Bytes received on the stream that have yet to be read
        std::size_t RxDataSize()
        {
            std::lock_guard _(rx_mutex);
            return rx_data.Size();
        }

        /**
         * @brief Release the buffered receive data of a stream that has been drained
         *
         * @details What the data was for is left in place, since a close notification still on its
         *      way to the delegate needs it to say which handler the stream belonged to.
         */
        void ReleaseRxData()
        {
            {
                std::lock_guard _(rx_mutex);
                rx_data.Clear();
            }

            rx_active = false;
        }

        /**
         * @name Receive state
         *
         * @details Adopted the first time data arrives, so a stream that only ever sends carries
         *      none of it, which is what `rx_active` says. `rx_data` and the notify flags are
         *      written by the network thread while the one delivering notifications reads them, so
         *      those are synchronised; the rest belongs to the delivering thread alone.
         */
        ///@{

        /// True once data has arrived on this stream, i.e. it is one the transport receives on
        bool rx_active{ false };

        /**
         * Handler consuming this stream, bound once the stream header identifies its track
         *
         * @details Weak so a handler that goes away while data is still arriving is detected rather
         *      than kept alive by the transport. Empty until the header is parsed, which is the same
         *      condition as `rx_is_new`.
         */
        std::weak_ptr<SubscribeTrackHandler> rx_handler;

        /// Indicates if new stream, on read set to false
        bool rx_is_new{ true };

        /**
         * Guards `rx_data`
         *
         * @details Unlike a queue, a stream buffer is not used one element at a time: a caller
         *      reads a run of bytes out, decides they do not form a complete message, and leaves
         *      them for the next attempt. Guarding each operation would not make that sequence
         *      safe, so this is held across everything a caller does with the buffer.
         */
        std::mutex rx_mutex;

        /**
         * Bytes received on the stream, in the order they arrived
         *
         * @details The network thread appends here and the consumer reads messages straight out,
         *      so a message split across arrivals is joined up in place. The buffer carries the
         *      partially parsed message too, letting the consumer resume where it got to.
         *
         *      A consumer takes each message it completes out of the buffer before handling it, so
         *      that `rx_mutex` is not held while application code runs and the network thread is
         *      not left waiting on it. Nothing read out may be a view of the buffer: appending to
         *      it moves the raw bytes.
         */
        StreamBuffer<std::uint8_t> rx_data QUICR_GUARDED_BY(rx_mutex);

        /**
         * Where the consumer got to in the sequence of objects arriving on this stream
         *
         * @details Each object gives only the difference from the one before, so this is what that
         *      difference is applied to.
         */
        struct RxParseState
        {
            /// A subgroup, as the pair naming it
            struct Subgroup
            {
                std::uint64_t group_id;
                std::uint64_t subgroup_id;
            };

            /// ID the next object to arrive will have, unset until the first one does
            std::optional<std::uint64_t> next_object_id;

            /**
             * Subgroup the stream carries, once the handler has been told which
             *
             * @details Also what says the handler is owed the end of that subgroup when the
             *      stream closes. A stream read as objects knows this from its first object,
             *      since the header alone may leave the subgroup ID to be taken from that
             *      object; one forwarded as bytes reads only as far as it must to know it.
             */
            std::optional<Subgroup> subgroup;
        };

        RxParseState rx_parse;

        /**
         * @name Receive notification progress
         *
         * @details Two flags rather than one so that a notification on its way always shows in at
         *      least one of them: each is set before the other is cleared. The consumer reads
         *      messages straight out of `rx_data`, so it must not be reclaimed while either is set.
         */
        ///@{

        /// True while a receive notification for this stream is waiting to be delivered
        std::atomic_bool rx_notify_pending{ false };

        /// True while a receive notification for this stream is being delivered
        std::atomic_bool rx_notify_delivering{ false };

        /// @returns True while the consumer may still be reading this stream's buffer
        bool RxNotifyInFlight() const noexcept { return rx_notify_pending.load() || rx_notify_delivering.load(); }

        ///@}

        ///@}

      protected:
        Stream(std::uint64_t stream_id, std::uint64_t conn_id)
          : stream_id_(stream_id)
          , conn_id_(conn_id)
        {
        }

      private:
        std::uint64_t stream_id_{ 0 };
        std::uint64_t conn_id_{ 0 };

        /// Read from application threads while the transport thread clears it, so it must be atomic.
        std::atomic_bool open_{ true };
    };
}
