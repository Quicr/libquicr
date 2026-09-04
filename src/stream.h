// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

namespace quicr {

    /**
     * @brief A single stream within a connection
     *
     * @details A stream carries an ordered byte sequence for one flow of data, such as a subgroup or
     *      a request control exchange. Transport implementations derive from this to add their own
     *      send and receive state.
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
