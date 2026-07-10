#include "quicr/connection.h"

#include "quicr/session.h"

namespace quicr {

    Connection::Connection(std::uint64_t id, API api)
      : id{ id }
      , api{ api }
      , status_{ Status::kConnecting }
    {
    }

    Connection::Connection(const Connection& other)
      : next_request_id(other.next_request_id.load(std::memory_order_seq_cst))
    {
    }

    std::uint64_t Connection::GetNextRequestID()
    {
        std::uint64_t rid = next_request_id;
        next_request_id += 2;

        return rid;
    }

    void Connection::SetStartingRequestID(std::uint64_t starting_id)
    {
        next_request_id = starting_id;
    }

    std::uint64_t Connection::GetID() const noexcept
    {
        return id;
    }

    Connection::API Connection::GetAPI() const noexcept
    {
        return api;
    }

    void Connection::SetStatus(Status new_status)
    {
        if (new_status == status_) {
            return;
        }

        status_ = new_status;

        // TODO: Do something here
        switch (status_) {
            case Status::kReady:
                break;
            case Status::kConnecting:
                break;
            case Status::kDisconnected:
                break;
            case Status::kIdleTimeout:
                break;
            case Status::kRemoteRequestClose:
                break;
            case Status::kShuttingDown:
                break;
            case Status::kShutdown:
                break;
        }

        OnStatusChanged(status_);
    }

    void Connection::SetDelegate(const std::shared_ptr<Delegate>& delegate)
    {
        delegate_ = delegate;

        if (delegate && status_ != Status::kConnecting) {
            // Replay the current status in case transport notifications were delivered
            // before the application delegate was attached.
            OnStatusChanged(status_);
        }
    }

    void Connection::OnStatusChanged(Status status)
    {
        if (auto delegate = delegate_.lock()) {
            delegate->OnConnectionStatus(status);
        }
    }

    void Connection::OnNewDataContext(const std::uint64_t& data_ctx_id)
    {
        if (auto delegate = delegate_.lock()) {
            delegate->OnNewDataContext(data_ctx_id);
        }
    }

    void Connection::OnRecvDgram(std::optional<std::uint64_t> data_ctx_id)
    {
        if (auto delegate = delegate_.lock()) {
            delegate->OnRecvDgram(data_ctx_id);
        }
    }

    void Connection::OnRecvStream(std::uint64_t stream_id, std::optional<std::uint64_t> data_ctx_id, bool is_bidir)
    {
        if (auto delegate = delegate_.lock()) {
            delegate->OnRecvStream(stream_id, data_ctx_id, is_bidir);
        }
    }

    void Connection::OnStreamClosed(std::uint64_t stream_id,
                                    std::shared_ptr<StreamRxContext> rx_ctx,
                                    std::optional<uint64_t> data_ctx_id,
                                    StreamClosedFlag flag)
    {

        if (auto delegate = delegate_.lock()) {
            delegate->OnStreamClosed(stream_id, rx_ctx, data_ctx_id, flag);
        }
    }
}
