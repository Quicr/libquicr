// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "detail/transport.h"

#include <memory>
#include <optional>
#include <utility>

namespace quicr {

    class Client : public Transport
    {
      public:
        static std::shared_ptr<Client> Create(const ClientConfig& cfg)
        {
            return std::shared_ptr<Client>(new Client(cfg));
        }

        Status Start() override
        {
            auto result = StartTransport();
            connection_handle_ = result.connection_handle;
            return result.status;
        }

        Status Connect() { return Start(); }

        void Stop() override
        {
            Transport::Stop();
            connection_handle_.reset();
        }

        void Disconnect() { Stop(); }

        using Transport::CancelFetchTrack;
        using Transport::FetchTrack;
        using Transport::PublishNamespace;
        using Transport::PublishNamespaceDone;
        using Transport::PublishTrack;
        using Transport::RequestTrackStatus;
        using Transport::SubscribeNamespace;
        using Transport::SubscribeTrack;
        using Transport::UnpublishTrack;
        using Transport::UnsubscribeNamespace;
        using Transport::UnsubscribeTrack;

        PublishNamespaceStatus GetPublishNamespaceStatus(const TrackNamespace&) { return {}; }

        void PublishNamespace(std::shared_ptr<PublishNamespaceHandler> handler)
        {
            if (connection_handle_) {
                Transport::PublishNamespace(*connection_handle_, std::move(handler));
            }
        }

        void PublishNamespaceDone(const std::shared_ptr<PublishNamespaceHandler>& handler)
        {
            if (connection_handle_) {
                Transport::PublishNamespaceDone(*connection_handle_, handler);
            }
        }

        void SubscribeNamespace(std::shared_ptr<SubscribeNamespaceHandler> handler)
        {
            if (connection_handle_) {
                Transport::SubscribeNamespace(*connection_handle_, std::move(handler));
            }
        }

        void UnsubscribeNamespace(const std::shared_ptr<SubscribeNamespaceHandler>& handler)
        {
            if (connection_handle_) {
                Transport::UnsubscribeNamespace(*connection_handle_, handler);
            }
        }

        void SubscribeTrack(std::shared_ptr<SubscribeTrackHandler> track_handler)
        {
            if (connection_handle_) {
                Transport::SubscribeTrack(*connection_handle_, std::move(track_handler));
            }
        }

        uint64_t RequestTrackStatus(const FullTrackName& track_full_name,
                                    const messages::SubscribeAttributes& subscribe_attributes = {})
        {
            if (connection_handle_) {
                return Transport::RequestTrackStatus(*connection_handle_, track_full_name, subscribe_attributes);
            }

            return 0;
        }

        void UnsubscribeTrack(std::shared_ptr<SubscribeTrackHandler> track_handler)
        {
            if (connection_handle_) {
                Transport::UnsubscribeTrack(*connection_handle_, std::move(track_handler));
            }
        }

        void PublishTrack(std::shared_ptr<PublishTrackHandler> track_handler)
        {
            if (connection_handle_) {
                Transport::PublishTrack(*connection_handle_, std::move(track_handler));
            }
        }

        void UnpublishTrack(std::shared_ptr<PublishTrackHandler> track_handler)
        {
            if (connection_handle_) {
                Transport::UnpublishTrack(*connection_handle_, std::move(track_handler));
            }
        }

        void FetchTrack(std::shared_ptr<FetchTrackHandler> track_handler)
        {
            if (connection_handle_) {
                Transport::FetchTrack(*connection_handle_, std::move(track_handler));
            }
        }

        void CancelFetchTrack(std::shared_ptr<FetchTrackHandler> track_handler)
        {
            if (connection_handle_) {
                Transport::CancelFetchTrack(*connection_handle_, std::move(track_handler));
            }
        }

        std::optional<ConnectionHandle> GetConnectionHandle() const noexcept { return connection_handle_; }

      protected:
        explicit Client(const ClientConfig& cfg)
          : Transport(cfg)
        {
        }

        Client(const ClientConfig& cfg, std::shared_ptr<timeq::tick_service> tick_service)
          : Transport(cfg, std::move(tick_service))
        {
        }

      private:
        std::optional<ConnectionHandle> connection_handle_;
    };

} // namespace quicr
