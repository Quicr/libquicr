#pragma once

#include "quicr/attributes.h"
#include "quicr/connection.h"
#include "quicr/messages/message.h"
#include "quicr/messages/messages.h"
#include "quicr/utilities/expected.h"

#include <cstdint>
#include <optional>
#include <string>

namespace quicr {

    class Session;
    class SubscribeNamespaceHandler;
    class SubscribeTrackHandler;

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

    struct RequestResponse
    {
        bool is_publisher_initiated = false;
        std::optional<messages::Location> largest_location = std::nullopt;
        messages::GroupOrder publisher_default_group_order = messages::GroupOrder::kAscending;
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
     * @brief Response to received MOQT Fetch message
     */
    struct FetchResponse
    {
        std::optional<messages::Location> largest_location = std::nullopt;
        messages::GroupOrder publisher_default_group_order = messages::GroupOrder::kAscending;
    };

    struct SessionCallbacks
    {
        virtual ~SessionCallbacks() = default;

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
        virtual Expected<const PublishResponse, Error<PublishErrorCode>> PublishReceived(
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
        virtual Expected<void, Error<PublishNamespaceErrorCode>> PublishNamespaceReceived(
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
        virtual Expected<const FetchResponse, Error<FetchErrorCode>> StandaloneFetchReceived(
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
        virtual Expected<const FetchResponse, Error<FetchErrorCode>> JoiningFetchReceived(
          const std::shared_ptr<Session>& session,
          std::uint64_t request_id,
          const FullTrackName& track_full_name,
          const JoiningFetchAttributes& attributes);

        /**
         * @brief Callback notification on receiving a FetchCancel message.
         *
         * @param request_id        Request ID received.
         */
        virtual Expected<void, Error<FetchErrorCode>> FetchCancelReceived(const std::shared_ptr<Session>& session,
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
        virtual Expected<RequestResponse, Error<RequestErrorCode>> TrackStatusReceived(
          const std::shared_ptr<Session>& session,
          std::uint64_t request_id,
          const FullTrackName& track_full_name);
    };

    struct ClientSessionCallbacks : public SessionCallbacks
    {
        virtual ~ClientSessionCallbacks() = default;

        /**
         * @brief Callback on server setup message
         *
         * @details Server will send server setup in response to client setup message sent. This callback is
         *      called when a server setup has been received. Client mode only.
         *
         * @param server_setup_attributes Server setup attributes received
         */
        virtual Expected<void, Error<int>> ServerSetupReceived(const std::shared_ptr<Session>& session,
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
        virtual Expected<void, Error<int>> UnpublishedSubscribeReceived(
          const std::shared_ptr<Session>& session,
          const FullTrackName& track_full_name,
          const SubscribeAttributes& subscribe_attributes);
    };

    struct ServerSessionCallbacks : public SessionCallbacks
    {
        virtual ~ServerSessionCallbacks() = default;

        virtual void OnStreamClosed(std::uint64_t stream_id, StreamClosedFlag flag);

        /**
         * @brief Callback on client setup message
         *
         * @details Server mode only. Client will send a setup message on new connection. Server responds with
         *      server setup.
         *
         * @param client_setup_attributes Decoded client setup message
         */
        virtual Expected<void, Error<int>> ClientSetupReceived(const std::shared_ptr<Session>& session,
                                                               const ClientSetupAttributes& client_setup_attributes);

        /**
         * @brief Callback notification for publish namespace done received
         *
         * @details Server mode only. The callback will indicate that publish namespace done has been received.
         *      The app should return a vector of connection handler ids that should receive a copy of the publish
         *      namespace done message. The returned list is based on subscribe namespace prefix matching.
         *
         * @param request_id        Request ID for the namespace that is done
         *
         * @returns Vector of subscribe namespace connection handler ids matching prefix to the namespace being
         *      marked as done.
         */
        virtual Expected<std::vector<std::uint64_t>, Error<quicr::PublishNamespaceErrorCode>>
        PublishNamespaceDoneReceived(const std::shared_ptr<Session>& session, std::uint64_t request_id);

        /**
         * @brief Callback notification for unsubscribe namespace received
         *
         * @details Server mode only.
         *
         * @param prefix_namespace  Prefix namespace
         */
        virtual Expected<void, Error<int>> UnsubscribeNamespaceReceived(const std::shared_ptr<Session>& session,
                                                                        const TrackNamespace& prefix_namespace);

        /**
         * @brief Callback notification for new subscribe namespace received
         *
         * @details Server mode only.
         *
         * @note The implementor **MUST** call `ResolveSubscribeNamespace()`.
         *
         * @param data_ctx_id        Data context ID that the message was received on
         * @param prefix_namespace   Track namespace prefix
         * @param attributes         Attributes received
         */
        virtual Expected<std::vector<TrackNamespace>, Error<RequestErrorCode>> SubscribeNamespaceReceived(
          const std::shared_ptr<Session>& session,
          std::uint64_t data_ctx_id,
          const TrackNamespace& prefix_namespace,
          const SubscribeNamespaceAttributes& attributes);

        /**
         * @brief Callback notification for new subscribe tracks received
         *
         * @details Server mode only.
         *
         * @note The implementor **MUST** call `ResolveSubscribeTracks()`.
         *
         * @param data_ctx_id        Data context ID that the message was received on
         * @param prefix_namespace   Track namespace prefix
         * @param attributes         Attributes received
         */
        virtual Expected<std::vector<TrackNamespace>, Error<RequestErrorCode>> SubscribeTracksReceived(
          const std::shared_ptr<Session>& session,
          std::uint64_t data_ctx_id,
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
        virtual Expected<RequestResponse, Error<RequestErrorCode>> SubscribeReceived(
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
        virtual Expected<void, Error<int>> UnsubscribeReceived(const std::shared_ptr<Session>& session,
                                                               std::uint64_t request_id);

        /**
         * @brief Callback notification on publish done received
         *
         * @details Server mode only.
         *
         * @param request_id        Request ID received
         */
        virtual Expected<void, Error<int>> PublishDoneReceived(const std::shared_ptr<Session>& session,
                                                               std::uint64_t request_id);

        /**
         * @brief New group requested received by a subscription
         *
         * @details Server mode only.
         *
         * @param track_full_name Track full name
         * @param group_id        Group ID requested — should be plus one of current group or zero
         */
        virtual Expected<void, Error<int>> NewGroupRequested(const FullTrackName& track_full_name,
                                                             std::uint64_t group_id);
    };
}
