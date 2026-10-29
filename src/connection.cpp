// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/connection.h"

#include "quicr/session.h"

namespace quicr {

    Connection::Connection(std::uint64_t id, Protocol api)
      : id{ id }
      , protocol_{ api }
      , status_{ Status::kConnecting }
    {
    }

    std::uint64_t Connection::GetID() const noexcept
    {
        return id;
    }

    Connection::Protocol Connection::GetProtocol() const noexcept
    {
        return protocol_;
    }

    void Connection::SetStatus(Status new_status)
    {
        std::shared_ptr<Delegate> delegate;
        {

            std::lock_guard lock(mutex_);
            if (new_status == status_.exchange(new_status)) {
                return;
            }

            delegate = delegate_.lock();
        }

        if (delegate) {
            delegate->OnConnectionStatus(shared_from_this(), new_status);
        }
    }

    void Connection::SetDelegate(const std::shared_ptr<Delegate>& delegate)
    {
        {
            std::lock_guard lock(mutex_);
            delegate_ = delegate;
        }

        const auto status = GetStatus();
        if (delegate && status != Status::kConnecting) {
            // Replay the current status in case transport notifications were delivered
            // before the application delegate was attached.
            OnStatusChanged(status);
        }
    }

    std::shared_ptr<Connection::Delegate> Connection::GetDelegate() const
    {
        std::lock_guard _(mutex_);
        return delegate_.lock();
    }

    void Connection::OnStatusChanged(Status status)
    {
        const auto self = weak_from_this().lock();
        if (auto delegate = GetDelegate(); delegate && self) {
            delegate->OnConnectionStatus(self, status);
        }
    }

    void Connection::OnRecvDgram()
    {
        const auto self = weak_from_this().lock();
        if (auto delegate = GetDelegate(); delegate && self) {
            delegate->OnRecvDgram(self);
        }
    }

    bool Connection::OnRecvStream(const std::shared_ptr<Stream>& stream)
    {
        if (auto delegate = GetDelegate()) {
            return delegate->OnRecvStream(stream);
        }

        return false;
    }

    void Connection::OnStreamClosed(const std::shared_ptr<Stream>& stream, StreamClosedFlag flag)
    {
        if (auto delegate = GetDelegate()) {
            delegate->OnStreamClosed(stream, flag);
        }
    }

    bool Connection::RecordSetupSent()
    {
        std::lock_guard _(mutex_);
        return std::exchange(setup_sent_, true) == false;
    }

    bool Connection::RecordSetupReceived()
    {
        std::lock_guard _(mutex_);
        return std::exchange(setup_received_, true) == false;
    }

    bool Connection::IsSetupComplete() const
    {
        std::lock_guard _(mutex_);
        return setup_sent_ && setup_received_;
    }

    void Connection::SetRxControlStreamId(std::uint64_t stream_id)
    {
        std::lock_guard _(mutex_);
        rx_ctrl_stream_id_ = stream_id;
    }

    std::optional<std::uint64_t> Connection::GetRxControlStreamId() const
    {
        std::lock_guard _(mutex_);
        return rx_ctrl_stream_id_;
    }

    void Connection::SetTxControlStream(std::shared_ptr<Stream> stream)
    {
        std::lock_guard _(mutex_);
        tx_ctrl_stream_ = std::move(stream);
    }

    std::shared_ptr<Stream> Connection::GetTxControlStream() const
    {
        std::lock_guard _(mutex_);
        return tx_ctrl_stream_;
    }
}
