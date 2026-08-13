// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/session_callbacks.h"

namespace quicr {

    // -- Session::Callbacks --------------------------------------------------------------------

    void Session::Callbacks::StatusChanged(const std::shared_ptr<Session>&, Status) {}

    Expected<const PublishResponse, Error<PublishErrorCode>> Session::Callbacks::PublishReceived(
      const std::shared_ptr<Session>&,
      std::uint64_t,
      const PublishAttributes&,
      std::weak_ptr<SubscribeNamespaceHandler>)
    {
        return Unexpected<Error<PublishErrorCode>>(PublishErrorCode::kNotSupported, "Publish is not supported");
    }

    Expected<void, Error<PublishNamespaceErrorCode>> Session::Callbacks::PublishNamespaceReceived(
      const std::shared_ptr<Session>&,
      const TrackNamespace&,
      const PublishNamespaceAttributes&)
    {
        return {};
    }

    Expected<const FetchResponse, Error<FetchErrorCode>> Session::Callbacks::StandaloneFetchReceived(
      const std::shared_ptr<Session>&,
      std::uint64_t,
      const FullTrackName&,
      const StandaloneFetchAttributes&)
    {
        return Unexpected<Error<FetchErrorCode>>(FetchErrorCode::kInternalError, "Fetch is not supported");
    }

    Expected<const FetchResponse, Error<FetchErrorCode>> Session::Callbacks::JoiningFetchReceived(
      const std::shared_ptr<Session>&,
      std::uint64_t,
      const FullTrackName&,
      const JoiningFetchAttributes&)
    {
        return Unexpected<Error<FetchErrorCode>>(FetchErrorCode::kInternalError, "Fetch is not supported");
    }

    Expected<void, Error<FetchErrorCode>> Session::Callbacks::FetchCancelReceived(const std::shared_ptr<Session>&,
                                                                                  std::uint64_t)
    {
        return {};
    }

    Expected<RequestResponse, Error<RequestErrorCode>>
    Session::Callbacks::TrackStatusReceived(const std::shared_ptr<Session>&, std::uint64_t, const FullTrackName&)
    {
        return RequestResponse{};
    }

    // -- Session::ClientCallbacks ---------------------------------------------------------------

    Expected<void, Error<int>> Session::ClientCallbacks::ServerSetupReceived(const std::shared_ptr<Session>&,
                                                                             const ServerSetupAttributes&)
    {
        return {};
    }

    Expected<void, Error<int>> Session::ClientCallbacks::UnpublishedSubscribeReceived(const std::shared_ptr<Session>&,
                                                                                      const FullTrackName&,
                                                                                      const SubscribeAttributes&)
    {
        return {};
    }

    // -- Session::ServerCallbacks ---------------------------------------------------------------

    void Session::ServerCallbacks::OnStreamClosed(std::uint64_t, StreamClosedFlag) {}

    Expected<void, Error<int>> Session::ServerCallbacks::ClientSetupReceived(const std::shared_ptr<Session>&,
                                                                             const ClientSetupAttributes&)
    {
        return {};
    }

    Expected<std::vector<std::uint64_t>, Error<PublishNamespaceErrorCode>>
    Session::ServerCallbacks::PublishNamespaceDoneReceived(const std::shared_ptr<Session>&, std::uint64_t)
    {
        return std::vector<std::uint64_t>{};
    }

    Expected<void, Error<int>> Session::ServerCallbacks::UnsubscribeNamespaceReceived(const std::shared_ptr<Session>&,
                                                                                      const TrackNamespace&)
    {
        return {};
    }

    Expected<std::vector<TrackNamespace>, Error<RequestErrorCode>> Session::ServerCallbacks::SubscribeNamespaceReceived(
      const std::shared_ptr<Session>&,
      std::uint64_t,
      const TrackNamespace&,
      const SubscribeNamespaceAttributes&)
    {
        return std::vector<TrackNamespace>{};
    }

    Expected<std::vector<TrackNamespace>, Error<RequestErrorCode>> Session::ServerCallbacks::SubscribeTracksReceived(
      const std::shared_ptr<Session>&,
      std::uint64_t,
      const TrackNamespace&,
      const SubscribeNamespaceAttributes&)
    {
        return std::vector<TrackNamespace>{};
    }

    Expected<RequestResponse, Error<RequestErrorCode>> Session::ServerCallbacks::SubscribeReceived(
      const std::shared_ptr<Session>&,
      std::uint64_t,
      const FullTrackName&,
      const SubscribeAttributes&)
    {
        return RequestResponse{};
    }

    Expected<void, Error<int>> Session::ServerCallbacks::UnsubscribeReceived(const std::shared_ptr<Session>&,
                                                                             std::uint64_t)
    {
        return {};
    }

    Expected<void, Error<int>> Session::ServerCallbacks::PublishDoneReceived(const std::shared_ptr<Session>&,
                                                                             std::uint64_t)
    {
        return {};
    }

    Expected<void, Error<int>> Session::ServerCallbacks::NewGroupRequested(const FullTrackName&, std::uint64_t)
    {
        return {};
    }

} // namespace quicr
