// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/session.h"
#include "quicr/utilities/expected.h"

#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <variant>
#include <vector>

namespace quicr {

    class SubscribeNamespaceHandler;

    template<typename E>
    struct Error
    {
        E reason_code;

        std::optional<std::string> error_reason;
    };

    /**
     * @details **kOK** indicates that the subscribe is accepted and OK should be sent. Any other
     *       value indicates that the subscribe is not accepted and the reason code and other
     *       fields will be set.
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
     * @details **kOK** indicates that the subscribe is accepted and OK should be sent. Any other
     *       value indicates that the subscribe is not accepted and the reason code and other
     *       fields will be set.
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
     * @details **kOK** indicates that the announce is accepted and OK should be sent. Any other
     *       value indicates that the announce is not accepted and the reason code and other
     *       fields will be set.
     */
    enum class PublishNamespaceErrorCode : uint8_t
    {
        kOk = 0,
        kInternalError
    };

    /**
     * @details **kOK** indicates that the fetch is accepted and OK should be sent. Any other
     *       value indicates that the subscribe is not accepted and the reason code and other
     *       fields will be set.
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
     * @brief The reply to a callback, which may be answered immediately or deferred.
     *
     * @details A Reply either holds the result of the callback, or an action that will produce it. A deferred
     *      action is run off the calling thread, which lets an application answer a callback without blocking
     *      the session's message handling.
     *
     * @tparam T The value type of a successful reply.
     * @tparam E The reason code type of a failed reply.
     */
    template<typename T, typename E>
    class Reply
    {
      public:
        using ResultType = Expected<T, Error<E>>;
        using DeferType = std::function<ResultType()>;

      private:
        Reply(DeferType&& action)
          : result_(std::in_place_type<DeferType>, std::move(action))
        {
        }

        bool IsDeferred() const noexcept { return std::holds_alternative<DeferType>(result_); }

      public:
        Reply() = default;

        Reply(const Reply& other) = delete;

        Reply(Reply&& other) noexcept = default;

        Reply& operator=(const Reply& other) = delete;

        Reply& operator=(Reply&& other) noexcept = default;

        /**
         * @brief Construct an immediate reply from anything the result is constructible from, such as a value,
         *      or an Unexpected error.
         */
        template<typename U>
            requires(!std::same_as<std::remove_cvref_t<U>, Reply> && std::is_constructible_v<ResultType, U &&>)
        Reply(U&& value)
          : result_(std::in_place_type<ResultType>, std::forward<U>(value))
        {
        }

        /**
         * @brief Construct a reply whose result is produced later, off the calling thread.
         */
        static Reply Defer(DeferType&& action) { return Reply(std::move(action)); }

        /**
         * @brief Hand the result to the given continuation, either now or once the deferred action completes.
         *
         * @note Consumes the reply; it must not be resolved more than once.
         */
        template<typename F>
        void Resolve(F&& f)
        {
            if (IsDeferred()) {
                std::thread([action = std::move(std::get<DeferType>(result_)), f = std::forward<F>(f)]() mutable {
                    try {
                        f(action());
                    } catch (...) {
                    }
                }).detach();
                return;
            }

            f(std::move(std::get<ResultType>(result_)));
        }

      private:
        std::variant<ResultType, DeferType> result_;
    };

    /**
     * @brief Callback interface for session events common to both client and server mode
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
         * @details The app must call `ResolvePublish()` with a reason code of OK to accept, or another reason code
         *      to reject. In client mode the default implementation rejects with `kNotSupported`.
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
         * @note The caller **MUST** respond to this via ResolveTrackStatus(). If the caller does not
         * override this method, the default will call ResolveTrackStatus() with the status of OK
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
        virtual Reply<void, int> ServerSetupReceived(const std::shared_ptr<Session>& session,
                                                     const ServerSetupAttributes& server_setup_attributes);

        /**
         * @brief Callback notification for new subscribe received that doesn't match an existing publish track
         *
         * @details Client mode only. When a new subscribe is received that doesn't match any existing publish
         *      track, this method signals the application that there is a new subscribe full track name. The
         *      application should `PublishTrack()` within this callback (or afterwards).
         *
         * @note The caller **MUST** respond via `ResolveSubscribe()`.
         *
         * @param track_full_name      Track full name
         * @param subscribe_attributes Subscribe attributes received
         */
        virtual Reply<void, int> UnpublishedSubscribeReceived(const std::shared_ptr<Session>& session,
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
        virtual Reply<void, int> ClientSetupReceived(const std::shared_ptr<Session>& session,
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
        virtual Reply<void, int> UnsubscribeNamespaceReceived(const std::shared_ptr<Session>& session,
                                                              const TrackNamespace& prefix_namespace);

        /**
         * @brief Callback notification for new subscribe namespace received
         *
         * @details Server mode only.
         *
         * @note The implementor **MUST** call `ResolveSubscribeNamespace()`.
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
         * @details Server mode only.
         *
         * @note The implementor **MUST** call `ResolveSubscribeTracks()`.
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
         * @details Server mode only.
         *
         * @note The caller **MUST** respond to this via `ResolveSubscribe()`. If the caller does not override this
         *      method, the default will call `ResolveSubscribe()` with the status of OK.
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
        virtual Reply<void, int> UnsubscribeReceived(const std::shared_ptr<Session>& session, std::uint64_t request_id);

        /**
         * @brief Callback notification on publish done received
         *
         * @details Server mode only.
         *
         * @param request_id        Request ID received
         */
        virtual Reply<void, int> PublishDoneReceived(const std::shared_ptr<Session>& session, std::uint64_t request_id);

        /**
         * @brief New group requested received by a subscription
         *
         * @details Server mode only.
         *
         * @param track_full_name Track full name
         * @param group_id        Group ID requested — should be plus one of current group or zero
         */
        virtual Reply<void, int> NewGroupRequested(const FullTrackName& track_full_name, std::uint64_t group_id);
    };
}
