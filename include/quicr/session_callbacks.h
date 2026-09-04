// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/errors.h"
#include "quicr/reply.h"
#include "quicr/session.h"
#include "quicr/utilities/expected.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace quicr {

    class SubscribeNamespaceHandler;

    /**
     * @brief Why a request was rejected, carried by the error of a `Reply`.
     *
     * @details Acceptance is expressed by the reply holding a value, so these are reasons for rejection
     *      only.
     */
    enum class RequestErrorCode : uint8_t
    {
        kInternalError = 1,
        kUnauthorized,
        kTimeout,
        kNotSupported,
        kMalformedAuthToken,
        kExpiredAuthToken,
        kDoesNotExist,
        kInvalidRange,
        kMalformedTrack,
        kDuplicateSubscription,
        kUninterested,
        kPrefixOverlap,
        kInvalidJoiningRequestId,
    };

    /**
     * @brief Why a publish was rejected, carried by the error of a `Reply`.
     *
     * @details Acceptance is expressed by the reply holding a `PublishResponse`, so these are reasons for
     *      rejection only.
     */
    enum class PublishErrorCode : std::uint8_t
    {
        kInternalError = 1,
        kNotSupported,
        kNotAuthorized,
        kRejected,
    };

    struct PublishResponse
    {
        const PublishOkAttributes attributes;
        std::shared_ptr<SubscribeTrackHandler> handler;
    };

    /**
     * @brief Why a publish namespace was rejected, carried by the error of a `Reply`.
     *
     * @details Acceptance is expressed by the reply holding a value, so `kOk` is vestigial and should not
     *      be returned as an error.
     */
    enum class PublishNamespaceErrorCode : uint8_t
    {
        kOk = 0,
        kInternalError
    };

    /**
     * @brief Why a fetch was rejected, carried by the error of a `Reply`.
     *
     * @details Acceptance is expressed by the reply holding a `FetchResponse`, so `kOk` is vestigial and
     *      should not be returned as an error.
     */
    enum class FetchErrorCode : uint8_t
    {
        kOk = 0,
        kInvalidRange,
        kNoObjects,
        kInternalError,
        // TODO: Expand reasons.
    };

    /**
     * @brief Callback interface for session events common to both client and server mode
     *
     * @details Callbacks that answer an inbound request return a `Reply`, which carries either the response
     *      or a reason code for rejecting it. The session sends the protocol message from that reply and
     *      correlates it with the request itself, so there is nothing further for the application to call
     *      and no request identifier for it to keep.
     *
     *      Return a value to accept, or an `Unexpected<Error<E>>` holding a reason code and optional reason
     *      string to reject. A `Reply<void, E>` accepts by returning `{}`.
     *
     *      An answer that cannot be given straight away is deferred with `Reply::Defer()`, which takes an
     *      action returning the same result and runs it off the session's message handling thread. This is
     *      the way to consult something slow, such as an authorization service, without stalling the
     *      session.
     *
     *      Callbacks are invoked on the transport notify thread, which also carries stream and connection
     *      events, so an immediate reply must return promptly; anything slow belongs in a deferred one. Each
     *      reply is answered exactly once, and an exception escaping a deferred action is swallowed, which
     *      leaves the request unanswered until the peer times it out.
     *
     *      A callback that is not overridden keeps its default, given per method below.
     */
    struct Session::Callbacks
    {
        virtual ~Callbacks() = default;

        virtual void OnStreamClosed(std::uint64_t stream_id, StreamClosedFlag flag);
        /**
         * @brief Callback notification for status/state change
         *
         * @details Callback notification indicates state change of connection, such as disconnected. May be
         *      invoked in either client or server mode.
         *
         * @param session          The session whose status changed
         * @param status           Changed Status value
         */
        virtual void StatusChanged(const std::shared_ptr<Session>& session, Status status);

        /**
         * @brief Notification callback to provide sampled connection metrics
         *
         * @details Invoked every `Config::metrics_sample_ms` in either client or server mode. The period based
         *      metrics reset after this callback, so a value not read here is lost. Per track metrics for the same
         *      period follow this call, via `PublishTrackHandler::MetricsSampled` and
         *      `SubscribeTrackHandler::MetricsSampled`, sharing the same `last_sample_time`.
         *
         *      Invoked on the transport notify thread, which also carries stream and connection events. Return
         *      promptly; a slow implementation backs the queue up and other notifications are dropped to make room.
         *
         * @param session          The session the metrics belong to
         * @param metrics          Copy of the connection metrics for the sample period
         */
        virtual void MetricsSampled(const std::shared_ptr<Session>& session, const ConnectionMetrics& metrics);

        /**
         * @brief Callback notification for new publish received
         *
         * @details Return the publish response to accept. Defaults to rejecting with `kNotSupported`.
         *
         * @param request_id         Incoming publish request ID
         * @param publish_attributes Attributes of the publish
         * @param sub_ns_handler     Matching subscribe namespace handler, if any
         */
        virtual Reply<const PublishResponse, PublishErrorCode> PublishReceived(
          const std::shared_ptr<Session>& session,
          std::uint64_t request_id,
          const PublishAttributes& publish_attributes,
          std::weak_ptr<SubscribeNamespaceHandler> sub_ns_handler);

        /**
         * @brief Callback notification for announce received by subscribe namespace
         *
         * @details Client mode only. Called when a PUBLISH_NAMESPACE is received for a subscribed prefix.
         *
         * @param track_namespace                Track namespace
         * @param publish_namespace_attributes   Publish announce attributes received
         */
        virtual Reply<void, PublishNamespaceErrorCode> PublishNamespaceReceived(
          const std::shared_ptr<Session>& session,
          const TrackNamespace& track_namespace,
          const PublishNamespaceAttributes& publish_namespace_attributes);

        /**
         * @brief Event to run on receiving a Standalone Fetch request.
         *
         * @details Defaults to rejecting with `kInternalError`, since fetch is not served unless implemented.
         *
         * @param request_id        Request ID received.
         * @param track_full_name   Track full name
         * @param attributes        Fetch attributes received.
         */
        virtual Reply<const FetchResponse, FetchErrorCode> StandaloneFetchReceived(
          const std::shared_ptr<Session>& session,
          std::uint64_t request_id,
          const FullTrackName& track_full_name,
          const StandaloneFetchAttributes& attributes);

        /**
         * @brief Event to run on receiving a Joining Fetch request.
         *
         * @details Defaults to rejecting with `kInternalError`, since fetch is not served unless implemented.
         *
         * @param request_id        Request ID received.
         * @param track_full_name   Track full name
         * @param attributes        Fetch attributes received.
         */
        virtual Reply<const FetchResponse, FetchErrorCode> JoiningFetchReceived(
          const std::shared_ptr<Session>& session,
          std::uint64_t request_id,
          const FullTrackName& track_full_name,
          const JoiningFetchAttributes& attributes);

        /**
         * @brief Callback notification on receiving a FetchCancel message.
         *
         * @param request_id        Request ID received.
         */
        virtual Reply<void, FetchErrorCode> FetchCancelReceived(const std::shared_ptr<Session>& session,
                                                                std::uint64_t request_id);

        /**
         * @brief Callback notification for track status message received
         *
         * @details Defaults to accepting with an empty `RequestResponse`.
         *
         * @param request_id            Request ID received
         * @param track_full_name       Track full name
         */
        virtual Reply<RequestResponse, RequestErrorCode> TrackStatusReceived(const std::shared_ptr<Session>& session,
                                                                             std::uint64_t request_id,
                                                                             const FullTrackName& track_full_name);
    };

    /**
     * @brief Callback interface for session events specific to client mode
     */
    struct Session::ClientCallbacks : public Session::Callbacks
    {
        virtual ~ClientCallbacks() = default;

        /**
         * @brief Callback on server setup message
         *
         * @details Server will send server setup in response to client setup message sent. This callback is
         *      called when a server setup has been received. Client mode only.
         *
         * @param server_setup_attributes Server setup attributes received
         */
        virtual Reply<void, ErrorCode> ServerSetupReceived(const std::shared_ptr<Session>& session,
                                                           const ServerSetupAttributes& server_setup_attributes);

        /**
         * @brief Callback notification for new subscribe received that doesn't match an existing publish track
         *
         * @details Client mode only. When a new subscribe is received that doesn't match any existing publish
         *      track, this method signals the application that there is a new subscribe full track name. The
         *      application should `PublishTrack()` within this callback (or afterwards).
         *
         *      Defaults to accepting.
         *
         * @param track_full_name      Track full name
         * @param subscribe_attributes Subscribe attributes received
         */
        virtual Reply<void, ErrorCode> UnpublishedSubscribeReceived(const std::shared_ptr<Session>& session,
                                                                    const FullTrackName& track_full_name,
                                                                    const SubscribeAttributes& subscribe_attributes);
    };

    /**
     * @brief Callback interface for session events specific to server mode
     */
    struct Session::ServerCallbacks : public Session::Callbacks
    {
        virtual ~ServerCallbacks() = default;

        virtual void OnStreamClosed(std::uint64_t stream_id, StreamClosedFlag flag);
        /**
         * @brief Callback on client setup message
         *
         * @details Server mode only. Client will send a setup message on new connection. Server responds with
         *      server setup.
         *
         * @param client_setup_attributes Decoded client setup message
         */
        virtual Reply<void, ErrorCode> ClientSetupReceived(const std::shared_ptr<Session>& session,
                                                           const ClientSetupAttributes& client_setup_attributes);

        /**
         * @brief Callback notification for publish namespace done received
         *
         * @details Server mode only. The callback will indicate that publish namespace done has been received.
         *      The app is responsible for forwarding a copy of the publish namespace done message to the
         *      subscribe namespace connections whose prefix matches.
         *
         * @param request_id        Request ID for the namespace that is done
         */
        virtual Reply<void, quicr::PublishNamespaceErrorCode> PublishNamespaceDoneReceived(
          const std::shared_ptr<Session>& session,
          std::uint64_t request_id);

        /**
         * @brief Callback notification for unsubscribe namespace received
         *
         * @details Server mode only.
         *
         * @param prefix_namespace  Prefix namespace
         */
        virtual Reply<void, ErrorCode> UnsubscribeNamespaceReceived(const std::shared_ptr<Session>& session,
                                                                    const TrackNamespace& prefix_namespace);

        /**
         * @brief Callback notification for new subscribe namespace received
         *
         * @details Server mode only. Accept by returning the namespaces already published under the prefix,
         *      which the session sends with the OK. Defaults to accepting with none.
         *
         * @param prefix_namespace   Track namespace prefix
         * @param attributes         Attributes received
         */
        virtual Reply<std::vector<TrackNamespace>, RequestErrorCode> SubscribeNamespaceReceived(
          const std::shared_ptr<Session>& session,
          const TrackNamespace& prefix_namespace,
          const SubscribeNamespaceAttributes& attributes);

        /**
         * @brief Callback notification for new subscribe tracks received
         *
         * @details Server mode only. Accept by returning the namespaces already published under the prefix,
         *      which the session sends with the OK. Defaults to accepting with none.
         *
         * @param prefix_namespace   Track namespace prefix
         * @param attributes         Attributes received
         */
        virtual Reply<std::vector<TrackNamespace>, RequestErrorCode> SubscribeTracksReceived(
          const std::shared_ptr<Session>& session,
          const TrackNamespace& prefix_namespace,
          const SubscribeNamespaceAttributes& attributes);

        /**
         * @brief Callback notification for new subscribe received
         *
         * @details Server mode only. Defaults to accepting with an empty `RequestResponse`.
         *
         * @param request_id           Request ID received
         * @param track_full_name      Track full name
         * @param subscribe_attributes Subscribe attributes received
         */
        virtual Reply<RequestResponse, RequestErrorCode> SubscribeReceived(
          const std::shared_ptr<Session>& session,
          std::uint64_t request_id,
          const FullTrackName& track_full_name,
          const SubscribeAttributes& subscribe_attributes);

        /**
         * @brief Callback notification on unsubscribe received
         *
         * @details Server mode only.
         *
         * @param request_id        Request ID received
         */
        virtual Reply<void, ErrorCode> UnsubscribeReceived(const std::shared_ptr<Session>& session,
                                                           std::uint64_t request_id);

        /**
         * @brief Callback notification on publish done received
         *
         * @details Server mode only.
         *
         * @param request_id        Request ID received
         */
        virtual Reply<void, ErrorCode> PublishDoneReceived(const std::shared_ptr<Session>& session,
                                                           std::uint64_t request_id);

        /**
         * @brief New group requested received by a subscription
         *
         * @details Server mode only.
         *
         * @param track_full_name Track full name
         * @param group_id        Group ID requested — should be plus one of current group or zero
         */
        virtual Reply<void, ErrorCode> NewGroupRequested(const FullTrackName& track_full_name, std::uint64_t group_id);
    };
}
