// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/session_callbacks.h"

namespace quicr {

    // -- Session::Callbacks --------------------------------------------------------------------

    void Session::Callbacks::StatusChanged(const std::shared_ptr<Session>&, Status) {}

    void Session::Callbacks::MetricsSampled(const std::shared_ptr<Session>&, const ConnectionMetrics&) {}

    Reply<const PublishResponse, PublishErrorCode> Session::Callbacks::PublishReceived(
      const std::shared_ptr<Session>&,
      std::uint64_t,
      const PublishAttributes&,
      std::weak_ptr<SubscribeNamespaceHandler>)
    {
        return Unexpected<Error<PublishErrorCode>>(PublishErrorCode::kNotSupported, "Publish is not supported");
    }

    Reply<void, PublishNamespaceErrorCode> Session::Callbacks::PublishNamespaceReceived(
      const std::shared_ptr<Session>&,
      const TrackNamespace&,
      const PublishNamespaceAttributes&)
    {
        return {};
    }

    Reply<const FetchResponse, FetchErrorCode> Session::Callbacks::StandaloneFetchReceived(
      const std::shared_ptr<Session>&,
      std::uint64_t,
      const FullTrackName&,
      const StandaloneFetchAttributes&)
    {
        return Unexpected<Error<FetchErrorCode>>(FetchErrorCode::kInternalError, "Fetch is not supported");
    }

    Reply<const FetchResponse, FetchErrorCode> Session::Callbacks::JoiningFetchReceived(const std::shared_ptr<Session>&,
                                                                                        std::uint64_t,
                                                                                        const FullTrackName&,
                                                                                        const JoiningFetchAttributes&)
    {
        return Unexpected<Error<FetchErrorCode>>(FetchErrorCode::kInternalError, "Fetch is not supported");
    }

    Reply<void, FetchErrorCode> Session::Callbacks::FetchCancelReceived(const std::shared_ptr<Session>&, std::uint64_t)
    {
        return {};
    }

    Reply<RequestResponse, RequestErrorCode> Session::Callbacks::TrackStatusReceived(const std::shared_ptr<Session>&,
                                                                                     std::uint64_t,
                                                                                     const FullTrackName&)
    {
        return RequestResponse{};
    }

    // -- Session::ClientCallbacks ---------------------------------------------------------------

    Reply<void, int> Session::ClientCallbacks::ServerSetupReceived(const std::shared_ptr<Session>&,
                                                                   const ServerSetupAttributes&)
    {
        return {};
    }

    Reply<void, int> Session::ClientCallbacks::UnpublishedSubscribeReceived(const std::shared_ptr<Session>&,
                                                                            const FullTrackName&,
                                                                            const SubscribeAttributes&)
    {
        return {};
    }

    // -- Session::ServerCallbacks ---------------------------------------------------------------

    void Session::ServerCallbacks::OnStreamClosed(std::uint64_t, StreamClosedFlag) {}

    Reply<void, int> Session::ServerCallbacks::ClientSetupReceived(const std::shared_ptr<Session>&,
                                                                   const ClientSetupAttributes&)
    {
        return {};
    }

    Reply<void, PublishNamespaceErrorCode> Session::ServerCallbacks::PublishNamespaceDoneReceived(
      const std::shared_ptr<Session>&,
      std::uint64_t)
    {
        return {};
    }

    Reply<void, int> Session::ServerCallbacks::UnsubscribeNamespaceReceived(const std::shared_ptr<Session>&,
                                                                            const TrackNamespace&)
    {
        return {};
    }

    Reply<std::vector<TrackNamespace>, RequestErrorCode> Session::ServerCallbacks::SubscribeNamespaceReceived(
      const std::shared_ptr<Session>&,
      const TrackNamespace&,
      const SubscribeNamespaceAttributes&)
    {
        return std::vector<TrackNamespace>{};
    }

    Reply<std::vector<TrackNamespace>, RequestErrorCode> Session::ServerCallbacks::SubscribeTracksReceived(
      const std::shared_ptr<Session>&,
      const TrackNamespace&,
      const SubscribeNamespaceAttributes&)
    {
        return std::vector<TrackNamespace>{};
    }

    Reply<RequestResponse, RequestErrorCode> Session::ServerCallbacks::SubscribeReceived(
      const std::shared_ptr<Session>&,
      std::uint64_t,
      const FullTrackName&,
      const SubscribeAttributes&)
    {
        return RequestResponse{};
    }

    Reply<void, int> Session::ServerCallbacks::UnsubscribeReceived(const std::shared_ptr<Session>&, std::uint64_t)
    {
        return {};
    }

    Reply<void, int> Session::ServerCallbacks::PublishDoneReceived(const std::shared_ptr<Session>&, std::uint64_t)
    {
        return {};
    }

    Reply<void, int> Session::ServerCallbacks::NewGroupRequested(const FullTrackName&, std::uint64_t)
    {
        return {};
    }

} // namespace quicr
