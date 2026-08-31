#include "quicr/connection.h"

#include "quicr/session.h"

namespace quicr {

    Connection::Connection(std::uint64_t id, API api)
      : id{ id }
      , api_{ api }
      , status_{ Status::kConnecting }
    {
    }

    std::uint64_t Connection::GetID() const noexcept
    {
        return id;
    }

    Connection::API Connection::GetAPI() const noexcept
    {
        return api_;
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

    void Connection::OnRecvDgram()
    {
        if (auto delegate = delegate_.lock()) {
            delegate->OnRecvDgram();
        }
    }

    void Connection::OnRecvStream(std::uint64_t stream_id,
                                  const std::shared_ptr<StreamRxContext>& rx_ctx,
                                  const std::shared_ptr<Stream>& stream,
                                  bool is_bidir)
    {
        if (auto delegate = delegate_.lock()) {
            delegate->OnRecvStream(stream_id, rx_ctx, stream, is_bidir);
        }
    }

    void Connection::OnStreamClosed(std::uint64_t stream_id,
                                    std::shared_ptr<StreamRxContext> rx_ctx,
                                    StreamClosedFlag flag)
    {
        if (auto delegate = delegate_.lock()) {
            delegate->OnStreamClosed(stream_id, std::move(rx_ctx), flag);
        }
    }
}
