// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstdint>
#include <memory>

namespace quicr {

    /**
     * @brief A flow of data within a connection
     *
     * @details A data context is a flow of data (track, namespace), similar to a pipe of data to be
     *      transmitted. It may be carried over datagrams or over one or more streams. Shaping and
     *      metrics are maintained at the data context level. Transport implementations derive from
     *      this to add their own state.
     *
     *      Contexts are created by the transport and handed out as shared handles. Holding a handle
     *      keeps the context alive for as long as it is needed, even if the transport removes the
     *      context concurrently. Transport internals that hold only a raw pointer can recover a
     *      handle via `shared_from_this()`.
     *
     *      This definition is internal to the library. Public headers only forward declare
     *      DataContext, so handles are opaque to applications: they can be held and handed back to
     *      the transport, but not inspected.
     */
    class DataContext : public std::enable_shared_from_this<DataContext>
    {
      public:
        /// Contexts are referenced by raw pointer from transport internals, so they must never be
        /// copied or relocated.
        DataContext(const DataContext&) = delete;
        DataContext(DataContext&&) = delete;
        DataContext& operator=(const DataContext&) = delete;
        DataContext& operator=(DataContext&&) = delete;

        virtual ~DataContext() = default;

        /// @returns ID of this context, unique within its connection
        std::uint64_t GetID() const noexcept { return id_; }

        /// @returns ID of the connection this context belongs to
        std::uint64_t GetConnectionID() const noexcept { return conn_id_; }

        /// @returns True if the context uses bidirectional streams, false if unidirectional
        bool IsBidir() const noexcept { return is_bidir_; }

      protected:
        DataContext(std::uint64_t id, std::uint64_t conn_id, bool bidir)
          : id_(id)
          , conn_id_(conn_id)
          , is_bidir_(bidir)
        {
        }

      private:
        std::uint64_t id_{ 0 };
        std::uint64_t conn_id_{ 0 };
        bool is_bidir_{ false };
    };
}
