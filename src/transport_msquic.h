// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "msquic_connection.h"
#include "quicr/containers/safe_queue.h"
#include "quicr/transport.h"

#include <msquic.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace quicr {

    class MsQuicTransport : public Transport
    {
      public:
        MsQuicTransport(const TransportRemote& remote,
                        const TransportConfig& config,
                        bool server_mode,
                        std::shared_ptr<timeq::tick_service> tick_service,
                        std::shared_ptr<spdlog::logger> logger);
        ~MsQuicTransport() override;

        TransportStatus Status() const override { return status_.load(); }
        std::shared_ptr<Connection> Start() override;
        void Shutdown() override;

        std::shared_ptr<DataContext> CreateDataContext(const std::shared_ptr<Connection>& connection,
                                                       bool use_reliable_transport,
                                                       uint8_t priority,
                                                       bool bidir) override;
        void DeleteDataContext(const std::shared_ptr<Connection>& connection,
                               const std::shared_ptr<DataContext>& data_context,
                               bool delete_on_empty = false) override;
        TransportError Enqueue(const std::shared_ptr<Connection>& connection,
                               const std::shared_ptr<DataContext>& data_context,
                               std::uint64_t stream_id,
                               std::shared_ptr<const std::vector<std::uint8_t>> bytes,
                               uint8_t priority,
                               uint32_t ttl_ms,
                               uint32_t delay_ms,
                               EnqueueFlags flags) override;
        std::shared_ptr<const std::vector<std::uint8_t>> Dequeue(
          const std::shared_ptr<Connection>& connection,
          const std::shared_ptr<DataContext>& data_context) override;
        std::shared_ptr<StreamRxContext> GetStreamRxContext(const std::shared_ptr<Connection>& connection,
                                                            std::uint64_t stream_id) override;
        std::uint64_t CreateStream(const std::shared_ptr<Connection>& connection,
                                   const std::shared_ptr<DataContext>& data_context,
                                   uint8_t priority) override;
        void CloseStream(const std::shared_ptr<Connection>& connection,
                         const std::shared_ptr<DataContext>& data_context,
                         std::uint64_t stream_id,
                         bool use_reset) override;
        void Close(const std::shared_ptr<Connection>& connection,
                   AppReasonForClose app_reason = AppReasonForClose::kRemoteRequestClose) override;
        bool GetPeerAddrInfo(const std::shared_ptr<Connection>& connection, sockaddr_storage* address) override;
        int CloseWebTransportSession(const std::shared_ptr<Connection>&, uint32_t, const char*) override { return -1; }
        int DrainWebTransportSession(const std::shared_ptr<Connection>&) override { return -1; }

      private:
        struct StreamSendContext;
        struct DatagramSendContext;

        static QUIC_STATUS QUIC_API ListenerCallback(HQUIC listener, void* context, QUIC_LISTENER_EVENT* event);
        static QUIC_STATUS QUIC_API ConnectionCallback(HQUIC connection, void* context, QUIC_CONNECTION_EVENT* event);
        static QUIC_STATUS QUIC_API StreamCallback(HQUIC stream, void* context, QUIC_STREAM_EVENT* event);

        void OpenConfiguration();
        std::shared_ptr<Connection> StartClient();
        void StartServer();
        QUIC_STATUS OnListenerEvent(QUIC_LISTENER_EVENT& event);
        QUIC_STATUS OnConnectionEvent(const std::shared_ptr<MsQuicConnection>& connection,
                                      QUIC_CONNECTION_EVENT& event);
        QUIC_STATUS OnStreamEvent(const std::shared_ptr<MsQuicStream>& stream, QUIC_STREAM_EVENT& event);
        void OnStreamReceive(const std::shared_ptr<MsQuicStream>& stream, const QUIC_STREAM_EVENT& event);
        void NotifyStreamClosed(const std::shared_ptr<MsQuicStream>& stream, StreamClosedFlag flag);
        void MaybeDeleteDataContext(const std::shared_ptr<MsQuicConnection>& connection,
                                    const std::shared_ptr<MsQuicDataContext>& data_context);
        void RemoveConnection(std::uint64_t connection_id);
        void QueueCallback(std::function<void()> callback);
        void CallbackLoop();
        void MaintenanceLoop();
        void FlushCallbacks();
        static QUIC_ADDR MakeAddress(const TransportRemote& remote);
        static std::uint64_t AppErrorCode(AppReasonForClose reason);

        const TransportRemote remote_;
        const TransportConfig config_;
        const bool server_mode_;
        std::shared_ptr<timeq::tick_service> tick_service_;
        std::shared_ptr<spdlog::logger> logger_;
        std::atomic<TransportStatus> status_{ TransportStatus::kConnecting };
        std::atomic<bool> stop_{ false };
        const QUIC_API_TABLE* api_{ nullptr };
        HQUIC registration_{ nullptr };
        HQUIC configuration_{ nullptr };
        HQUIC listener_{ nullptr };
        QUIC_BUFFER alpn_{};
        std::map<std::uint64_t, std::shared_ptr<MsQuicConnection>> connections_;
        mutable std::mutex connections_mutex_;
        std::condition_variable connections_cv_;
        SafeQueue<std::function<void()>> callback_queue_;
        std::thread callback_thread_;
        std::thread maintenance_thread_;
    };
}
