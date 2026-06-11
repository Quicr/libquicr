// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "session.h"

#include <memory>
#include <optional>
#include <utility>

namespace quicr {

    class Client : public Session
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

        void Stop() override
        {
            Session::Stop();
            connection_handle_.reset();
        }

        using Session::CancelFetchTrack;
        using Session::FetchTrack;
        using Session::PublishNamespace;
        using Session::PublishNamespaceDone;
        using Session::PublishTrack;
        using Session::RequestTrackStatus;
        using Session::SubscribeNamespace;
        using Session::SubscribeTrack;
        using Session::UnpublishTrack;
        using Session::UnsubscribeNamespace;
        using Session::UnsubscribeTrack;

        PublishNamespaceStatus GetPublishNamespaceStatus(const TrackNamespace&) { return {}; }

        void PublishNamespace(std::shared_ptr<PublishNamespaceHandler> handler)
        {
            if (connection_handle_) {
                Session::PublishNamespace(*connection_handle_, std::move(handler));
            }
        }

        void PublishNamespaceDone(const std::shared_ptr<PublishNamespaceHandler>& handler)
        {
            if (connection_handle_) {
                Session::PublishNamespaceDone(*connection_handle_, handler);
            }
        }

        void SubscribeNamespace(std::shared_ptr<SubscribeNamespaceHandler> handler)
        {
            if (connection_handle_) {
                Session::SubscribeNamespace(*connection_handle_, std::move(handler));
            }
        }

        void UnsubscribeNamespace(const std::shared_ptr<SubscribeNamespaceHandler>& handler)
        {
            if (connection_handle_) {
                Session::UnsubscribeNamespace(*connection_handle_, handler);
            }
        }

        void SubscribeTrack(std::shared_ptr<SubscribeTrackHandler> track_handler)
        {
            if (connection_handle_) {
                Session::SubscribeTrack(*connection_handle_, std::move(track_handler));
            }
        }

        uint64_t RequestTrackStatus(const FullTrackName& track_full_name,
                                    const messages::SubscribeAttributes& subscribe_attributes = {})
        {
            if (connection_handle_) {
                return Session::RequestTrackStatus(*connection_handle_, track_full_name, subscribe_attributes);
            }

            return 0;
        }

        void UnsubscribeTrack(std::shared_ptr<SubscribeTrackHandler> track_handler)
        {
            if (connection_handle_) {
                Session::UnsubscribeTrack(*connection_handle_, std::move(track_handler));
            }
        }

        void PublishTrack(std::shared_ptr<PublishTrackHandler> track_handler)
        {
            if (connection_handle_) {
                Session::PublishTrack(*connection_handle_, std::move(track_handler));
            }
        }

        void UnpublishTrack(std::shared_ptr<PublishTrackHandler> track_handler)
        {
            if (connection_handle_) {
                Session::UnpublishTrack(*connection_handle_, std::move(track_handler));
            }
        }

        void FetchTrack(std::shared_ptr<FetchTrackHandler> track_handler)
        {
            if (connection_handle_) {
                Session::FetchTrack(*connection_handle_, std::move(track_handler));
            }
        }

        void CancelFetchTrack(std::shared_ptr<FetchTrackHandler> track_handler)
        {
            if (connection_handle_) {
                Session::CancelFetchTrack(*connection_handle_, std::move(track_handler));
            }
        }

        std::optional<std::uint64_t> GetConnectionHandle() const noexcept { return connection_handle_; }

      protected:
        explicit Client(const ClientConfig& cfg)
          : Session(cfg)
        {
        }

        Client(const ClientConfig& cfg, std::shared_ptr<timeq::tick_service> tick_service)
          : Session(cfg, std::move(tick_service))
        {
        }

      private:
        std::optional<std::uint64_t> connection_handle_;
    };

} // namespace quicr
