// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/session_callbacks.h"

namespace quicr {

    // -- SessionCallbacks --------------------------------------------------------------------

    Expected<const PublishResponse, Error<PublishErrorCode>> SessionCallbacks::PublishReceived(
      const std::shared_ptr<Session>&,
      std::uint64_t,
      const PublishAttributes&,
      std::weak_ptr<SubscribeNamespaceHandler>)
    {
        return Unexpected<Error<PublishErrorCode>>(PublishErrorCode::kNotSupported, "Publish is not supported");
    }

    Expected<void, Error<PublishNamespaceErrorCode>> SessionCallbacks::PublishNamespaceReceived(
      const std::shared_ptr<Session>&,
      const TrackNamespace&,
      const PublishNamespaceAttributes&)
    {
        return {};
    }

    Expected<const FetchResponse, Error<FetchErrorCode>> SessionCallbacks::StandaloneFetchReceived(
      const std::shared_ptr<Session>&,
      std::uint64_t,
      const FullTrackName&,
      const StandaloneFetchAttributes&)
    {
        return Unexpected<Error<FetchErrorCode>>(FetchErrorCode::kInternalError, "Fetch is not supported");
    }

    Expected<const FetchResponse, Error<FetchErrorCode>> SessionCallbacks::JoiningFetchReceived(
      const std::shared_ptr<Session>&,
      std::uint64_t,
      const FullTrackName&,
      const JoiningFetchAttributes&)
    {
        return Unexpected<Error<FetchErrorCode>>(FetchErrorCode::kInternalError, "Fetch is not supported");
    }

    Expected<void, Error<FetchErrorCode>> SessionCallbacks::FetchCancelReceived(const std::shared_ptr<Session>&,
                                                                                std::uint64_t)
    {
        return {};
    }

    Expected<RequestResponse, Error<RequestErrorCode>> SessionCallbacks::TrackStatusReceived(
      const std::shared_ptr<Session>&,
      std::uint64_t,
      const FullTrackName&)
    {
        return RequestResponse{};
    }

    // -- ClientSessionCallbacks ---------------------------------------------------------------

    Expected<void, Error<int>> ClientSessionCallbacks::ServerSetupReceived(const std::shared_ptr<Session>&,
                                                                           const ServerSetupAttributes&)
    {
        return {};
    }

    Expected<void, Error<int>> ClientSessionCallbacks::UnpublishedSubscribeReceived(const std::shared_ptr<Session>&,
                                                                                    const FullTrackName&,
                                                                                    const SubscribeAttributes&)
    {
        return {};
    }

    // -- ServerSessionCallbacks ---------------------------------------------------------------

    void
    ServerSessionCallbacks::OnStreamClosed(std::uint64_t, StreamClosedFlag)
    {
    }

    Expected<void, Error<int>> ServerSessionCallbacks::ClientSetupReceived(const std::shared_ptr<Session>&,
                                                                           const ClientSetupAttributes&)
    {
        return {};
    }

    Expected<std::vector<std::uint64_t>, Error<PublishNamespaceErrorCode>>
    ServerSessionCallbacks::PublishNamespaceDoneReceived(const std::shared_ptr<Session>&, std::uint64_t)
    {
        return std::vector<std::uint64_t>{};
    }

    Expected<void, Error<int>> ServerSessionCallbacks::UnsubscribeNamespaceReceived(const std::shared_ptr<Session>&,
                                                                                    const TrackNamespace&)
    {
        return {};
    }

    Expected<std::vector<TrackNamespace>, Error<RequestErrorCode>> ServerSessionCallbacks::SubscribeNamespaceReceived(
      const std::shared_ptr<Session>&,
      std::uint64_t,
      const TrackNamespace&,
      const SubscribeNamespaceAttributes&)
    {
        return std::vector<TrackNamespace>{};
    }

    Expected<std::vector<TrackNamespace>, Error<RequestErrorCode>> ServerSessionCallbacks::SubscribeTracksReceived(
      const std::shared_ptr<Session>&,
      std::uint64_t,
      const TrackNamespace&,
      const SubscribeNamespaceAttributes&)
    {
        return std::vector<TrackNamespace>{};
    }

    Expected<RequestResponse, Error<RequestErrorCode>> ServerSessionCallbacks::SubscribeReceived(
      const std::shared_ptr<Session>&,
      std::uint64_t,
      const FullTrackName&,
      const SubscribeAttributes&)
    {
        return RequestResponse{};
    }

    Expected<void, Error<int>> ServerSessionCallbacks::UnsubscribeReceived(const std::shared_ptr<Session>&,
                                                                           std::uint64_t)
    {
        return {};
    }

    Expected<void, Error<int>> ServerSessionCallbacks::PublishDoneReceived(const std::shared_ptr<Session>&,
                                                                           std::uint64_t)
    {
        return {};
    }

    Expected<void, Error<int>> ServerSessionCallbacks::NewGroupRequested(const FullTrackName&, std::uint64_t)
    {
        return {};
    }

} // namespace quicr
