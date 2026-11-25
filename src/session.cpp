// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/session.h"
#include "connect_uri.h"
#include "quicr/clock.h"
#include "quicr/connection.h"
#include "quicr/handlers/joining_fetch_handler.h"
#include "quicr/handlers/subscribe_namespace_handler.h"
#include "quicr/log.h"
#include "quicr/messages/ctrl_message_types.h"
#include "quicr/messages/message.h"
#include "quicr/messages/message_serialisation.h"
#include "quicr/messages/parameters.h"
#include "quicr/session_callbacks.h"
#include "quicr/utilities/defer.h"
#include "request.h"
#include "stream.h"
#include "track_properties.h"
#include "transport_picoquic.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace quicr {
    using namespace quicr::messages;
    using namespace std::chrono_literals;

    constexpr uint64_t kSubscribeExpires = 0; ///< Never expires
    constexpr int kReadLoopMax = 100; ///< Support packet/frame bursts, but do not allow starving whatever reads next

    namespace {

        /**
         * @brief Try to decode a uintvar from the front of a span, advancing past it on success.
         *
         * @param data Span to decode from, advanced past the value when one is decoded.
         *
         * @return Decoded value, or nullopt when the span holds fewer bytes than the value needs.
         */
        std::optional<std::uint64_t> TryDecodeUintV(BytesSpan& data) noexcept
        {
            if (data.empty()) {
                return std::nullopt;
            }

            const auto size = UintVar::Size(data.front());
            if (data.size() < size) {
                return std::nullopt;
            }

            const auto value = static_cast<std::uint64_t>(UintVar(data.first(size)));
            data = data.subspan(size);
            return value;
        }

        RequestErrorCode FromErrorCode(ErrorCode error_code)
        {
            switch (error_code) {
                case ErrorCode::kInternalError:
                    return RequestErrorCode::kInternalError;
                case ErrorCode::kUnauthorized:
                    return RequestErrorCode::kUnauthorized;
                case ErrorCode::kTimeout:
                    return RequestErrorCode::kTimeout;
                case ErrorCode::kNotSupported:
                    return RequestErrorCode::kNotSupported;
                case ErrorCode::kMalformedAuthToken:
                    return RequestErrorCode::kMalformedAuthToken;
                case ErrorCode::kExpiredAuthToken:
                    return RequestErrorCode::kExpiredAuthToken;
                case ErrorCode::kGoingAway:
                    return RequestErrorCode::kGoingAway;
                case ErrorCode::kDoesNotExist:
                    return RequestErrorCode::kDoesNotExist;
                case ErrorCode::kInvalidRange:
                    return RequestErrorCode::kInvalidRange;
                case ErrorCode::kMalformedTrack:
                    return RequestErrorCode::kMalformedTrack;
                case ErrorCode::kDuplicateSubscription:
                    return RequestErrorCode::kDuplicateSubscription;
                case ErrorCode::kUninterested:
                    return RequestErrorCode::kUninterested;
                case ErrorCode::kPrefixOverlap:
                    return RequestErrorCode::kPrefixOverlap;
                case ErrorCode::kInvalidJoiningRequestId:
                    return RequestErrorCode::kInvalidJoiningRequestId;
                case ErrorCode::kRedirect:
                    return RequestErrorCode::kRedirect;
            }
        }

        ErrorCode ToErrorCode(RequestErrorCode error_code)
        {
            switch (error_code) {
                case RequestErrorCode::kInternalError:
                    return ErrorCode::kInternalError;
                case RequestErrorCode::kUnauthorized:
                    return ErrorCode::kUnauthorized;
                case RequestErrorCode::kTimeout:
                    return ErrorCode::kTimeout;
                case RequestErrorCode::kNotSupported:
                    return ErrorCode::kNotSupported;
                case RequestErrorCode::kMalformedAuthToken:
                    return ErrorCode::kMalformedAuthToken;
                case RequestErrorCode::kExpiredAuthToken:
                    return ErrorCode::kExpiredAuthToken;
                case RequestErrorCode::kGoingAway:
                    return ErrorCode::kGoingAway;
                case RequestErrorCode::kDoesNotExist:
                    return ErrorCode::kDoesNotExist;
                case RequestErrorCode::kInvalidRange:
                    return ErrorCode::kInvalidRange;
                case RequestErrorCode::kMalformedTrack:
                    return ErrorCode::kMalformedTrack;
                case RequestErrorCode::kDuplicateSubscription:
                    return ErrorCode::kDuplicateSubscription;
                case RequestErrorCode::kUninterested:
                    return ErrorCode::kUninterested;
                case RequestErrorCode::kPrefixOverlap:
                    return ErrorCode::kPrefixOverlap;
                case RequestErrorCode::kInvalidJoiningRequestId:
                    return ErrorCode::kInvalidJoiningRequestId;
                case RequestErrorCode::kRedirect:
                    return ErrorCode::kRedirect;
            }
        }

        ErrorCode ToErrorCode(PublishNamespaceErrorCode error_code)
        {
            switch (error_code) {
                case PublishNamespaceErrorCode::kRedirect:
                    return ErrorCode::kRedirect;
                case PublishNamespaceErrorCode::kOk:
                case PublishNamespaceErrorCode::kInternalError:
                    return ErrorCode::kInternalError;
            }
        }

        std::shared_ptr<TrackHandler> GetRequestHandler(const std::shared_ptr<Request>& request)
        {
            return request && !request->handlers.empty() ? request->handlers.back() : nullptr;
        }

        std::uint64_t ValidateRequest(const std::shared_ptr<Request>& request)
        {
            if (!request) {
                throw std::invalid_argument("Request cannot be null");
            }

            const auto stream = request->GetStream();
            if (!stream) {
                throw std::logic_error("Request is missing its request stream");
            }

            const auto connection_id = stream->GetConnectionID();
            for (const auto& handler : request->handlers) {
                if (!handler) {
                    throw std::logic_error("Request contains a null handler");
                }
            }

            return connection_id;
        }

    }

    void Session::AddRequest(const std::shared_ptr<Request>& request)
    {
        const auto connection_id = ValidateRequest(request);
        const auto request_stream = request->GetStream();
        const RequestKey request_key{ connection_id, request->request_id };
        const StreamKey stream_key{ connection_id, request_stream->GetStreamId() };

        if (requests.contains(request_key)) {
            throw std::logic_error("Request is already registered");
        }

        if (const auto stream_request = request_stream->request_.lock(); stream_request && stream_request != request) {
            throw std::logic_error("Request stream is already bound to another request");
        }

        requests.emplace(request_key, request);

        try {
            const auto [stream_it, inserted] = request_by_stream.try_emplace(
              stream_key, StreamRequest{ .request_id = request->request_id, .is_request_stream = true });

            if (!inserted &&
                (stream_it->second.request_id != request->request_id || !stream_it->second.is_request_stream)) {
                throw std::logic_error("Request stream is already registered to another request");
            }
        } catch (...) {
            requests.erase(request_key);
            throw;
        }

        request_stream->request_ = request;
    }

    void Session::BindHandlerToRequest(const std::shared_ptr<Request>& request,
                                       const std::shared_ptr<TrackHandler>& handler)
    {
        if (!request || !handler) {
            throw std::invalid_argument("Request and handler cannot be null");
        }

        if (FindRequestForHandler(*handler)) {
            throw std::logic_error("Request handler is already bound");
        }

        request->handlers.push_back(handler);
        try {
            handler->BindRequest(request);
        } catch (...) {
            request->handlers.pop_back();
            throw;
        }
    }

    std::shared_ptr<Request> Session::FindRequest(const RequestKey& key) const
    {
        const auto request = requests.find(key);
        return request == requests.end() ? nullptr : request->second;
    }

    std::shared_ptr<Request> Session::FindRequestForHandler(const TrackHandler& handler) const
    {
        const auto& bound_request = handler.GetRequest();
        const auto bound_stream = bound_request ? bound_request->GetStream() : nullptr;
        if (!bound_stream) {
            return nullptr;
        }

        const auto request = FindRequest({ bound_stream->GetConnectionID(), bound_request->request_id });
        if (request != bound_request) {
            return nullptr;
        }

        const auto is_bound =
          std::ranges::any_of(request->handlers, [&](const auto& bound) { return bound.get() == &handler; });
        if (!is_bound) {
            return nullptr;
        }

        return request;
    }

    std::shared_ptr<Request> Session::FindResponseRequest(const Connection& connection, std::uint64_t request_id)
    {
        if (auto request = FindRequest({ connection.GetID(), request_id })) {
            return request;
        }

        const auto migration = FindMigration(connection);
        if (migration == migrations_.end() || !migration->second.pending.contains(request_id)) {
            return nullptr;
        }

        return FindRequest({ migration->second.source_connection->GetID(), request_id });
    }

    bool Session::RemoveRequest(const std::shared_ptr<Request>& request)
    {
        // Do not take a handler's binding_operation_mutex_ here. A publication that already holds
        // it retains this request and is allowed to drain before the caller closes its subgroups.
        // New publications observe the cleared handler binding and fail with kNoSubscribers.

        if (!request) {
            return false;
        }

        const auto request_stream = request->GetStream();
        if (!request_stream) {
            return false;
        }

        const auto removed_request = request;
        const auto connection_id = request_stream->GetConnectionID();
        const auto request_it = requests.find({ connection_id, removed_request->request_id });
        if (request_it == requests.end() || request_it->second.get() != removed_request.get()) {
            return false;
        }

        request_by_stream.erase({ connection_id, request_stream->GetStreamId() });
        requests.erase(request_it);

        if (request_stream->request_.lock() == removed_request) {
            request_stream->request_.reset();
        }

        while (!removed_request->handlers.empty()) {
            UnbindHandler(removed_request, *removed_request->handlers.back());
        }

        return true;
    }

    bool Session::UnbindHandler(const std::shared_ptr<Request>& request, TrackHandler& handler)
    {
        const auto handler_it =
          std::ranges::find_if(request->handlers, [&](const auto& bound) { return bound.get() == &handler; });
        if (handler_it == request->handlers.end()) {
            return false;
        }

        handler.UnbindRequest();

        request->handlers.erase(handler_it);
        return true;
    }

    TransportException::TransportException(TransportError error, std::source_location location)
      : std::runtime_error("Error in transport (error=" + std::to_string(static_cast<int>(error)) + ", " +
                           std::to_string(location.line()) + ", " + location.file_name() + ")")
      , Error(error)
    {
    }

    Session::Session(const ClientConfig& cfg,
                     std::shared_ptr<Transport> transport,
                     std::shared_ptr<Connection> connection,
                     std::shared_ptr<ClientCallbacks> callbacks,
                     std::shared_ptr<Clock> clock,
                     std::shared_ptr<Logger> logger)
      : std::enable_shared_from_this<Session>()
      , default_connection_(std::move(connection))
      , callbacks_(std::move(callbacks))
      , next_request_id_(0)
      , client_mode_(true)
      , logger_(std::move(logger))
      , server_config_({})
      , client_config_(cfg)
      , clock_(std::move(clock))
      , quic_transport_(std::move(transport))
    {
        QUICR_LOGGER_TRACE(logger_, "Created MoQ Session in client mode connected to {}", cfg.connect_uri);
        Init();
    }

    Session::Session(const ServerConfig& cfg,
                     std::shared_ptr<Transport> transport,
                     std::shared_ptr<Connection> connection,
                     std::shared_ptr<ServerCallbacks> callbacks,
                     std::shared_ptr<Clock> clock,
                     std::shared_ptr<Logger> logger)
      : std::enable_shared_from_this<Session>()
      , default_connection_(std::move(connection))
      , callbacks_(std::move(callbacks))
      , next_request_id_(1)
      , client_mode_(false)
      , logger_(std::move(logger))
      , server_config_(cfg)
      , client_config_({})
      , clock_(std::move(clock))
      , quic_transport_(std::move(transport))
    {
        QUICR_LOGGER_INFO(
          logger_, "Created MoQ Session in server mode listening on {}:{}", cfg.server_bind_ip, cfg.server_port);
        Init();
    }

    Session::Session(const ClientConfig& cfg,
                     std::shared_ptr<Transport> transport,
                     std::shared_ptr<Connection> connection,
                     std::shared_ptr<ClientCallbacks> callbacks,
                     std::shared_ptr<Logger> logger)
      : Session(cfg,
                std::move(transport),
                std::move(connection),
                std::move(callbacks),
                Clock::Create(cfg.tick_service_sleep_delay_us),
                std::move(logger))
    {
    }

    Session::Session(const ServerConfig& cfg,
                     std::shared_ptr<Transport> transport,
                     std::shared_ptr<Connection> connection,
                     std::shared_ptr<ServerCallbacks> callbacks,
                     std::shared_ptr<Logger> logger)
      : Session(cfg,
                std::move(transport),
                std::move(connection),
                std::move(callbacks),
                Clock::Create(cfg.tick_service_sleep_delay_us),
                std::move(logger))
    {
    }

    Session::~Session()
    {
        std::lock_guard lock(state_mutex_);
        while (!requests.empty()) {
            RemoveRequest(requests.begin()->second);
        }
    }

    std::shared_ptr<Connection> Session::GetDefaultConnection() const noexcept
    {
        std::lock_guard lock(state_mutex_);
        return default_connection_;
    }

    void Session::Init()
    {
        const auto connection = GetDefaultConnection();
        {
            std::lock_guard lock(connections_mutex_);
            connections_.emplace(connection->GetID(), connection);
        }

        if (client_mode_) {
            // client init items

            if (client_config_.transport_config.debug) {
                if (logger_) {
                    logger_->SetLevel(Logger::Level::Debug);
                }
            }
        } else {
            // Server init items

            if (server_config_.transport_config.debug) {
                if (logger_) {
                    logger_->SetLevel(Logger::Level::Debug);
                }
            }
        }
    }

    void Session::CheckReady(const std::shared_ptr<Connection>& connection)
    {
        if (!connection->IsSetupComplete()) {
            return;
        }

        if (connection != GetDefaultConnection()) {
            StartMigrationRequests(connection);
            return;
        }

        if (GetStatus() == Status::kPendingPeerSetup) {
            SetStatus(Status::kReady);
        }
    }

    std::shared_ptr<Connection> Session::GetConnection(std::uint64_t connection_id) const
    {
        std::lock_guard lock(connections_mutex_);
        const auto it = connections_.find(connection_id);
        return it == connections_.end() ? nullptr : it->second;
    }

    std::shared_ptr<Connection> Session::GetConnectionForStream(const std::shared_ptr<Stream>& stream) const
    {
        return stream ? stream->GetConnection() : nullptr;
    }

    void Session::Disconnect()
    {
        decltype(connections_) connections;
        {
            std::lock_guard lock(connections_mutex_);
            connections.swap(connections_);
        }

        {
            std::lock_guard lock(state_mutex_);
            for (const auto& [_, migration] : migrations_) {
                for (const auto& item : migration.pending) {
                    const auto& pending = item.second;
                    if (pending.request_stream) {
                        request_by_stream.erase(
                          { migration.candidate_connection->GetID(), pending.request_stream->GetStreamId() });
                    }
                }
            }
            migrations_.clear();
            default_connection_ = nullptr;
        }

        for (const auto& [_, connection] : connections) {
            RemoveTracksForConnectionClose(connection);
            connection->SetDelegate(nullptr);
            if (quic_transport_) {
                quic_transport_->Close(connection);
            }
        }
        SetStatus(Status::kNotConnected);
    }

    void Session::GoAway(const std::string& new_session_uri,
                         std::chrono::milliseconds timeout,
                         std::optional<std::uint64_t> first_unprocessed_request_id)
    {
        if (timeout.count() < 0) {
            throw std::invalid_argument("GOAWAY timeout cannot be negative");
        }

        if (new_session_uri.size() > detail::kMaxUriLength) {
            throw std::invalid_argument(quicr::format("GOAWAY URI exceeds {} bytes", detail::kMaxUriLength));
        }

        if (client_mode_ && !new_session_uri.empty()) {
            throw std::invalid_argument("A client GOAWAY cannot contain a new session URI");
        }

        const auto connection = GetDefaultConnection();
        if (!connection) {
            throw std::logic_error("Session has no default connection");
        }

        const auto stream = connection->GetTxControlStream();
        if (first_unprocessed_request_id.has_value()) {
            connection->SetFirstUnprocessedRequestId(*first_unprocessed_request_id);
            SendCtrlMsg(stream,
                        ControlMessageType::kGoaway,
                        AsOwnedBytes(new_session_uri),
                        UintVar(timeout.count()),
                        UintVar(*first_unprocessed_request_id));
        } else {
            SendCtrlMsg(stream, ControlMessageType::kGoaway, AsOwnedBytes(new_session_uri), UintVar(timeout.count()));
        }
    }

    void Session::GoAway(const std::string& new_session_uri,
                         std::chrono::milliseconds timeout,
                         const std::shared_ptr<TrackHandler>& handler)
    {
        if (!handler) {
            throw std::invalid_argument("Cannot send request GOAWAY without a handler");
        }

        if (handler->GetSession().lock() != GetSharedPtr()) {
            throw std::invalid_argument("Handler is not registered with this session");
        }

        if (timeout.count() < 0) {
            throw std::invalid_argument("GOAWAY timeout cannot be negative");
        }

        if (new_session_uri.size() > detail::kMaxUriLength) {
            throw std::invalid_argument(quicr::format("GOAWAY URI exceeds {} bytes", detail::kMaxUriLength));
        }

        if (client_mode_ && !new_session_uri.empty()) {
            throw std::invalid_argument("A client GOAWAY cannot contain a new session URI");
        }

        const auto& request = handler->GetRequest();
        SendCtrlMsg(request ? request->GetStream() : nullptr,
                    ControlMessageType::kGoaway,
                    AsOwnedBytes(new_session_uri),
                    UintVar(timeout.count()));
    }

    void Session::SendSetup(const std::shared_ptr<Connection>& connection, const std::string& connect_uri)
    try {
        QUICR_LOGGER_DEBUG(logger_, "Sending SETUP to conn_id: {}", connection->GetID());

        KeyValuePairs setup_options;

        if (client_mode_) {
            setup_options.Add(SetupOptionType::kEndpointId, client_config_.endpoint_id);

            const auto [host, port, protocol, path] =
              *detail::ParseConnectUri(connect_uri, connection->GetProtocol() == Connection::Protocol::kQmux);
            if (protocol != Connection::Protocol::kWebTransport) {
                const auto authority = port > 0 ? host + ":" + std::to_string(port) : host;
                setup_options.Add(SetupOptionType::kAuthority, authority);
                if (!path.empty()) {
                    setup_options.Add(SetupOptionType::kPath, path);
                }
            }

        } else {
            setup_options.Add(SetupOptionType::kEndpointId, server_config_.endpoint_id);
        }

        SendCtrlMsg(connection->GetTxControlStream(), ControlMessageType::kSetup, setup_options);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending Setup (error={})", e.what());
        throw e;
    }

    void Session::SendTrackStatusOk(const std::shared_ptr<Stream>& stream, const TrackStatusResponse& response)
    {
        SendRequestOk(stream,
                      Parameters().AddOptional(ParameterType::kLargestObject, response.largest_location),
                      response.track_properties,
                      true);
    }

    void Session::SendSubscribeNamespaceOk(const std::shared_ptr<Stream>& stream)
    {
        SendRequestOk(stream, {});
    }

    void Session::SendRequestUpdateOk(const std::shared_ptr<Stream>& stream,
                                      std::optional<std::uint64_t> expires,
                                      const std::optional<messages::Location>& largest_object)
    {
        SendRequestOk(stream,
                      Parameters()
                        .AddOptional(ParameterType::kExpires, expires)
                        .AddOptional(ParameterType::kLargestObject, largest_object));
    }

    void Session::SendRequestOk(const std::shared_ptr<Stream>& stream,
                                const messages::Parameters& params,
                                const TrackExtensions& track_properties,
                                bool close_stream)
    try {
        QUICR_LOGGER_DEBUG(
          logger_, "Sending REQUEST_OK to conn_id: {} stream_id: {}", stream->GetConnectionID(), stream->GetStreamId());

        SendCtrlMsg(stream, close_stream, ControlMessageType::kRequestOk, params, track_properties);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending REQUEST_OK (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendRequestUpdate(const std::shared_ptr<Stream>& stream,
                                    [[maybe_unused]] quicr::TrackHash th,
                                    std::optional<std::uint64_t> end_group_id,
                                    std::uint8_t priority,
                                    bool forward)
    try {
        auto params = Parameters{}
                        .Add(ParameterType::kSubscriberPriority, priority)
                        .Add(ParameterType::kForward, forward)
                        .AddOptional(ParameterType::kNewGroupRequest, end_group_id);

        const auto update_request_id = GetNextRequestID();
        QUICR_LOGGER_DEBUG(logger_,
                           "Sending REQUEST_UPDATE to conn_id: {} update_request_id: {} track namespace hash: {} name "
                           "hash: {} forward: {} ngr: {}",
                           stream->GetConnectionID(),
                           update_request_id,
                           th.track_namespace_hash,
                           th.track_name_hash,
                           forward,
                           end_group_id.has_value());

        SendCtrlMsg(stream, ControlMessageType::kRequestUpdate, UintVar(update_request_id), params);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending REQUEST_UPDATE (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendRequestError(const std::shared_ptr<Stream>& stream,
                                   [[maybe_unused]] uint64_t request_id,
                                   ErrorCode error,
                                   std::chrono::milliseconds retry_interval,
                                   const std::string& reason,
                                   bool close_stream,
                                   std::optional<Redirect> redirect)
    try {
        if ((error == ErrorCode::kRedirect) != redirect.has_value()) {
            throw std::invalid_argument("REQUEST_ERROR must carry Redirect exactly when its error code is REDIRECT");
        }

        if (redirect && redirect->uri.size() > detail::kMaxUriLength) {
            throw std::invalid_argument(
              quicr::format("REQUEST_ERROR redirect URI exceeds {} bytes", detail::kMaxUriLength));
        }

        if (client_mode_ && redirect && !redirect->uri.empty()) {
            throw std::invalid_argument("A client REQUEST_ERROR redirect cannot contain a connect URI");
        }

        QUICR_LOGGER_DEBUG(logger_,
                           "Sending REQUEST_ERROR to conn_id: {} request_id: {} error code: {} reason: {}",
                           stream->GetConnectionID(),
                           request_id,
                           static_cast<int>(error),
                           reason);

        if (redirect.has_value()) {
            SendCtrlMsg(stream,
                        close_stream,
                        ControlMessageType::kRequestError,
                        error,
                        UintVar(retry_interval.count()),
                        AsOwnedBytes(reason),
                        AsOwnedBytes(redirect->uri),
                        redirect->track_namespace,
                        redirect->track_name.value_or(Bytes{}));
        } else {
            SendCtrlMsg(stream,
                        close_stream,
                        ControlMessageType::kRequestError,
                        error,
                        UintVar(retry_interval.count()),
                        AsOwnedBytes(reason));
        }
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending REQUEST_ERROR (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    bool Session::SendPublishNamespace(const std::shared_ptr<Stream>& stream,
                                       std::uint64_t request_id,
                                       const TrackNamespace& track_namespace)
    try {
        QUICR_LOGGER_DEBUG(logger_,
                           "Sending PublishNamespace to conn_id: {} request_id: {} namespace_hash: {}",
                           stream->GetConnectionID(),
                           request_id,
                           TrackHash({ track_namespace, {} }).track_namespace_hash);

        SendCtrlMsg(stream, ControlMessageType::kPublishNamespace, UintVar(request_id), track_namespace, Parameters{});
        return true;
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending PublishNamespace (error={})", e.what());
        return false;
    }

    void Session::SendTrackStatus(const std::shared_ptr<Stream>& stream,
                                  std::uint64_t request_id,
                                  const FullTrackName& tfn)
    try {
        QUICR_LOGGER_DEBUG(
          logger_, "Sending TRACK_STATUS to conn_id: {} request_id: {}", stream->GetConnectionID(), request_id);

        SendCtrlMsg(
          stream, true, ControlMessageType::kTrackStatus, UintVar(request_id), tfn.name_space, tfn.name, Parameters{});
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending TRACK_STATUS (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    bool Session::SendSubscribe(const std::shared_ptr<Stream>& stream,
                                uint64_t request_id,
                                const FullTrackName& tfn,
                                TrackHash th, // TODO: This is only for a debug message, should be removed
                                std::uint8_t priority,
                                std::optional<GroupOrder> group_order,
                                const Filter& filter,
                                std::optional<std::chrono::milliseconds> delivery_timeout,
                                std::chrono::milliseconds rendezvous_timeout)
    try {
        /* Available parameters:
         * - AUTHORIZATION TOKEN (0x03): Conveys information to authorize the subscription.
         * - DELIVERY TIMEOUT (0x02): Duration the relay should attempt forwarding objects.
         * - RENDEZVOUS TIMEOUT (0x04): Duration to wait for a publisher to become available.
         * - SUBSCRIBER PRIORITY (0x20): Priority of the subscription relative to others.
         * - GROUP ORDER (0x22): Preference for group delivery order (Ascending/Descending).
         * - SUBSCRIPTION FILTER (0x21): Specifies which objects the publisher should send.
         * - FORWARD (0x10): Specifies the Forwarding State (0 or 1).
         * - NEW GROUP REQUEST (0x32): Requests the publisher to start a new group.
         */
        auto params =
          Parameters{}
            .AddOptional(ParameterType::kGroupOrder, group_order)
            .Add(ParameterType::kForward, 1)
            .AddOptional(ParameterType::kDeliveryTimeout,
                         delivery_timeout.has_value() ? std::make_optional(delivery_timeout->count()) : std::nullopt);

        if (priority) {
            params.Add(ParameterType::kSubscriberPriority, priority);
        }

        if (rendezvous_timeout > std::chrono::milliseconds::zero()) {
            params.Add(ParameterType::kRendezvousTimeout, rendezvous_timeout.count());
        }

        if (auto filter_type = GetFilterParameterType(filter); filter_type != ParameterType::kInvalid) {
            params.Add(filter_type, filter);
        }

        QUICR_LOGGER_DEBUG(logger_,
                           "Sending SUBSCRIBE to conn_id: {} request_id: {} track namespace hash: {} name hash: {}",
                           stream->GetConnectionID(),
                           request_id,
                           th.track_namespace_hash,
                           th.track_name_hash);

        SendCtrlMsg(stream, ControlMessageType::kSubscribe, UintVar(request_id), tfn.name_space, tfn.name, params);
        return true;
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending Subscribe (error={})", e.what());
        return false;
    }

    bool Session::SendPublish(const std::shared_ptr<Stream>& stream,
                              std::uint64_t request_id,
                              const PublishAttributes& publish)
    try {
        /* Available parameters:
         * - AUTHORIZATION TOKEN (0x03): Conveys authorization for the publisher to initiate the track.
         * - EXPIRES (0x08): Time in milliseconds after which the publisher will terminate the subscription.
         * - LARGEST OBJECT (0x09): The largest Location in the Track observed by the sender.
         * - FORWARD (0x10): Specifies the initial Forwarding State.
         */
        auto params = Parameters{}
                        .Add(ParameterType::kForward, publish.forward)
                        .AddOptional(ParameterType::kExpires, publish.expires)
                        .AddOptional(ParameterType::kLargestObject, publish.largest_object);

        auto extensions = TrackExtensions{}
                            .AddOptional(ExtensionType::kDeliveryTimeout, publish.delivery_timeout)
                            .AddOptional(ExtensionType::kMaxCacheDuration, publish.max_cache_duration)
                            .Add(ExtensionType::kDefaultPublisherGroupOrder, publish.default_publisher_group_order)
                            .Add(ExtensionType::kDefaultPublisherPriority, publish.publisher_priority)
                            .Add(ExtensionType::kDynamicGroups, publish.dynamic_groups);

        QUICR_LOGGER_DEBUG(logger_,
                           "Sending PUBLISH to conn_id: {} request_id: {} track alias: {}",
                           stream->GetConnectionID(),
                           request_id,
                           publish.track_alias);

        SendCtrlMsg(stream,
                    ControlMessageType::kPublish,
                    UintVar(request_id),
                    publish.track_full_name.name_space,
                    publish.track_full_name.name,
                    UintVar(publish.track_alias),
                    params,
                    extensions);
        return true;
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending Publish (error={})", e.what());
        return false;
    }

    void Session::SendPublishOk(const std::shared_ptr<Stream>& stream, const PublishOkAttributes& attributes)
    try {
        // Attributes -> Parameters.
        auto params = Parameters{}
                        .AddOptional(ParameterType::kSubscriberPriority, attributes.subscriber_priority)
                        .AddOptional(ParameterType::kGroupOrder, attributes.group_order)
                        .AddOptional(ParameterType::kNewGroupRequest, attributes.new_group_request_id)
                        .AddOptional(ParameterType::kForward, attributes.forward);
        const std::optional<std::uint64_t> object_timeout =
          attributes.object_delivery_timeout.has_value()
            ? std::make_optional(attributes.object_delivery_timeout.value().count())
            : std::nullopt;
        params.AddOptional(ParameterType::kDeliveryTimeout, object_timeout);
        const std::optional<std::uint64_t> subgroup_timeout =
          attributes.subgroup_delivery_timeout.has_value()
            ? std::make_optional(attributes.subgroup_delivery_timeout.value().count())
            : std::nullopt;
        params.AddOptional(ParameterType::kSubgroupDeliveryTimeout, subgroup_timeout);

        if (const auto filter_type = GetFilterParameterType(attributes.filter);
            filter_type != ParameterType::kInvalid) {
            params.Add(filter_type, attributes.filter);
        }

        SendRequestOk(stream, params);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending Publish Ok (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendSubscribeOk(const std::shared_ptr<Stream>& stream,
                                  [[maybe_unused]] uint64_t request_id,
                                  uint64_t track_alias,
                                  const SubscribeResponse& response)
    try {
        const auto expires_ms =
          response.expires.has_value() ? std::make_optional(response.expires->count()) : std::nullopt;
        auto params = Parameters{}
                        .AddOptional(ParameterType::kExpires, expires_ms)
                        .AddOptional(ParameterType::kLargestObject, response.largest_location);

        QUICR_LOGGER_DEBUG(
          logger_, "Sending SUBSCRIBE OK to conn_id: {} request_id: {}", stream->GetConnectionID(), request_id);

        SendCtrlMsg(stream, ControlMessageType::kSubscribeOk, UintVar(track_alias), params, response.track_properties);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending SubscribeOk (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendPublishDone(const std::shared_ptr<Stream>& stream,
                                  uint64_t request_id,
                                  messages::PublishDoneStatusCode status,
                                  std::uint64_t stream_count,
                                  const std::string& reason)
    try {
        QUICR_LOGGER_DEBUG(logger_,
                           "Sending PUBLISH_DONE to conn_id: {} request_id: {} status: {} stream_count: {}",
                           stream->GetConnectionID(),
                           request_id,
                           static_cast<uint64_t>(status),
                           stream_count);

        SendCtrlMsg(stream, ControlMessageType::kPublishDone, status, UintVar(stream_count), AsOwnedBytes(reason));
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending PUBLISH_DONE (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    bool Session::SendSubscribeNamespace(const std::shared_ptr<Stream>& stream,
                                         std::uint64_t request_id,
                                         const TrackNamespace& prefix,
                                         const messages::Filter& filter,
                                         messages::ControlMessageType type)
    try {
        Parameters params;
        if (const auto filter_type = GetFilterParameterType(filter); filter_type != ParameterType::kInvalid) {
            params.Add(filter_type, filter);
        }

        const char* log_name =
          type == ControlMessageType::kSubscribeNamespace ? "SUBSCRIBE_NAMESPACE" : "SUBSCRIBE_TRACKS";

        [[maybe_unused]] auto th = TrackHash({ prefix, {} });

        QUICR_LOGGER_DEBUG(logger_,
                           "Sending {} to conn_id: {} request_id: {} prefix_hash: {}",
                           log_name,
                           stream->GetConnectionID(),
                           request_id,
                           th.track_namespace_hash);

        SendCtrlMsg(stream, type, UintVar(request_id), prefix, params);
        return true;
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending subscribe namespace (error={})", e.what());
        return false;
    }

    void Session::SendNamespace(const TrackNamespace& track_namespace)
    {
        SendNamespaceMessage(ControlMessageType::kNamespace, track_namespace);
    }

    void Session::SendNamespaceDone(const TrackNamespace& track_namespace)
    {
        SendNamespaceMessage(ControlMessageType::kNamespaceDone, track_namespace);
    }

    void Session::SendNamespaceMessage(ControlMessageType type, const TrackNamespace& track_namespace)
    {
        std::vector<std::pair<std::shared_ptr<Stream>, TrackNamespace>> outgoing;
        {
            std::lock_guard _(state_mutex_);
            for (const auto request_id : recv_subscribe_namespaces) {
                const auto it = recv_req_id.find(request_id);
                if (it == recv_req_id.end() || it->second.stream == nullptr) {
                    continue;
                }

                const auto& prefix = it->second.track_full_name.name_space;
                const auto match = prefix.IsPrefixOf(track_namespace);
                if (match == std::partial_ordering::unordered || match == std::partial_ordering::greater) {
                    continue;
                }

                const auto suffix_size = track_namespace.GetEntries().size() - prefix.GetEntries().size();
                outgoing.emplace_back(it->second.stream, track_namespace.GetSuffix(suffix_size));
            }
        }

        const char* log_name = type == ControlMessageType::kNamespace ? "NAMESPACE" : "NAMESPACE_DONE";
        for (const auto& [stream, suffix] : outgoing) {
            try {
                QUICR_LOGGER_DEBUG(
                  logger_, "Sending {} to conn_id: {} suffix: {}", log_name, stream->GetConnectionID(), suffix.Str());
                SendCtrlMsg(stream, type, suffix);
            } catch (const std::exception& e) {
                QUICR_LOGGER_ERROR(logger_, "Caught exception sending {} (error={})", log_name, e.what());
            }
        }
    }

    void Session::SubscribeNamespace(std::shared_ptr<SubscribeNamespaceHandler> handler)
    {
        const auto connection = GetDefaultConnection();
        if (!connection) {
            throw std::logic_error("Session has no default connection");
        }

        if (!handler) {
            throw std::invalid_argument("Handler cannot be null");
        }

        handler->RegisterSession(GetSharedPtr());

        const auto& prefix = handler->GetPrefix();
        const auto request = std::make_shared<Request>(Request{
          .request_id = GetNextRequestID(),
          .type = Request::Type::kSubscribeNamespace,
          .stream = quic_transport_->CreateRequestStream(connection),
          .name = { prefix, {} },
          .handlers = {},
          .is_locally_initiated = true,
        });

        [[maybe_unused]] auto th = TrackHash({ prefix, {} });

        QUICR_LOGGER_INFO(logger_,
                          "Subscribe namespace conn_id: {} prefix_hash: {} mode: {}",
                          connection->GetID(),
                          th.track_namespace_hash,
                          handler->GetMode() == SubscribeNamespaceHandler::Mode::kNamespaces ? "namespaces" : "tracks");

        {
            std::lock_guard _(state_mutex_);
            AddRequest(request);
            BindHandlerToRequest(request, handler);
        }

        const bool is_tracks = handler->GetMode() == SubscribeNamespaceHandler::Mode::kTracks;
        QUICR_LOGGER_INFO(logger_,
                          "Subscribe namespace conn_id: {} prefix_hash: {} mode: {}",
                          connection->GetID(),
                          TrackHash({ prefix, {} }).track_namespace_hash,
                          is_tracks ? "tracks" : "namespaces");

        SendSubscribeNamespace(request->GetStream(),
                               request->request_id,
                               prefix,
                               handler->GetFilter(),
                               is_tracks ? ControlMessageType::kSubscribeTracks
                                         : ControlMessageType::kSubscribeNamespace);
    }

    void Session::UnsubscribeNamespace(const std::shared_ptr<SubscribeNamespaceHandler>& handler)
    {
        if (handler == nullptr) {
            throw std::invalid_argument("Handler must not be null");
        }

        handler->UnregisterSession(GetSharedPtr());

        const auto& prefix = handler->GetPrefix();
        [[maybe_unused]] auto th = TrackHash({ prefix, {} });
        const auto& request = handler->GetRequest();
        const auto request_stream = request ? request->GetStream() : nullptr;

        QUICR_LOGGER_INFO(logger_,
                          "Unsubscribe namespace conn_id: {} prefix_hash: {}",
                          request_stream ? request_stream->GetConnectionID() : 0,
                          th.track_namespace_hash);

        std::shared_ptr<Stream> stream;
        {
            std::lock_guard _(state_mutex_);
            if (const auto request = FindRequestForHandler(*handler)) {
                stream = request->GetStream();
                RemoveRequest(request);
            }
        }

        if (stream) {
            try {
                quic_transport_->CloseStream(stream, StreamOperation::kCancel);
            } catch (const std::exception& e) {
                QUICR_LOGGER_ERROR(logger_, "Failed to unsubscribe from namespace: {}", e.what());
            }
        }
        if (stream || handler->GetStatus() != SubscribeNamespaceHandler::Status::kNotSubscribed) {
            handler->SetStatus(SubscribeNamespaceHandler::Status::kNotSubscribed);
        }
    }

    bool Session::SendFetch(const std::shared_ptr<Stream>& stream,
                            uint64_t request_id,
                            const FullTrackName& tfn,
                            std::uint8_t priority,
                            std::optional<messages::GroupOrder> group_order,
                            const messages::Location& start_location,
                            const messages::FetchEndLocation& end_location)
    try {
        messages::Location wire_end_location = {
            .group = end_location.group,
            .object = end_location.object.has_value() ? *end_location.object + 1 : 0,
        };

        /* Available parameters:
         * - AUTHORIZATION TOKEN (0x03): Conveys authorization for the fetch request.
         * - SUBSCRIBER PRIORITY (0x20): Priority of the fetch response relative to other data.
         * - GROUP ORDER (0x22): Preference for the order of groups in the fetch response.
         */
        auto params = Parameters{}
                        .Add(ParameterType::kSubscriberPriority, priority)
                        .AddOptional(ParameterType::kGroupOrder, group_order);

        SendCtrlMsg(stream,
                    ControlMessageType::kFetch,
                    UintVar(request_id),
                    messages::FetchType::kStandalone,
                    tfn.name_space,
                    tfn.name,
                    start_location,
                    wire_end_location,
                    params);
        return true;
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending Fetch (error={})", e.what());
        return false;
    }

    void Session::SendJoiningFetch(const std::shared_ptr<Stream>& stream,
                                   uint64_t request_id,
                                   std::uint8_t priority,
                                   std::optional<messages::GroupOrder> group_order,
                                   uint64_t joining_request_id,
                                   std::uint64_t joining_start,
                                   bool absolute)
    try {
        /* Available parameters:
         * - AUTHORIZATION TOKEN (0x03): Conveys authorization for the fetch request.
         * - SUBSCRIBER PRIORITY (0x20): Priority of the fetch response relative to other data.
         * - GROUP ORDER (0x22): Preference for the order of groups in the fetch response.
         */
        auto params = Parameters{}
                        .Add(ParameterType::kSubscriberPriority, priority)
                        .AddOptional(ParameterType::kGroupOrder, group_order);

        SendCtrlMsg(stream,
                    ControlMessageType::kFetch,
                    UintVar(request_id),
                    absolute ? FetchType::kAbsoluteJoiningFetch : FetchType::kRelativeJoiningFetch,
                    joining_request_id,
                    joining_start,
                    params);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending JoiningFetch (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendFetchOk(const std::shared_ptr<Stream>& stream,
                              GroupOrder publisher_default_group_order,
                              bool end_of_track,
                              Location largest_location)
    try {
        /* Available parameters: None */
        auto params = Parameters{};

        auto extensions = TrackExtensions{}
                            .Add(ExtensionType::kDeliveryTimeout, 0)
                            .Add(ExtensionType::kMaxCacheDuration, 0)
                            .Add(ExtensionType::kDefaultPublisherGroupOrder, publisher_default_group_order)
                            .Add(ExtensionType::kDefaultPublisherPriority, 1)
                            .Add(ExtensionType::kDynamicGroups, true);

        SendCtrlMsg(stream, ControlMessageType::kFetchOk, end_of_track, largest_location, params, extensions);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending FetchOk (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::RequestTrackStatus(std::shared_ptr<TrackStatusHandler> handler)
    {
        if (handler == nullptr) {
            throw std::invalid_argument("Handler cannot be null");
        }

        if (handler->GetStatus() == TrackStatusHandler::Status::kPendingResponse) {
            throw std::logic_error("Track status handler already has a pending request");
        }

        const auto connection = GetDefaultConnection();
        if (!connection) {
            throw std::logic_error("Session has no default connection");
        }

        handler->RegisterSession(GetSharedPtr());

        const auto ftn = handler->GetFullTrackName();
        QUICR_LOGGER_INFO(logger_,
                          "Requesting track status conn_id: {} tfn: {} ({})",
                          connection->GetID(),
                          ftn.NamespaceStr(),
                          ftn.NameStr());
        handler->SetStatus(TrackStatusHandler::Status::kPendingResponse);

        const auto request = std::make_shared<Request>(Request{
          .request_id = GetNextRequestID(),
          .type = Request::Type::kTrackStatus,
          .stream = quic_transport_->CreateRequestStream(connection),
          .name = ftn,
          .handlers = {},
          .is_locally_initiated = true,
        });

        {
            std::lock_guard _(state_mutex_);
            AddRequest(request);
            BindHandlerToRequest(request, handler);
        }

        SendTrackStatus(request->GetStream(), request->request_id, ftn);
    }

    void Session::CancelTrackStatus(const std::shared_ptr<TrackStatusHandler>& handler)
    {
        if (!handler) {
            throw std::invalid_argument("Handler cannot be null");
        }

        handler->UnregisterSession(GetSharedPtr());

        if (handler->GetStatus() != TrackStatusHandler::Status::kPendingResponse) {
            return;
        }

        const auto& bound_request = handler->GetRequest();
        const auto request_id = bound_request ? std::optional{ bound_request->request_id } : std::nullopt;
        const auto request_stream = bound_request ? bound_request->GetStream() : nullptr;
        {
            std::lock_guard lock(state_mutex_);
            if (!request_id) {
                return;
            }

            const auto request = FindRequestForHandler(*handler);
            if (!request) {
                return;
            }

            RemoveRequest(request);
        }

        if (request_stream) {
            quic_transport_->CloseStream(request_stream, StreamOperation::kCancel);
        }

        handler->SetStatus(TrackStatusHandler::Status::kDoneByReset);
    }

    void Session::SubscribeTrack(std::shared_ptr<SubscribeTrackHandler> track_handler)
    {
        if (!track_handler) {
            throw std::invalid_argument("Handler cannot be null");
        }

        if (track_handler->IsPublisherInitiated()) {
            throw std::logic_error("Publisher-initiated tracks must be added by the PUBLISH callback");
        }

        const auto connection = GetDefaultConnection();
        if (!connection) {
            throw std::logic_error("Session has no default connection");
        }

        const auto& tfn = track_handler->GetFullTrackName();

        // Track hash is the track alias for now.
        auto th = TrackHash(tfn);

        auto proposed_track_alias = track_handler->GetTrackAlias();
        if (proposed_track_alias.has_value()) {
            th.track_fullname_hash = proposed_track_alias.value();
        }

        QUICR_LOGGER_INFO(
          logger_, "Subscribe track conn_id: {} track_alias: {}", connection->GetID(), th.track_fullname_hash);

        std::lock_guard<std::mutex> _(state_mutex_);

        track_handler->RegisterSession(GetSharedPtr());

        if (not proposed_track_alias.has_value()) {
            track_handler->SetTrackAlias(th.track_fullname_hash);
        }

        const auto request = std::make_shared<Request>(Request{
          .request_id = GetNextRequestID(),
          .type = Request::Type::kSubscribe,
          .stream = quic_transport_->CreateRequestStream(connection),
          .name = track_handler->GetFullTrackName(),
          .handlers = {},
          .is_locally_initiated = true,
        });

        AddRequest(request);
        BindHandlerToRequest(request, track_handler);

        SendSubscribe(request->GetStream(),
                      request->request_id,
                      tfn,
                      th,
                      track_handler->GetSubscriberPriority(),
                      track_handler->GetGroupOrder(),
                      track_handler->GetFilter(),
                      track_handler->GetDeliveryTimeout(),
                      track_handler->GetRendezvousTimeout());

        // Handle joining fetch, if requested.
        auto joining_fetch = track_handler->GetJoiningFetch();
        if (track_handler->GetJoiningFetch()) {
            const auto& info = *joining_fetch;
            // Make a joining fetch handler.
            const auto joining_fetch_handler = std::make_shared<JoiningFetchHandler>(
              track_handler, info.group_order.value_or(messages::GroupOrder::kAscending));

            joining_fetch_handler->RegisterSession(GetSharedPtr());

            const auto fetch_request = std::make_shared<Request>(Request{
              .request_id = GetNextRequestID(),
              .type = Request::Type::kFetch,
              .stream = quic_transport_->CreateRequestStream(connection),
              .name = joining_fetch_handler->GetFullTrackName(),
              .handlers = {},
              .is_locally_initiated = true,
            });

            AddRequest(fetch_request);
            BindHandlerToRequest(fetch_request, joining_fetch_handler);

            QUICR_LOGGER_INFO(logger_,
                              "Subscribe with joining fetch conn_id: {} track_alias: {} subscribe id: {} "
                              "joining subscribe id: {}",
                              connection->GetID(),
                              th.track_fullname_hash,
                              fetch_request->request_id,
                              request->request_id);

            SendJoiningFetch(fetch_request->GetStream(),
                             fetch_request->request_id,
                             info.priority,
                             info.group_order,
                             request->request_id,
                             info.joining_start,
                             info.absolute);
        }
    }

    void Session::UnsubscribeTrack(const std::shared_ptr<SubscribeTrackHandler>& track_handler)
    {
        track_handler->UnregisterSession(GetSharedPtr());

        const auto& tfn = track_handler->GetFullTrackName();
        auto th = TrackHash(tfn);
        const auto& request = track_handler->GetRequest();
        const auto request_stream = request ? request->GetStream() : nullptr;

        QUICR_LOGGER_INFO(logger_,
                          "Unsubscribe track conn_id: {} track_alias: {}",
                          request_stream ? request_stream->GetConnectionID() : 0,
                          th.track_fullname_hash);

        std::lock_guard<std::mutex> lock(state_mutex_);

        RemoveSubscribeTrack(*track_handler);
    }

    void Session::UpdateTrackSubscription(std::shared_ptr<SubscribeTrackHandler> track_handler)
    {
        if (!track_handler) {
            throw std::invalid_argument("Handler cannot be null");
        }

        if (track_handler->GetSession().lock() != GetSharedPtr()) {
            throw std::logic_error("Handler is not registered with this session");
        }

        const auto& tfn = track_handler->GetFullTrackName();
        auto th = TrackHash(tfn);
        const auto& request = track_handler->GetRequest();
        const auto logged_stream = request ? request->GetStream() : nullptr;

        QUICR_LOGGER_INFO(logger_,
                          "Subscribe track conn_id: {} hash: {}",
                          logged_stream ? logged_stream->GetConnectionID() : 0,
                          th.track_fullname_hash);

        std::lock_guard<std::mutex> _(state_mutex_);

        if (!request) {
            return;
        }

        auto priority = track_handler->GetSubscriberPriority();
        const auto request_stream = request->GetStream();
        if (request_stream == nullptr) {
            QUICR_LOGGER_ERROR(logger_, "Subscribe track update missing request stream conn_id: {}", 0);
            return;
        }

        SendRequestUpdate(request_stream, th, track_handler->pending_new_group_request_id_, priority, true);
    }

    void Session::RemoveSubscribeTrack(SubscribeTrackHandler& handler, bool remove_handler)
    {
        auto handler_status = handler.GetStatus();

        switch (handler_status) {
            case SubscribeTrackHandler::Status::kDoneByFin:
                [[fallthrough]];
            case SubscribeTrackHandler::Status::kDoneByReset:
                [[fallthrough]];
            case SubscribeTrackHandler::Status::kOk:
                try {
                    if (not handler.IsPublisherInitiated()) {
                        // TODO: Is it possible for these to not be sent at this point?
                        const auto& request = handler.GetRequest();
                        const auto request_stream = request ? request->GetStream() : nullptr;
                        quic_transport_->CloseStream(request_stream, StreamOperation::kCancel);
                    }
                } catch (const std::exception& e) {
                    QUICR_LOGGER_ERROR(logger_, "Failed to send unsubscribe: {}", e.what());
                }

                handler.SetStatus(SubscribeTrackHandler::Status::kNotSubscribed);
                break;

            default:
                break;
        }

        if (remove_handler) {
            const auto& request = handler.GetRequest();
            const auto request_stream = request ? request->GetStream() : nullptr;
            const auto connection = request_stream ? request_stream->GetConnection() : nullptr;
            if (const auto request = FindRequestForHandler(handler)) {
                RemoveRequest(request);
            }

            if (handler.GetReceivedTrackAlias().has_value()) {
                sub_by_recv_track_alias.erase(
                  { connection ? connection->GetID() : 0, handler.GetReceivedTrackAlias().value() });
            }
        }
    }

    void Session::CloseRequestHandler(std::uint64_t request_id,
                                      const std::shared_ptr<Stream>& stream,
                                      StreamClosedFlag flag)
    {
        std::vector<std::shared_ptr<TrackHandler>> handlers;
        bool is_pub_ns = false;
        bool is_sub_ns = false;
        TrackNamespace sub_ns_prefix;
        {
            std::lock_guard _(state_mutex_);

            // Incoming SUBSCRIBE_NAMESPACE and PUBLISH_NAMESPACE requests are not handler based.
            // Closing the bidirectional request stream ends the subscription.
            if (std::erase(recv_subscribe_namespaces, request_id) > 0) {
                if (const auto it = recv_req_id.find(request_id); it != recv_req_id.end()) {
                    sub_ns_prefix = it->second.track_full_name.name_space;
                }
                recv_req_id.erase(request_id);
                is_sub_ns = true;
            } else if (std::erase(recv_publish_namespaces, request_id) > 0) {
                recv_req_id.erase(request_id);
                is_pub_ns = true;
            } else {
                const auto request = FindRequest({ stream->GetConnectionID(), request_id });
                if (!request) {
                    QUICR_LOGGER_DEBUG(logger_,
                                       "Stream closed for unknown request_id conn_id: {} request_id: {}",
                                       stream->GetConnectionID(),
                                       request_id);
                    recv_req_id.erase(request_id);
                    return;
                }

                // Common request cleanup.
                handlers = request->handlers;
                RemoveRequest(request);

                // Request specific cleanup.
                for (const auto& handler : handlers) {
                    if (handler && handler->Get<PublishFetchHandler>()) {
                        pub_fetch_tracks_by_request_id.erase(request_id);
                    } else if (const auto sub_handler = handler ? handler->Get<SubscribeTrackHandler>() : nullptr) {
                        if (const auto alias = sub_handler->GetReceivedTrackAlias()) {
                            sub_by_recv_track_alias.erase({ stream->GetConnectionID(), *alias });
                        }
                    } else if (const auto pub_handler = handler ? handler->Get<PublishTrackHandler>() : nullptr) {
                        const auto th = TrackHash(pub_handler->GetFullTrackName());
                        if (auto alias_it = pub_tracks_by_track_alias.find(th.track_fullname_hash);
                            alias_it != pub_tracks_by_track_alias.end()) {
                            const auto connection_it = alias_it->second.find(stream->GetConnectionID());
                            if (connection_it != alias_it->second.end() && connection_it->second == pub_handler) {
                                alias_it->second.erase(connection_it);
                                if (alias_it->second.empty()) {
                                    pub_tracks_by_track_alias.erase(alias_it);
                                }
                            }
                        }
                        if (const auto pub_ns_it = pub_tracks_by_name.find(th.track_namespace_hash);
                            pub_ns_it != pub_tracks_by_name.end()) {
                            const auto pub_name_it = pub_ns_it->second.find(th.track_name_hash);
                            if (pub_name_it != pub_ns_it->second.end() && pub_name_it->second == pub_handler) {
                                pub_ns_it->second.erase(pub_name_it);
                            }
                            if (pub_ns_it->second.empty()) {
                                pub_tracks_by_name.erase(pub_ns_it);
                            }
                        }
                    }
                }

                recv_req_id.erase(request_id);
            }
        }

        // Notifications / callbacks.
        QUICR_LOGGER_INFO(logger_,
                          "Closing request handler conn_id: {} request_id: {} stream_id: {} closure: {}",
                          stream->GetConnectionID(),
                          request_id,
                          stream->GetStreamId(),
                          static_cast<int>(flag));

        // Standard bidir response: FIN answers FIN, and any abrupt closure is cancellation.
        const auto operation = flag == StreamClosedFlag::kFin ? StreamOperation::kFin : StreamOperation::kCancel;
        const bool is_reset = operation == StreamOperation::kCancel;

        if (is_sub_ns) {
            if (auto callbacks = std::dynamic_pointer_cast<ServerCallbacks>(callbacks_)) {
                callbacks->UnsubscribeNamespaceReceived(GetSharedPtr(), sub_ns_prefix);
            }
            return;
        }

        if (is_pub_ns) {
            // The publisher has closed their request stream (PUBLISH_NAMESPACE_DONE).
            quic_transport_->CloseStream(stream, operation);
            if (auto callbacks = std::dynamic_pointer_cast<ServerCallbacks>(callbacks_)) {
                callbacks->PublishNamespaceDoneReceived(GetSharedPtr(), request_id);
            }
            return;
        }

        if (handlers.empty()) {
            return;
        }

        quic_transport_->CloseStream(stream, operation);
        bool notify_publish_done = false;
        bool notify_unsubscribe = false;
        bool notify_fetch_cancel = false;
        for (const auto& handler : handlers) {
            if (const auto status_handler = handler->Get<TrackStatusHandler>()) {
                status_handler->SetStatus(is_reset ? TrackStatusHandler::Status::kDoneByReset
                                                   : TrackStatusHandler::Status::kDoneByFin);
            } else if (const auto fetch_handler = handler->Get<FetchTrackHandler>()) {
                // We must check FETCH before SUBSCRIBE as it inherits.
                fetch_handler->SetStatus(is_reset ? FetchTrackHandler::Status::kDoneByReset
                                                  : FetchTrackHandler::Status::kDoneByFin);
            } else if (const auto sub_handler = handler->Get<SubscribeTrackHandler>()) {
                sub_handler->SetStatus(is_reset ? SubscribeTrackHandler::Status::kDoneByReset
                                                : SubscribeTrackHandler::Status::kDoneByFin);
                sub_handler->SetStatus(SubscribeTrackHandler::Status::kNotSubscribed);
                notify_publish_done = true;
            } else if (const auto pub_fetch_handler = handler->Get<PublishFetchHandler>()) {
                // We must check FETCH before PUBLISH as it inherits.
                pub_fetch_handler->SetStatus(is_reset ? PublishFetchHandler::Status::kUnsubscribed
                                                      : PublishFetchHandler::Status::kDoneByFin);
                {
                    std::lock_guard lock(pub_fetch_handler->binding_operation_mutex_);
                    pub_fetch_handler->EndFetch(!is_reset);
                }
                notify_fetch_cancel = is_reset;
            } else if (const auto pub_handler = handler->Get<PublishTrackHandler>()) {
                pub_handler->SetStatus(is_reset ? PublishTrackHandler::Status::kUnsubscribed
                                                : PublishTrackHandler::Status::kDoneByFin);
                pub_handler->EndAllSubgroups(false);
                notify_unsubscribe = true;
            } else if (const auto ns_handler = handler->Get<SubscribeNamespaceHandler>()) {
                if (ns_handler->GetStatus() == SubscribeNamespaceHandler::Status::kOk) {
                    ns_handler->SetStatus(SubscribeNamespaceHandler::Status::kNotSubscribed);
                }
            } else if (const auto pub_ns_handler = handler->Get<PublishNamespaceHandler>()) {
                pub_ns_handler->SetStatus(PublishNamespaceHandler::Status::kNotPublished);
            }
        }

        if (const auto callbacks = std::dynamic_pointer_cast<ServerCallbacks>(callbacks_)) {
            if (notify_publish_done) {
                callbacks->PublishDoneReceived(GetSharedPtr(), request_id);
            }
            if (notify_unsubscribe) {
                callbacks->UnsubscribeReceived(GetSharedPtr(), request_id);
            }
        }
        if (notify_fetch_cancel && callbacks_) {
            callbacks_->FetchCancelReceived(GetSharedPtr(), request_id);
        }
    }

    void Session::UnpublishTrack(const std::shared_ptr<PublishTrackHandler>& track_handler)
    {
        defer({ track_handler->UnregisterSession(GetSharedPtr()); });

        // Generate track alias
        auto tfn = track_handler->GetFullTrackName();
        auto th = TrackHash(tfn);
        const auto& request = track_handler->GetRequest();
        const auto request_stream = request ? request->GetStream() : nullptr;

        QUICR_LOGGER_INFO(logger_,
                          "Unpublish track conn_id: {} hash: {}",
                          request_stream ? request_stream->GetConnectionID() : 0,
                          th.track_fullname_hash);

        std::shared_ptr<PublishTrackHandler> removed;
        std::optional<PublishTrackHandler::Status> changed_status;
        std::shared_ptr<Request> removed_request;
        {
            std::lock_guard state_lock(state_mutex_);
            removed_request = FindRequestForHandler(*track_handler);
            if (!removed_request) {
                return;
            }

            RemoveRequest(removed_request);
        }

        {
            std::lock_guard binding_lock(track_handler->binding_operation_mutex_);
            // Remaining open subgroups have not been declared complete.
            track_handler->EndAllSubgroupsInternal(false);

            std::lock_guard state_lock(state_mutex_);
            const auto connection_id = removed_request->GetStream()->GetConnectionID();
            if (auto alias_it = pub_tracks_by_track_alias.find(th.track_fullname_hash);
                alias_it != pub_tracks_by_track_alias.end()) {
                const auto connection_it = alias_it->second.find(connection_id);
                if (connection_it != alias_it->second.end() && connection_it->second == track_handler) {
                    alias_it->second.erase(connection_it);
                    if (alias_it->second.empty()) {
                        pub_tracks_by_track_alias.erase(alias_it);
                    }
                }
            }

            /*
             * This is a round about way to send subscribe done because of the announce flow. This
             * will go away if we stop using the announce flow. For now, it works for both announce
             * and publish flows.
             */
            auto pub_ns_it = pub_tracks_by_name.find(th.track_namespace_hash);
            if (pub_ns_it == pub_tracks_by_name.end()) {
                return;
            }

            auto pub_n_it = pub_ns_it->second.find(th.track_name_hash);
            if (pub_n_it != pub_ns_it->second.end() && pub_n_it->second == track_handler) {
                removed = std::move(pub_n_it->second);
                pub_ns_it->second.erase(pub_n_it);

                const auto ctrl_stream = removed_request->GetStream();

                // Send subscribe done if track has subscriber and is sending
                if (removed->GetStatus() == PublishTrackHandler::Status::kOk && ctrl_stream != nullptr) {
                    QUICR_LOGGER_INFO(logger_,
                                      "Unpublish track namespace hash: {} track_name_hash: {} track_alias: {}, sending "
                                      "publish_done",
                                      th.track_namespace_hash,
                                      th.track_name_hash,
                                      th.track_fullname_hash);
                    SendPublishDone(ctrl_stream,
                                    removed_request->request_id,
                                    PublishDoneStatusCode::kSubscriptionEnded,
                                    removed->publish_track_metrics_.opened_streams,
                                    "Unpublish track");
                } else {
                    QUICR_LOGGER_INFO(logger_,
                                      "Unpublish track namespace hash: {} track_name_hash: {} track_alias: {}",
                                      th.track_namespace_hash,
                                      th.track_name_hash,
                                      th.track_fullname_hash);
                }
                changed_status = removed->UpdateStatus(PublishTrackHandler::Status::kNotAnnounced);
            }
        }

        if (changed_status) {
            removed->StatusChanged(*changed_status);
        }
    }

    void Session::PublishTrack(std::shared_ptr<PublishTrackHandler> track_handler)
    {
        const auto connection = GetDefaultConnection();
        if (!connection) {
            throw std::logic_error("Session has no default connection");
        }

        if (!track_handler) {
            throw std::invalid_argument("Handler cannot be null");
        }

        track_handler->RegisterSession(GetSharedPtr());

        const auto tfn = track_handler->GetFullTrackName();
        auto th = TrackHash(tfn);
        QUICR_LOGGER_INFO(logger_,
                          "Publish track conn_id: {} hash: {} tfn: {} ({})",
                          connection->GetID(),
                          th.track_fullname_hash,
                          tfn.NamespaceStr(),
                          tfn.NameStr());

        const auto request = std::make_shared<Request>(Request{
          .request_id = GetNextRequestID(),
          .type = Request::Type::kPublish,
          .stream = quic_transport_->CreateRequestStream(connection),
          .name = tfn,
          .handlers = {},
          .is_locally_initiated = true,
        });

        std::lock_guard _(state_mutex_);

        if (!track_handler->GetTrackAlias().has_value()) {
            track_handler->SetTrackAlias(th.track_fullname_hash);
        }

        // Add state to received request ID since a subscribe will not be received for this request
        recv_req_id[request->request_id] = { track_handler->GetFullTrackName(), th };

        AddRequest(request);
        BindHandlerToRequest(request, track_handler);

        track_handler->SetStatus(PublishTrackHandler::Status::kPendingPublishOk);

        const PublishAttributes publish{ .track_full_name = { tfn },
                                         .track_alias = track_handler->GetTrackAlias().value(),
                                         .auth_tokens = {},
                                         .expires = 0, // TODO: Expires?
                                         .largest_object =
                                           std::make_optional(Location{ track_handler->largest_location_.group,
                                                                        track_handler->largest_location_.object }),
                                         .forward = true,
                                         .default_publisher_group_order = GroupOrder::kAscending,
                                         .dynamic_groups = track_handler->support_new_group_request_,
                                         .publisher_priority = track_handler->GetPublisherPriority(),
                                         .max_cache_duration = std::nullopt,
                                         .delivery_timeout = track_handler->GetDefaultTTL(),
                                         .track_properties = {} };

        QUICR_LOGGER_INFO(logger_,
                          "Publish track connId {}, track namespace hash: {}, name hash: {}",
                          connection->GetID(),
                          th.track_namespace_hash,
                          th.track_name_hash);

        // Hold ref to track handler
        pub_tracks_by_name[th.track_namespace_hash][th.track_name_hash] = track_handler;
        pub_tracks_by_track_alias[th.track_fullname_hash][connection->GetID()] = track_handler;

        SendPublish(request->GetStream(), request->request_id, publish);
    }

    void Session::PublishNamespace(std::shared_ptr<PublishNamespaceHandler> ns_handler, bool passive)
    {
        const auto connection = GetDefaultConnection();
        if (!connection) {
            throw std::logic_error("Session has no default connection");
        }

        if (!ns_handler) {
            throw std::invalid_argument("Handler cannot be null");
        }

        ns_handler->RegisterSession(GetSharedPtr());

        auto prefix_hash = hash(ns_handler->GetPrefix());
        QUICR_LOGGER_INFO(logger_, "Publish namespace conn_id: {} hash: {}", connection->GetID(), prefix_hash);

        if (!passive) {
            QUICR_LOGGER_INFO(logger_, "Publishing to namespace hash: {} sending ANNOUNCE message", prefix_hash);

            ns_handler->SetStatus(PublishNamespaceHandler::Status::kPendingResponse);

            const auto request = std::make_shared<Request>(Request{
              .request_id = GetNextRequestID(),
              .type = Request::Type::kPublishNamespace,
              .stream = quic_transport_->CreateRequestStream(connection),
              .name = ns_handler->GetFullTrackName(),
              .handlers = {},
              .is_locally_initiated = true,
            });

            {
                std::lock_guard lock(state_mutex_);

                AddRequest(request);
                BindHandlerToRequest(request, ns_handler);
            }

            SendPublishNamespace(request->GetStream(), request->request_id, ns_handler->GetPrefix());
        } else {
            ns_handler->SetStatus(PublishNamespaceHandler::Status::kOk);
        }
    }

    void Session::PublishNamespaceDone(const std::shared_ptr<PublishNamespaceHandler>& track_handler)
    {
        track_handler->UnregisterSession(GetSharedPtr());

        const auto& prefix = track_handler->GetPrefix();
        const auto prefix_hash = hash(prefix);
        const auto& request = track_handler->GetRequest();
        const auto request_stream = request ? request->GetStream() : nullptr;

        QUICR_LOGGER_INFO(logger_,
                          "PublishNamespaceDone (conn_id={}, prefix_hash={})",
                          request_stream ? request_stream->GetConnectionID() : 0,
                          prefix_hash);

        std::lock_guard<std::mutex> lock(state_mutex_);

        if (request_stream == nullptr) {
            QUICR_LOGGER_ERROR(
              logger_, "PublishNamespaceDone missing request context conn_id: {} prefix_hash: {}", 0, prefix_hash);
            return;
        }

        quic_transport_->CloseStream(request_stream, StreamOperation::kCancel);
        if (const auto request = FindRequestForHandler(*track_handler)) {
            RemoveRequest(request);
        }
    }

    void Session::FetchTrack(std::shared_ptr<FetchTrackHandler> track_handler)
    {
        const auto connection = GetDefaultConnection();
        if (!connection) {
            throw std::logic_error("Session has no default connection");
        }

        if (!track_handler) {
            throw std::invalid_argument("Handler cannot be null");
        }

        track_handler->RegisterSession(GetSharedPtr());

        const auto& tfn = track_handler->GetFullTrackName();
        auto th = TrackHash(tfn);

        track_handler->SetTrackAlias(th.track_fullname_hash);

        QUICR_LOGGER_INFO(logger_, "Fetch track conn_id: {} hash: {}", connection->GetID(), th.track_fullname_hash);

        const auto request = std::make_shared<Request>(Request{
          .request_id = GetNextRequestID(),
          .type = Request::Type::kFetch,
          .stream = quic_transport_->CreateRequestStream(connection),
          .name = tfn,
          .handlers = {},
          .is_locally_initiated = true,
        });

        std::lock_guard<std::mutex> _(state_mutex_);

        QUICR_LOGGER_DEBUG(logger_, "subscribe id (from fetch) to add to memory: {}", request->request_id);

        auto priority = track_handler->GetSubscriberPriority();
        auto group_order = track_handler->GetGroupOrder();
        auto start_location = track_handler->GetStartLocation();
        auto end_location = track_handler->GetEndLocation();

        track_handler->SetStatus(FetchTrackHandler::Status::kPendingResponse);

        AddRequest(request);
        BindHandlerToRequest(request, track_handler);

        SendFetch(request->GetStream(), request->request_id, tfn, priority, group_order, start_location, end_location);
    }

    void Session::CancelFetchTrack(std::shared_ptr<FetchTrackHandler> track_handler)
    {
        track_handler->UnregisterSession(GetSharedPtr());

        std::lock_guard<std::mutex> _(state_mutex_);

        const auto& request = track_handler->GetRequest();
        if (!request) {
            return;
        }

        if (const auto request = FindRequestForHandler(*track_handler)) {
            RemoveRequest(request);
        }

        if (track_handler->GetStatus() == FetchTrackHandler::Status::kDoneByFin ||
            track_handler->GetStatus() == FetchTrackHandler::Status::kDoneByReset) {
            return;
        }

        // TODO: Cancel using QUIC here when FETCH migrated.
        track_handler->SetStatus(FetchTrackHandler::Status::kNotConnected);
    }

    std::shared_ptr<PublishTrackHandler> Session::GetPubTrackHandler(TrackHash& th)
    {
        auto pub_ns_it = pub_tracks_by_name.find(th.track_namespace_hash);
        if (pub_ns_it == pub_tracks_by_name.end()) {
            return nullptr;
        }

        auto pub_n_it = pub_ns_it->second.find(th.track_name_hash);
        if (pub_n_it == pub_ns_it->second.end()) {
            return nullptr;
        }

        return pub_n_it->second;
    }

    void Session::RemoveTracksForConnectionClose(const std::shared_ptr<Connection>& connection)
    {
        std::set<std::shared_ptr<TrackHandler>> removed_handlers;
        const auto connection_id = connection->GetID();

        {
            std::lock_guard lock(state_mutex_);

            std::set<std::uint64_t> migrating_requests;
            for (const auto& [_, migration] : migrations_) {
                if (migration.source_connection == connection) {
                    for (const auto& pending : migration.pending) {
                        migrating_requests.insert(pending.first);
                    }
                }
            }

            const auto remove_handler = [&](const std::shared_ptr<TrackHandler>& handler) {
                if (!handler) {
                    return false;
                }

                const auto request = handler->GetRequest();
                const auto request_stream = request ? request->GetStream() : nullptr;
                if (!request_stream || request_stream->GetConnection() != connection) {
                    return false;
                }

                if (migrating_requests.contains(request->request_id)) {
                    return false;
                }
                removed_handlers.insert(handler);
                return true;
            };
            std::erase_if(requests, [&](const auto& item) {
                if (item.first.first != connection_id || migrating_requests.contains(item.first.second)) {
                    return false;
                }

                for (const auto& handler : item.second->handlers) {
                    remove_handler(handler);
                }
                const auto request_stream = item.second->GetStream();
                if (request_stream && request_stream->request_.lock() == item.second) {
                    request_stream->request_.reset();
                }

                return true;
            });

            std::erase_if(sub_by_recv_track_alias,
                          [connection_id](const auto& item) { return item.first.first == connection_id; });
            std::erase_if(request_by_stream,
                          [connection_id](const auto& item) { return item.first.first == connection_id; });

            const std::size_t removed_requests = std::erase_if(recv_req_id, [&](const auto& item) {
                return item.second.stream && item.second.stream->GetConnection() == connection;
            });
            if (removed_requests > 0) {
                std::erase_if(recv_publish_namespaces,
                              [&](const auto request_id) { return !recv_req_id.contains(request_id); });
                std::erase_if(recv_subscribe_namespaces,
                              [&](const auto request_id) { return !recv_req_id.contains(request_id); });
            }

            const auto remove_indexed_handlers = [&](auto& indexes) {
                std::erase_if(indexes, [&](auto& index) {
                    std::erase_if(index.second, [&](const auto& item) { return remove_handler(item.second); });
                    return index.second.empty();
                });
            };
            remove_indexed_handlers(pub_tracks_by_name);
            remove_indexed_handlers(pub_tracks_by_track_alias);
            std::erase_if(pub_fetch_tracks_by_request_id,
                          [&](const auto& item) { return remove_handler(item.second); });

            for (const auto& handler : removed_handlers) {
                if (handler->GetRequest()) {
                    handler->UnbindRequest();
                }
            }
        }

        for (const auto& handler : removed_handlers) {
            if (const auto h = handler->Get<SubscribeTrackHandler>()) {
                h->SetStatus(SubscribeTrackHandler::Status::kNotConnected);
            } else if (const auto h = handler->Get<PublishTrackHandler>()) {
                h->SetStatus(PublishTrackHandler::Status::kNotConnected);
            } else if (const auto h = handler->Get<TrackStatusHandler>()) {
                h->SetStatus(TrackStatusHandler::Status::kNotConnected);
            } else if (const auto h = handler->Get<SubscribeNamespaceHandler>()) {
                h->SetStatus(SubscribeNamespaceHandler::Status::kNotSubscribed);
            } else if (const auto h = handler->Get<PublishNamespaceHandler>()) {
                h->SetStatus(PublishNamespaceHandler::Status::kNotConnected);
            }

            handler->UnregisterSession(GetSharedPtr());
        }
    }

    // ---------------------------------------------------------------------------------------
    // Transport handler callbacks
    // ---------------------------------------------------------------------------------------

    void Session::OnConnectionStatus(const std::shared_ptr<Connection>& connection, Connection::Status status)
    {
        if (!connection) {
            return;
        }
        const auto connection_id = connection->GetID();

        QUICR_LOGGER_DEBUG(
          logger_, "Connection status conn_id: {} status: {}", connection_id, static_cast<int>(status));
        bool remove_connection = false;

        switch (status) {
            case Connection::Status::kReady: {
                if (connection == GetDefaultConnection()) {
                    SetStatus(Status::kPendingPeerSetup);
                }
                QUICR_LOGGER_INFO(logger_, "Connection established, creating unidir control stream and sending SETUP");
                connection->SetTxControlStream(quic_transport_->CreateControlStream(connection));
                std::string connect_uri = client_config_.connect_uri;
                {
                    std::lock_guard lock(state_mutex_);
                    if (const auto migration = FindMigration(*connection); migration != migrations_.end()) {
                        connect_uri = migration->second.redirect.uri;
                    }
                }
                SendSetup(connection, connect_uri);
                connection->RecordSetupSent();
                CheckReady(connection);
                break;
            }

            case Connection::Status::kConnecting:
                if (client_mode_ && connection == GetDefaultConnection()) {
                    SetStatus(Status::kConnecting);
                }
                break;
            case Connection::Status::kRemoteRequestClose:
                [[fallthrough]];
            case Connection::Status::kIdleTimeout:
                [[fallthrough]];
            case Connection::Status::kDisconnected: {
                remove_connection = true;
                break;
            }

            case Connection::Status::kShuttingDown:
                break;

            case Connection::Status::kShutdown:
                remove_connection = true;
                break;
        }

        if (remove_connection) {
            const bool closed_default = connection == GetDefaultConnection();
            std::vector<std::shared_ptr<Connection>> failed_sources;
            {
                std::lock_guard lock(state_mutex_);
                for (auto migration = migrations_.begin(); migration != migrations_.end();) {
                    if (!migration->second.candidate_connection ||
                        migration->second.candidate_connection->GetID() != connection_id) {
                        ++migration;
                        continue;
                    }

                    for (const auto& item : migration->second.pending) {
                        if (const auto& stream = item.second.request_stream) {
                            request_by_stream.erase({ connection_id, stream->GetStreamId() });
                        }
                    }

                    if (migration->second.source_connection->GetStatus() != Connection::Status::kReady) {
                        failed_sources.push_back(migration->second.source_connection);
                    }
                    migration = migrations_.erase(migration);
                }
            }

            for (const auto& failed_source : failed_sources) {
                RemoveTracksForConnectionClose(failed_source);
            }

            {
                std::lock_guard lock(connections_mutex_);
                connections_.erase(connection_id);
            }
            RemoveTracksForConnectionClose(connection);

            if (!closed_default) {
                return;
            }

            std::shared_ptr<Connection> replacement;
            {
                std::lock_guard lock(connections_mutex_);
                for (const auto& [_, candidate] : connections_) {
                    if (candidate->GetStatus() == Connection::Status::kReady && candidate->IsSetupComplete()) {
                        replacement = candidate;
                        break;
                    }
                }
            }

            {
                std::lock_guard lock(state_mutex_);
                default_connection_ = replacement;
            }
            if (!replacement) {
                SetStatus(status == Connection::Status::kShutdown ? Status::kNotReady : Status::kNotConnected);
            }
        }
    }

    void Session::SetStatus(Status status)
    {
        status_.store(status, std::memory_order_release);
        if (callbacks_) {
            if (auto self = weak_from_this().lock()) {
                callbacks_->StatusChanged(self, status);
            }
        }
    }

    bool Session::OnRecvStream(const std::shared_ptr<Stream>& stream)
    try {
        if (stream == nullptr) {
            return false;
        }
        const auto connection = stream->GetConnection();
        if (!connection) {
            return false;
        }
        const auto connection_id = connection->GetID();

        const bool is_request_stream = stream->IsBidirectional();
        if (!is_request_stream && stream->rx_is_new && !BindRecvStream(*stream)) {
            return false;
        }
        stream->rx_is_new = false;

        // Every bidirectional stream is a request, and the peer sends the session's control
        // messages on one unidirectional stream. Both carry control messages rather than data.
        const bool is_ctrl_stream = is_request_stream || stream->GetStreamId() == connection->GetRxControlStreamId();

        // What a stream carries and who consumes it do not change while it is read, so both are
        // settled for the whole turn rather than per message. Holding the handler also keeps it
        // alive across the turn, so a subgroup cannot be abandoned part way through.
        std::shared_ptr<SubscribeTrackHandler> handler;
        std::shared_ptr<ForwardingSubscribeTrackHandler> forwarder;
        bool reads_subgroups = false;
        if (!is_ctrl_stream) {
            handler = stream->rx_handler.lock();
            if (handler == nullptr) {
                QUICR_LOGGER_ERROR(
                  logger_, "Received data on existing stream_id: {} with no handler anymore", stream->GetStreamId());
                return false;
            }
            const auto request = handler->GetRequest();
            const auto request_stream = request ? request->GetStream() : nullptr;
            const auto handler_connection = request_stream ? request_stream->GetConnection() : nullptr;
            if (!handler_connection || handler_connection->GetID() != connection_id) {
                QUICR_LOGGER_DEBUG(logger_,
                                   "Ignoring data from retired connection conn_id: {} stream_id: {}",
                                   connection_id,
                                   stream->GetStreamId());
                return false;
            }

            // Binding set up parsing whichever header the stream carries, so that header's type
            // says which kind it is. Only a stream drained and reclaimed after closing has none.
            const auto stream_type = [&stream] {
                std::lock_guard _(stream->rx_mutex);
                return stream->rx_data.GetAnyType();
            }();

            reads_subgroups = stream_type == static_cast<std::uint64_t>(StreamMessageType::kSubgroupHeader);
            if (!reads_subgroups && stream_type != static_cast<std::uint64_t>(StreamMessageType::kFetchHeader)) {
                return false;
            }

            forwarder = std::dynamic_pointer_cast<ForwardingSubscribeTrackHandler>(handler);
            if (forwarder != nullptr && !reads_subgroups) {
                QUICR_LOGGER_WARN(logger_,
                                  "Fetch on stream_id: {} is for a handler that takes bytes, which fetch does not "
                                  "support, ignored",
                                  stream->GetStreamId());
                return false;
            }
        }

        // A stream passed on as bytes is taken whole, since it costs no more to take all of what
        // has arrived than part of it. Reading objects instead stops short of draining the stream,
        // so a burst on one does not starve the others, and says as much to earn another turn.
        const bool has_remaining = [&] {
            if (forwarder != nullptr) {
                return ForwardSubgroupBytes(*stream, *forwarder);
            }

            for (int i = 0; i < kReadLoopMax; i++) {
                const bool read = is_ctrl_stream    ? RecvCtrlMessage(connection, stream, is_request_stream)
                                  : reads_subgroups ? RecvSubgroupObject(*stream, *handler)
                                                    : RecvFetchObject(*stream, *handler);
                if (!read) {
                    return false;
                }
            }

            return stream->RxDataSize() != 0;
        }();

        return has_remaining;
    } catch (const TransportException& e) {
        QUICR_LOGGER_INFO(logger_, "OnRecvStream: connection or stream no longer exists (error={})", e.what());
        if (const auto failed_connection = stream->GetConnection()) {
            quic_transport_->Close(failed_connection, AppReasonForClose::kInternalError);
        }
        return false;
    } catch (const std::exception& e) {
        // A stream that cannot be made sense of will fail the same way on every later arrival, and
        // the transport's notifier would only swallow this, leaving the peer waiting on a session
        // that can no longer make progress.
        QUICR_LOGGER_ERROR(logger_, "Caught exception on receiving stream, closing connection. (error={})", e.what());
        if (const auto failed_connection = stream->GetConnection()) {
            failed_connection->metrics.invalid_ctrl_stream_msg++;
            quic_transport_->Close(failed_connection, AppReasonForClose::kProtocolViolation);
        }

        // TODO(tievens): Add metrics to track if this happens
        return false;
    }

    bool Session::BindRecvStream(Stream& stream)
    {
        const auto stream_id = stream.GetStreamId();
        const auto connection = stream.GetConnection();
        if (!connection) {
            return false;
        }

        // Peeked rather than consumed, so the header is still there for whoever the stream belongs
        // to. Both kinds of data stream name their destination right after the type.
        std::optional<std::uint64_t> stream_type;
        std::optional<std::uint64_t> destination;
        {
            std::lock_guard _(stream.rx_mutex);
            auto cursor = stream.rx_data.Data();

            stream_type = TryDecodeUintV(cursor);
            if (stream_type.has_value()) {
                destination = TryDecodeUintV(cursor);
            }
        }

        if (!stream_type.has_value()) {
            QUICR_LOGGER_DEBUG(logger_, "New stream {} has too few bytes to say what it carries yet", stream_id);
            return false;
        }

        QUICR_LOGGER_DEBUG(logger_,
                           "New stream conn_id: {} stream_id: {} msg_type: 0x{:02x} ({})",
                           connection->GetID(),
                           stream_id,
                           *stream_type,
                           *stream_type);

        // This might be incoming control stream arriving.
        if (static_cast<ControlMessageType>(*stream_type) == ControlMessageType::kSetup) {
            connection->SetRxControlStreamId(stream_id);
            return true;
        }

        StreamMessageType message_type;
        try {
            message_type = GetStreamMessageType(*stream_type);
        } catch (const ProtocolViolationException&) {
            QUICR_LOGGER_WARN(logger_,
                              "Received stream {} with invalid header type 0x{:02x}, disconnecting session",
                              stream_id,
                              *stream_type);
            connection->metrics.rx_stream_invalid_type++;

            throw;
        }

        if (!destination.has_value()) {
            return false; // Need more bytes, will try again.
        }

        // TODO: A stream nothing is waiting for is held until it closes or the connection ends.
        // Bounding that by unknown_stream_expiry_ms was never implemented.
        return message_type == StreamMessageType::kSubgroupHeader ? OnRecvSubgroup(*destination, stream)
                                                                  : OnRecvFetch(*destination, stream);
    }

    bool Session::RecvCtrlMessage(const std::shared_ptr<Connection>& connection,
                                  const std::shared_ptr<Stream>& stream,
                                  const bool is_request_stream)
    {
        // A control message sits among the raw bytes, which move as more arrive, so it is copied
        // out while the buffer is locked and handled once it is not: the network thread filling the
        // buffer must not be left waiting on that.
        ControlMessageType msg_type;
        Bytes payload;
        {
            std::lock_guard _(stream->rx_mutex);
            auto& buffer = stream->rx_data;

            if (buffer.Empty()) {
                return false;
            }

            const auto message_view = buffer.Data();
            auto cursor = message_view;

            const auto decoded_type = TryDecodeUintV(cursor);
            if (!decoded_type.has_value()) {
                return false;
            }
            msg_type = static_cast<ControlMessageType>(*decoded_type);

            std::uint16_t payload_len;
            if (cursor.size() < sizeof(payload_len)) {
                return false;
            }
            std::memcpy(&payload_len, cursor.data(), sizeof(payload_len));
            payload_len = SwapBytes(payload_len);
            cursor = cursor.subspan(sizeof(payload_len));

            if (cursor.size() < payload_len) {
                return false;
            }
            payload.assign(cursor.begin(), cursor.begin() + payload_len);

            buffer.Pop(message_view.size() - cursor.size() + payload_len);
        }

        QUICR_LOGGER_DEBUG(logger_,
                           "Transport:ControlMessageReceived conn_id: {} stream_id: {} msg_type: 0x{:02x} size: {}",
                           connection->GetID(),
                           stream->GetStreamId(),
                           static_cast<std::uint64_t>(msg_type),
                           payload.size());

        bool processed = false;
        try {
            processed = is_request_stream ? ProcessRequestMessage(connection, stream, msg_type, payload)
                                          : ProcessCtrlMessage(connection, stream, msg_type, payload);
        } catch (const std::exception& e) {
            QUICR_LOGGER_ERROR(logger_,
                               "Caught exception trying to process control message. (type={}, error={})",
                               static_cast<int>(msg_type),
                               e.what());
            throw ProtocolViolationException(e.what());
        } catch (...) {
            QUICR_LOGGER_ERROR(logger_, "Unable to parse control message");
            throw ProtocolViolationException("Control message cannot be parsed");
        }

        if (!processed) {
            connection->metrics.invalid_ctrl_stream_msg++;
        }

        return true;
    }

    bool Session::RecvSubgroupObject(Stream& stream, SubscribeTrackHandler& handler)
    {
        // An object is taken out of the buffer while it is locked and handed over once it is not:
        // the network thread filling the buffer must not be left waiting on the handler. Its
        // payload is a vector of its own rather than a view of the raw bytes, which move as more
        // arrive, so taking it costs nothing.
        Bytes payload;
        ObjectHeaders headers{};
        std::optional<messages::StreamHeaderProperties> stream_mode;
        {
            std::lock_guard _(stream.rx_mutex);
            auto& buffer = stream.rx_data;

            // Resuming an already parsed header costs nothing, so it is checked per object rather
            // than tracked as another piece of receive state.
            auto& s_hdr = buffer.GetAny<messages::StreamHeaderSubGroup>();
            if (not(buffer >> s_hdr)) {
                return false; // Header has not fully arrived yet.
            }

            // TODO: This shouldn't override subscriber priority, but keeping existing behaviour.
            if (s_hdr.publisher_priority.has_value()) {
                handler.SetSubscriberPriority(*s_hdr.publisher_priority);
            }

            if (buffer.Empty()) {
                return false;
            }

            if (not buffer.AnyHasValueB()) {
                buffer.InitAnyB<messages::StreamSubGroupObject>();
            }

            auto& partial = buffer.GetAnyB<messages::StreamSubGroupObject>();
            partial.properties.emplace(*s_hdr.properties);
            if (not(buffer >> partial)) {
                return false; // Object has not fully arrived yet.
            }

            // Objects carry only the step from the one before, counted from the start of each
            // subgroup. The first also reports how the subgroup is framed.
            auto& next_object_id = stream.rx_parse.next_object_id;
            const auto carried = stream.rx_parse.subgroup;
            const bool same_subgroup = next_object_id.has_value() && carried.has_value() &&
                                       carried->group_id == s_hdr.group_id && carried->subgroup_id == s_hdr.subgroup_id;
            if (!next_object_id.has_value()) {
                stream_mode.emplace(*s_hdr.properties);
            }
            next_object_id = same_subgroup ? *next_object_id + partial.object_delta : partial.object_delta;

            if (!s_hdr.subgroup_id.has_value()) {
                if (partial.properties->subgroup_id_mode != messages::SubgroupIdType::kSetFromFirstObject) {
                    throw messages::ProtocolViolationException("Subgoup ID mismatch");
                }

                s_hdr.subgroup_id = next_object_id;
            }

            stream.rx_parse.subgroup = { s_hdr.group_id, *s_hdr.subgroup_id };

            payload = std::move(partial.payload);
            headers = { s_hdr.group_id,
                        *next_object_id,
                        *s_hdr.subgroup_id,
                        payload.size(),
                        partial.object_status,
                        s_hdr.publisher_priority,
                        std::nullopt,
                        TrackMode::kStream,
                        std::move(partial.extensions),
                        std::move(partial.immutable_extensions) };

            buffer.ResetAnyB();
            *next_object_id += 1;
        }

        handler.subscribe_track_metrics_.objects_received++;
        handler.ObjectReceived(headers, payload, std::move(stream_mode));
        {
            std::lock_guard lock(handler.delivered_location_mutex_);
            handler.latest_delivered_location_ = messages::Location{
                .group = headers.group_id,
                .object = headers.object_id,
            };
        }

        return true;
    }

    bool Session::ForwardSubgroupBytes(Stream& stream, ForwardingSubscribeTrackHandler& handler)
    {
        // Taken out of the buffer while it is locked and handed over once it is not, so that the
        // network thread filling it is not left waiting on the handler. Taking the whole of what
        // has arrived costs nothing, since the bytes move out rather than being copied.
        Bytes data;

        // What the header said, set only on the turn the subgroup starts and kept back until the
        // lock is released along with the bytes.
        std::optional<messages::StreamHeaderProperties> start_properties;
        std::optional<std::uint8_t> start_priority;
        Stream::RxParseState::Subgroup subgroup{};
        {
            std::lock_guard _(stream.rx_mutex);
            auto& buffer = stream.rx_data;

            auto& s_hdr = buffer.GetAny<messages::StreamHeaderSubGroup>();
            if (not(buffer >> s_hdr)) {
                return false; // Header has not fully arrived yet.
            }

            if (not stream.rx_parse.subgroup.has_value()) {
                /*
                 * Framing that takes the subgroup ID from the first object leaves it out of the
                 * header, so the one varint that object opens with is read to find it. Nothing
                 * else of the object is, and it stays where it is to be passed on with the rest.
                 */
                auto subgroup_id = s_hdr.subgroup_id;
                if (not subgroup_id.has_value()) {
                    auto cursor = buffer.Data();
                    subgroup_id = TryDecodeUintV(cursor);
                    if (not subgroup_id.has_value()) {
                        return false; // The first object has yet to say which subgroup this is.
                    }
                }

                stream.rx_parse.subgroup = { s_hdr.group_id, *subgroup_id };
                start_properties.emplace(*s_hdr.properties);
                start_priority = s_hdr.publisher_priority;

                // TODO: This shouldn't override subscriber priority, but keeping existing behaviour.
                if (s_hdr.publisher_priority.has_value()) {
                    handler.SetSubscriberPriority(*s_hdr.publisher_priority);
                }
            }

            subgroup = *stream.rx_parse.subgroup;
            data = buffer.TakeAll();
        }

        if (start_properties.has_value()) {
            handler.SubgroupStarted(subgroup.group_id, subgroup.subgroup_id, start_priority, *start_properties);
        }

        if (!data.empty()) {
            handler.StreamBytesForwarded(subgroup.group_id, subgroup.subgroup_id, std::move(data));
        }

        return false; // Everything that had arrived has been taken.
    }

    bool Session::RecvFetchObject(Stream& stream, SubscribeTrackHandler& handler)
    {
        // Resolving an object against the deltas before it takes it out of the buffer, so the
        // handler is called once that is unlocked, rather than leaving the network thread filling
        // it waiting.
        std::optional<messages::ResolvedFetchObject> resolved;
        {
            std::lock_guard _(stream.rx_mutex);
            auto& buffer = stream.rx_data;

            // Resuming an already parsed header costs nothing, so it is checked per object rather
            // than tracked as another piece of receive state.
            auto& f_hdr = buffer.GetAny<messages::FetchHeader>();
            if (not(buffer >> f_hdr)) {
                return false; // Header has not fully arrived yet.
            }

            if (buffer.Empty()) {
                return false;
            }

            if (not buffer.AnyHasValueB()) {
                buffer.InitAnyB<messages::FetchObject>();
            }

            auto& obj = buffer.GetAnyB<messages::FetchObject>();
            if (not(buffer >> obj)) {
                return false; // Object has not fully arrived yet.
            }

            resolved = stream.rx_parse.fetch->Decode(std::move(obj));
            buffer.ResetAnyB();
        }

        // An object the peer reports as non-existent leaves nothing to hand over, but the stream
        // has been read and can be read on.
        if (!resolved.has_value()) {
            // TODO: We're being told this object doesn't exist, should we notify?
            return true;
        }

        handler.subscribe_track_metrics_.objects_received++;
        handler.subscribe_track_metrics_.bytes_received += resolved->payload.size();

        handler.ObjectReceived(resolved->headers, resolved->payload);
        {
            std::lock_guard lock(handler.delivered_location_mutex_);
            handler.latest_delivered_location_ = messages::Location{
                .group = resolved->headers.group_id,
                .object = resolved->headers.object_id,
            };
        }

        return true;
    }

    void Session::OnStreamClosed(const std::shared_ptr<Stream>& stream, StreamClosedFlag flag)
    {
        if (stream == nullptr) {
            return;
        }
        const auto connection = stream->GetConnection();
        if (!connection) {
            return;
        }
        const auto connection_id = connection->GetID();

        const auto stream_id = stream->GetStreamId();

        QUICR_LOGGER_DEBUG(logger_, "Stream {} closed", stream_id);

        if (callbacks_ != nullptr) {
            callbacks_->OnStreamClosed(stream_id, flag);
        }

        try {
            std::optional<std::uint64_t> request_id;
            bool awaiting_migration = false;
            bool failed_migration = false;
            {
                std::lock_guard _(state_mutex_);
                const auto req_it = request_by_stream.find({ connection_id, stream_id });
                if (req_it != request_by_stream.end() && req_it->second.is_request_stream) {
                    request_id = req_it->second.request_id;
                    request_by_stream.erase(req_it);

                    const auto migration = FindMigration(*connection);
                    if (migration != migrations_.end()) {
                        const auto pending = migration->second.pending.find(*request_id);
                        failed_migration =
                          pending != migration->second.pending.end() && pending->second.request_stream == stream;
                    }

                    const auto handler = GetRequestHandler(FindRequest({ connection_id, *request_id }));
                    const bool handler_uses_connection = handler != nullptr;

                    awaiting_migration =
                      handler_uses_connection && std::ranges::any_of(migrations_, [&](const auto& item) {
                          return item.second.source_connection == connection &&
                                 MatchesRedirect(*handler, item.second.redirect);
                      });
                }
            }

            // If this was a request stream, the request is over.
            if (request_id.has_value()) {
                if (failed_migration) {
                    FailMigration(connection, *request_id, false);
                    return;
                }

                if (awaiting_migration) {
                    QUICR_LOGGER_DEBUG(logger_,
                                       "Keeping request handler during migration conn_id: {} request_id: {}",
                                       connection_id,
                                       *request_id);
                    return;
                }
                CloseRequestHandler(*request_id, stream, flag);
                return;
            }
            // If this was a data stream, notify the handler.
            if (const auto publish_handler = stream->tx_handler.lock()) {
                const auto request = publish_handler->GetRequest();
                const auto request_stream = request ? request->GetStream() : nullptr;
                if (flag == StreamClosedFlag::kStopSending && request_stream &&
                    request_stream->GetConnection() == connection) {
                    publish_handler->StreamStopped(stream);
                }
                return;
            }
        } catch (const std::exception& e) {
            QUICR_LOGGER_ERROR(logger_, "Caught exception on stream closed: {}", e.what());
            return;
        }

        // TODO: Replace this check with control stream IDs check.
        if (stream->IsBidirectional()) {
            const auto tx_ctrl_stream = connection->GetTxControlStream();
            switch (flag) {
                case StreamClosedFlag::kFin:
                    if (tx_ctrl_stream != nullptr && tx_ctrl_stream->GetStreamId() == stream_id) {
                        throw ProtocolViolationException("Primary control stream FIN");
                    }
                    break;
                case StreamClosedFlag::kReset:
                    if (tx_ctrl_stream != nullptr && tx_ctrl_stream->GetStreamId() == stream_id) {
                        throw ProtocolViolationException("Primary control stream RESET");
                    }
                    break;
                case StreamClosedFlag::kStopSending:
                    break;
            }

            return;
        }

        const auto handler_ptr = stream->rx_handler.lock();
        if (handler_ptr == nullptr) {
            QUICR_LOGGER_WARN(logger_, "Received stream closed for unknown handler");
            return;
        }

        const auto request = handler_ptr->GetRequest();
        const auto request_stream = request ? request->GetStream() : nullptr;
        if (!request_stream || request_stream->GetConnection() != connection) {
            return;
        }

        const bool reset = flag == StreamClosedFlag::kReset;

        try {
            if (std::dynamic_pointer_cast<FetchTrackHandler>(handler_ptr) != nullptr &&
                flag != StreamClosedFlag::kStopSending) {
                handler_ptr->SetStatus(reset ? FetchTrackHandler::Status::kDoneByReset
                                             : FetchTrackHandler::Status::kDoneByFin);
            }

            /*
             * A stream carries a single subgroup, so the stream closing is that subgroup ending.
             * Only a subgroup stream ever records which one it carries, and only once the handler
             * has been told of it, so this says nothing about a fetch or about a stream that
             * closed before anything reached its handler.
             */
            if (const auto& subgroup = stream->rx_parse.subgroup) {
                handler_ptr->SubgroupEnded(subgroup->group_id, subgroup->subgroup_id, reset);
            }
        } catch (const ProtocolViolationException& e) {
            QUICR_LOGGER_ERROR(logger_, "Protocol violation on stream data recv: {}", e.reason);
            throw ProtocolViolationException(e.reason);
        } catch (const std::exception& e) {
            QUICR_LOGGER_ERROR(logger_, "Caught exception on stream data recv: {}", e.what());
            throw std::runtime_error("Internal error");
        }
    }

    bool Session::OnRecvSubgroup(std::uint64_t track_alias, Stream& stream)
    {
        const auto stream_id = stream.GetStreamId();

        const auto sub_it = sub_by_recv_track_alias.find({ stream.GetConnectionID(), track_alias });
        if ((sub_it == sub_by_recv_track_alias.end() || sub_it->second == nullptr)) {
            if (const auto connection = stream.GetConnection()) {
                connection->metrics.rx_stream_unknown_track_alias++;
            }
            QUICR_LOGGER_WARN(
              logger_,
              "Received stream_header_subgroup to unknown subscribe track track_alias: {} stream: {}, ignored",
              track_alias,
              stream_id);

            return false;
        }

        stream.rx_handler = sub_it->second;

        // The header itself is still at the front of the stream's buffer. Setting up to parse it
        // there is also what records, for every later arrival, what this stream carries.
        std::lock_guard _(stream.rx_mutex);
        stream.rx_data.InitAny<messages::StreamHeaderSubGroup>(
          static_cast<std::uint64_t>(StreamMessageType::kSubgroupHeader));

        return true;
    }

    bool Session::OnRecvFetch(std::uint64_t request_id, Stream& stream)
    {
        const auto stream_id = stream.GetStreamId();

        std::shared_ptr<TrackHandler> handler;
        {
            std::lock_guard lock(state_mutex_);
            handler = GetRequestHandler(FindRequest({ stream.GetConnectionID(), request_id }));
        }

        if (!handler) {
            // TODO: Metrics.
            QUICR_LOGGER_WARN(logger_,
                              "Received fetch_header to unknown fetch track request_id: {} stream: {}, ignored",
                              request_id,
                              stream_id);

            // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging
            return false;
        }

        if (auto h = handler->Get<SubscribeTrackHandler>()) {
            // Fetched objects are each given as a step from the one before, against the order the
            // fetch was made with, so a request that never had one is not what this stream claims.
            const auto group_order = h->GetGroupOrder();
            if (!group_order.has_value()) {
                QUICR_LOGGER_WARN(logger_,
                                  "Received fetch_header naming request_id: {} which was not a fetch, stream: {}, "
                                  "ignored",
                                  request_id,
                                  stream_id);
                return false;
            }

            stream.rx_handler = h;

            // The header itself is still at the front of the stream's buffer. Setting up to parse it
            // there is also what records, for every later arrival, what this stream carries.
            std::lock_guard _(stream.rx_mutex);
            stream.rx_parse.fetch.emplace(*group_order);
            stream.rx_data.InitAny<messages::FetchHeader>(static_cast<std::uint64_t>(StreamMessageType::kFetchHeader));

            return true;
        }

        return false;
    }

    std::shared_ptr<Stream> Session::CreateStream(const std::shared_ptr<PublishTrackHandler>& handler)
    {
        if (handler == nullptr) {
            throw std::invalid_argument("Handler cannot be null");
        }

        const auto request = handler->GetRequest();
        const auto request_stream = request ? request->GetStream() : nullptr;
        const auto connection = request_stream ? request_stream->GetConnection() : nullptr;
        if (!connection) {
            throw std::invalid_argument("Connection cannot be null");
        }

        const auto stream = quic_transport_->CreateDataStream(connection, handler->GetPublishObjectPriority());
        stream->tx_handler = handler;

        // Streams report their own metrics, so the session needs to know which track each belongs to.
        std::lock_guard _(state_mutex_);
        if (!request) {
            throw std::logic_error("Handler is not bound to a request");
        }
        request_by_stream[{ stream->GetConnectionID(), stream->GetStreamId() }] = { .request_id = request->request_id,
                                                                                    .is_request_stream = false };

        return stream;
    }

    void Session::OnRecvDgram(const std::shared_ptr<Connection>& connection)
    {
        if (!connection) {
            return;
        }
        const auto connection_id = connection->GetID();
        for (int i = 0; i < kReadLoopMax; i++) {
            auto data = quic_transport_->Dequeue(connection);
            if (data && !data->empty() && data->size() > 3) {
                auto msg_type = data->front();

                // Message type needs to be either datagram header types or status types.
                if (!DatagramHeaderProperties::IsValid(msg_type)) {
                    QUICR_LOGGER_DEBUG(
                      logger_, "Received datagram that is not a supported datagram type, dropping: {}", msg_type);
                    connection->metrics.rx_dgram_invalid_type++;
                    continue;
                }

                uint64_t track_alias = 0;
                try {
                    // Decode and check next header, subscribe ID
                    auto cursor_it = std::next(data->begin(), 1);

                    auto track_alias_sz = quicr::UintVar::Size(*cursor_it);
                    track_alias = uint64_t(quicr::UintVar({ cursor_it, cursor_it + track_alias_sz }));
                    cursor_it += track_alias_sz;

                } catch (std::invalid_argument&) {
                    continue; // Invalid, not enough bytes to decode
                }

                auto sub_it = sub_by_recv_track_alias.find({ connection_id, track_alias });
                if (sub_it == sub_by_recv_track_alias.end()) {
                    connection->metrics.rx_dgram_unknown_track_alias++;

                    QUICR_LOGGER_DEBUG(
                      logger_, "Received datagram to unknown subscribe track track alias: {}, ignored", track_alias);

                    // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                    continue;
                }

                QUICR_LOGGER_TRACE(logger_,
                                   "Received object datagram conn_id: {} track_alias: {} data size: {}",
                                   connection_id,
                                   track_alias,
                                   data->size());

                auto handler = static_cast<SubscribeTrackHandler*>(sub_it->second.get());

                try {
                    handler->DgramDataRecv(data);
                } catch (const std::exception& e) {
                    QUICR_LOGGER_ERROR(logger_, "Caught exception in ObjectStatusReceived. (error={})", e.what());
                }
            } else if (data) {
                connection->metrics.rx_dgram_decode_failed++;

                QUICR_LOGGER_DEBUG(
                  logger_, "Failed to decode datagram conn_id: {} size: {}", connection_id, data->size());
            }
        }
    }

    void Session::OnConnectionMetricsSampled(const std::shared_ptr<Connection>& connection,
                                             const MetricsTimeStamp sample_time,
                                             const QuicConnectionMetrics& quic_connection_metrics)
    {
        if (!connection) {
            return;
        }
        const auto connection_id = connection->GetID();

        connection->metrics.last_sample_time = sample_time.time_since_epoch() / std::chrono::microseconds(1);
        connection->metrics.quic = quic_connection_metrics;

        if (callbacks_) {
            if (auto self = weak_from_this().lock()) {
                callbacks_->MetricsSampled(self, connection->metrics);
            }
        }

        // The connection sample closes the period, so every handler's counters are final by now.
        // Handlers are sampled whether or not any of their streams reported, so a track that was idle
        // for the period still gets a sample.
        const auto last_sample_time = connection->metrics.last_sample_time;
        std::vector<std::shared_ptr<TrackHandler>> handlers;
        {
            std::lock_guard lock(state_mutex_);
            const auto begin = requests.lower_bound({ connection_id, 0 });
            const auto end = requests.upper_bound({ connection_id, std::numeric_limits<std::uint64_t>::max() });
            for (auto request = begin; request != end; ++request) {
                if (auto handler = GetRequestHandler(request->second)) {
                    handlers.push_back(std::move(handler));
                }
            }
        }

        for (const auto& handler : handlers) {
            if (auto h = handler->Get<SubscribeTrackHandler>()) {
                h->subscribe_track_metrics_.last_sample_time = last_sample_time;
                h->MetricsSampled(h->subscribe_track_metrics_);
            } else if (auto h = handler->Get<PublishTrackHandler>()) {
                h->publish_track_metrics_.last_sample_time = last_sample_time;
                h->MetricsSampled(h->publish_track_metrics_);

                // Unlike the counters beside them, these describe a period, so the next starts empty.
                h->publish_track_metrics_.quic.tx_callback_ms.Clear();
                h->publish_track_metrics_.quic.tx_object_duration_us.Clear();
                h->publish_track_metrics_.quic.tx_queue_size.Clear();
            }
        }
    }

    void Session::OnStreamMetricsStampled(const std::shared_ptr<Connection>& connection,
                                          const MetricsTimeStamp,
                                          const std::uint64_t stream_id,
                                          const QuicStreamMetrics& quic_stream_metrics,
                                          const bool is_final)
    {
        if (!connection) {
            return;
        }
        const auto connection_id = connection->GetID();
        std::lock_guard _(state_mutex_);

        const auto req_it = request_by_stream.find({ connection_id, stream_id });
        if (req_it == request_by_stream.end()) {
            return;
        }

        if (const auto handler = GetRequestHandler(FindRequest({ connection_id, req_it->second.request_id }))) {

            // A track can be carried by several streams, so these accumulate across the period and
            // are handed to the handler when the connection sample closes it.
            if (auto h = handler->Get<SubscribeTrackHandler>()) {
                h->subscribe_track_metrics_.bytes_received += quic_stream_metrics.rx_stream_bytes;

            } else if (auto h = handler->Get<PublishTrackHandler>()) {
                h->publish_track_metrics_.quic.tx_buffer_drops += quic_stream_metrics.tx_buffer_drops;
                h->publish_track_metrics_.quic.tx_delayed_callback += quic_stream_metrics.tx_delayed_callback;
                h->publish_track_metrics_.quic.tx_queue_discards += quic_stream_metrics.tx_queue_discards;
                h->publish_track_metrics_.quic.tx_queue_expired += quic_stream_metrics.tx_queue_expired;

                // Several streams can carry one track, so their periods fold into one distribution.
                h->publish_track_metrics_.quic.tx_callback_ms.Merge(quic_stream_metrics.tx_callback_ms);
                h->publish_track_metrics_.quic.tx_object_duration_us.Merge(quic_stream_metrics.tx_object_duration_us);
                h->publish_track_metrics_.quic.tx_queue_size.Merge(quic_stream_metrics.tx_queue_size);
            }
        }

        // The association exists only to route metrics, so it outlives the stream itself by one
        // sample.
        if (is_final) {
            request_by_stream.erase(req_it);
        }
    }

    std::shared_ptr<Session> Session::GetSharedPtr()
    {
        if (!weak_from_this().lock()) {
            throw std::runtime_error("Transport is not shared_ptr");
        }

        return shared_from_this();
    }

    std::uint64_t Session::GetNextRequestID()
    {
        std::uint64_t rid = next_request_id_;
        next_request_id_ += 2;

        return rid;
    }

    bool Session::Enqueue(const std::shared_ptr<Connection>& connection,
                          const std::shared_ptr<Stream>& stream,
                          std::shared_ptr<const std::vector<uint8_t>> bytes,
                          const uint8_t priority,
                          const uint32_t ttl_ms,
                          const Transport::EnqueueFlags flags)
    {
        if (!connection) {
            throw std::invalid_argument("Connection cannot be null");
        }

        if (!flags.use_reliable) {
            quic_transport_->EnqueueDatagram(connection, std::move(bytes), priority, ttl_ms);
            return true;
        }

        if (!stream || stream->GetConnection() != connection) {
            throw std::invalid_argument("Stream must belong to connection");
        }

        return quic_transport_->Enqueue(connection, stream, std::move(bytes), priority, ttl_ms, flags);
    }

    void Session::Migrate(const std::shared_ptr<Connection>& source_connection, const Redirect& redirect)
    {
        if (!source_connection) {
            return;
        }

        const auto redirect_key = std::pair{ source_connection->GetID(), redirect.request_id };
        {
            std::lock_guard lock(state_mutex_);
            const bool overlaps = std::ranges::any_of(migrations_, [&](const auto& migration) {
                return migration.first.first == redirect_key.first &&
                       (!migration.first.second.has_value() || !redirect_key.second.has_value() ||
                        migration.first.second == redirect_key.second);
            });

            if (overlaps) {
                QUICR_LOGGER_DEBUG(
                  logger_, "Ignoring overlapping redirect while migration is pending (uri='{}')", redirect.uri);
                return;
            }

            migrations_.emplace(redirect_key,
                                Migration{
                                  .redirect = redirect,
                                  .source_connection = source_connection,
                                  .candidate_connection = nullptr,
                                  .pending = {},
                                });
        }

        const auto release_redirect = [&] {
            std::lock_guard lock(state_mutex_);
            migrations_.erase(redirect_key);
        };

        const bool qmux = source_connection->GetProtocol() == Connection::Protocol::kQmux;
        auto uri = detail::ParseConnectUri(redirect.uri, qmux);
        if (!uri.has_value()) {
            QUICR_LOGGER_ERROR(logger_, "Invalid redirect URI (uri='{}')", redirect.uri);
            release_redirect();
            return;
        }

        const auto& [host, port, protocol, path] = uri.value();
        if (protocol != source_connection->GetProtocol()) {
            QUICR_LOGGER_ERROR(logger_,
                               "Redirect changes transport protocol, which one transport cannot currently support "
                               "(uri='{}')",
                               redirect.uri);
            release_redirect();
            return;
        }

        const TransportRemote remote{
            .host_or_ip = host,
            .port = port,
            .web_transport_options = protocol == Connection::Protocol::kWebTransport
                                       ? std::make_optional(WebTransportOptions{ .path = path })
                                       : std::nullopt,
            .qmux = qmux,
        };

        std::shared_ptr<Connection> connection;
        try {
            connection = quic_transport_->Connect(remote);
        } catch (const std::exception& e) {
            QUICR_LOGGER_ERROR(
              logger_, "Failed to connect to redirect URI (uri='{}', error='{}')", redirect.uri, e.what());
            release_redirect();
            return;
        }

        if (!connection) {
            QUICR_LOGGER_ERROR(logger_, "Failed to connect to redirect URI (uri='{}')", redirect.uri);
            release_redirect();
            return;
        }

        {
            std::lock_guard lock(connections_mutex_);
            connections_[connection->GetID()] = connection;
        }

        {
            std::lock_guard lock(state_mutex_);
            migrations_.at(redirect_key).candidate_connection = connection;
        }

        if (connection_added_callback_) {
            connection_added_callback_(connection, shared_from_this());
        }

        try {
            connection->SetDelegate(shared_from_this());
        } catch (const std::exception& e) {
            QUICR_LOGGER_ERROR(
              logger_, "Failed to initialize redirect connection conn_id: {} error: {}", connection->GetID(), e.what());
            {
                std::lock_guard lock(state_mutex_);
                migrations_.erase(redirect_key);
            }
            {
                std::lock_guard lock(connections_mutex_);
                connections_.erase(connection->GetID());
            }
            connection->SetDelegate(nullptr);
            quic_transport_->Close(connection);
        }
    }

    bool Session::MatchesRedirect(const TrackHandler& handler, const Redirect& redirect) const
    {
        const auto& request = handler.GetRequest();
        return !redirect.request_id.has_value() || (request && request->request_id == *redirect.request_id);
    }

    Session::MigrationMap::iterator Session::FindMigration(const Connection& candidate)
    {
        return std::ranges::find_if(migrations_, [&](const auto& migration) {
            return migration.second.candidate_connection &&
                   migration.second.candidate_connection->GetID() == candidate.GetID();
        });
    }

    void Session::StartMigrationRequests(const std::shared_ptr<Connection>& connection)
    {
        Redirect redirect;
        std::shared_ptr<Connection> source_connection;
        MigrationKey migration_key;
        std::vector<std::shared_ptr<TrackHandler>> handlers;
        {
            std::lock_guard lock(state_mutex_);
            const auto migration = FindMigration(*connection);
            if (migration == migrations_.end()) {
                return;
            }

            migration_key = migration->first;
            redirect = migration->second.redirect;
            source_connection = migration->second.source_connection;
            for (const auto& [_, request] : requests) {
                const auto handler = GetRequestHandler(request);
                const auto request_stream = request->GetStream();
                if (handler && request_stream && request_stream->GetConnection() == source_connection &&
                    MatchesRedirect(*handler, redirect)) {
                    handlers.push_back(handler);
                }
            }
        }

        std::stable_sort(handlers.begin(), handlers.end(), [](const auto& lhs, const auto& rhs) {
            const auto dependency_rank = [](const std::shared_ptr<TrackHandler>& handler) {
                if (handler->Get<SubscribeNamespaceHandler>() || handler->Get<PublishNamespaceHandler>()) {
                    return 0;
                }
                return handler->Get<FetchTrackHandler>() ? 2 : 1;
            };
            return dependency_rank(lhs) < dependency_rank(rhs);
        });

        if (handlers.empty()) {
            const bool session_wide = !redirect.request_id.has_value();
            std::shared_ptr<Connection> old_connection;
            {
                std::lock_guard lock(state_mutex_);
                migrations_.erase(migration_key);
                if (session_wide && default_connection_ == source_connection) {
                    old_connection = source_connection;
                    default_connection_ = connection;
                }
            }

            if (session_wide && old_connection && old_connection != connection) {
                RemoveTracksForConnectionClose(old_connection);
                {
                    std::lock_guard lock(connections_mutex_);
                    connections_.erase(old_connection->GetID());
                }
                old_connection->SetDelegate(nullptr);
                quic_transport_->Close(old_connection);
            } else if (!session_wide) {
                {
                    std::lock_guard lock(connections_mutex_);
                    connections_.erase(connection->GetID());
                }
                RemoveTracksForConnectionClose(connection);
                connection->SetDelegate(nullptr);
                quic_transport_->Close(connection);
            }

            return;
        }

        defer({
            {
                std::lock_guard lock(state_mutex_);

                const auto migration = migrations_.find(migration_key);
                if (migration == migrations_.end() || !migration->second.pending.empty()) {
                    return;
                }

                migrations_.erase(migration);
            }

            {
                std::lock_guard lock(connections_mutex_);
                connections_.erase(connection->GetID());
            }

            connection->SetDelegate(nullptr);
            quic_transport_->Close(connection);
        });

        for (const auto& handler : handlers) {
            const auto& request = handler->GetRequest();
            if (!request) {
                continue;
            }
            const auto request_id = request->request_id;

            MigrationRequestType type;
            if (const auto subscribe = handler->Get<SubscribeTrackHandler>()) {
                if (subscribe->IsPublisherInitiated()) {
                    continue;
                }
                type = handler->Get<FetchTrackHandler>() || handler->Get<JoiningFetchHandler>()
                         ? MigrationRequestType::kFetch
                         : MigrationRequestType::kSubscribe;
            } else if (handler->Get<PublishTrackHandler>()) {
                type = MigrationRequestType::kPublish;
            } else if (handler->Get<SubscribeNamespaceHandler>()) {
                type = MigrationRequestType::kSubscribeNamespace;
            } else if (handler->Get<PublishNamespaceHandler>()) {
                type = MigrationRequestType::kPublishNamespace;
            } else {
                continue;
            }

            const auto request_stream = quic_transport_->CreateRequestStream(connection);
            if (!request_stream) {
                QUICR_LOGGER_ERROR(logger_,
                                   "Failed to create migration request stream conn_id: {} request_id: {}",
                                   connection->GetID(),
                                   request_id);
                continue;
            }

            {
                std::lock_guard lock(state_mutex_);
                request_by_stream[{ connection->GetID(), request_stream->GetStreamId() }] = { .request_id = request_id,
                                                                                              .is_request_stream =
                                                                                                true };
                migrations_.at(migration_key).pending[request_id] = {
                    .type = type,
                    .request_stream = request_stream,
                };
            }

            const auto redirected_name = [&](FullTrackName name) {
                if (!redirect.track_namespace.GetEntries().empty()) {
                    name.name_space = redirect.track_namespace;
                }
                if (redirect.track_name.has_value() && !redirect.track_name->empty()) {
                    name.name = *redirect.track_name;
                }
                return name;
            };

            try {
                switch (type) {
                    case MigrationRequestType::kSubscribe: {
                        const auto subscribe = handler->Get<SubscribeTrackHandler>();

                        // TODO: Make this require a new group request
                        auto filter = subscribe->GetFilter();
                        if (const auto delivered = subscribe->GetLatestDeliveredLocation()) {
                            if (std::holds_alternative<std::monostate>(filter)) {
                                filter = messages::SubscriptionFilter{
                                    .type = messages::SubscriptionFilterType::kAbsoluteStart,
                                    .start_location = *delivered,
                                };
                            } else if (auto* location = std::get_if<messages::SubscriptionFilter>(&filter)) {
                                const auto end_group =
                                  location->type == messages::SubscriptionFilterType::kAbsoluteRange
                                    ? std::optional{ location->start_location.group + location->end_group_delta }
                                    : std::nullopt;
                                location->type = end_group ? messages::SubscriptionFilterType::kAbsoluteRange
                                                           : messages::SubscriptionFilterType::kAbsoluteStart;
                                location->start_location = std::max(location->start_location, *delivered);
                                if (end_group) {
                                    location->end_group_delta = *end_group > location->start_location.group
                                                                  ? *end_group - location->start_location.group
                                                                  : 0;
                                }
                            }
                        }

                        const auto full_name = redirected_name(subscribe->GetFullTrackName());
                        const auto track_hash = TrackHash(full_name);

                        if (!SendSubscribe(request_stream,
                                           request_id,
                                           full_name,
                                           track_hash,
                                           subscribe->GetSubscriberPriority(),
                                           subscribe->GetGroupOrder(),
                                           filter,
                                           subscribe->GetDeliveryTimeout(),
                                           subscribe->GetRendezvousTimeout())) {
                            throw std::runtime_error("Failed to send migrated SUBSCRIBE");
                        }

                        break;
                    }
                    case MigrationRequestType::kPublish: {
                        const auto publish_handler = handler->Get<PublishTrackHandler>();
                        const auto full_name = redirected_name(publish_handler->GetFullTrackName());
                        const PublishAttributes publish{
                            .track_full_name = { full_name },
                            .track_alias =
                              publish_handler->GetTrackAlias().value_or(TrackHash(full_name).track_fullname_hash),
                            .auth_tokens = {},
                            .expires = 0,
                            .largest_object = publish_handler->GetLargestLocation(),
                            .forward = true,
                            .default_publisher_group_order = GroupOrder::kAscending,
                            .dynamic_groups = publish_handler->support_new_group_request_,
                            .publisher_priority = publish_handler->GetPublisherPriority(),
                            .max_cache_duration = std::nullopt,
                            .delivery_timeout = publish_handler->GetDefaultTTL(),
                            .track_properties = {},
                        };

                        if (!SendPublish(request_stream, request_id, publish)) {
                            throw std::runtime_error("Failed to send migrated PUBLISH");
                        }

                        break;
                    }
                    case MigrationRequestType::kSubscribeNamespace: {
                        const auto ns_handler = handler->Get<SubscribeNamespaceHandler>();
                        const auto message_type = ns_handler->GetMode() == SubscribeNamespaceHandler::Mode::kNamespaces
                                                    ? ControlMessageType::kSubscribeNamespace
                                                    : ControlMessageType::kSubscribeTracks;
                        const auto& prefix = redirect.track_namespace.GetEntries().empty() ? ns_handler->GetPrefix()
                                                                                           : redirect.track_namespace;
                        if (!SendSubscribeNamespace(
                              request_stream, request_id, prefix, ns_handler->GetFilter(), message_type)) {
                            throw std::runtime_error("Failed to send migrated namespace subscription");
                        }

                        break;
                    }
                    case MigrationRequestType::kPublishNamespace: {
                        const auto ns_handler = handler->Get<PublishNamespaceHandler>();
                        const auto& prefix = redirect.track_namespace.GetEntries().empty() ? ns_handler->GetPrefix()
                                                                                           : redirect.track_namespace;
                        if (!SendPublishNamespace(request_stream, request_id, prefix)) {
                            throw std::runtime_error("Failed to send migrated PUBLISH_NAMESPACE");
                        }

                        break;
                    }
                    case MigrationRequestType::kFetch: {
                        if (const auto joining_handler = handler->Get<JoiningFetchHandler>()) {
                            const auto& subscribe = joining_handler->joining_subscribe_;
                            const auto info = subscribe->GetJoiningFetch();
                            const auto& joining_request = subscribe->GetRequest();

                            if (!info || !joining_request) {
                                throw std::logic_error("Joining FETCH is missing its subscription");
                            }

                            SendJoiningFetch(request_stream,
                                             request_id,
                                             info->priority,
                                             info->group_order,
                                             joining_request->request_id,
                                             info->joining_start,
                                             info->absolute);
                        } else {
                            const auto fetch_handler = handler->Get<FetchTrackHandler>();
                            if (!SendFetch(request_stream,
                                           request_id,
                                           redirected_name(fetch_handler->GetFullTrackName()),
                                           fetch_handler->GetSubscriberPriority(),
                                           fetch_handler->GetGroupOrder(),
                                           fetch_handler->GetStartLocation(),
                                           fetch_handler->GetEndLocation())) {
                                throw std::runtime_error("Failed to send migrated FETCH");
                            }
                        }

                        break;
                    }
                }
            } catch (const std::exception& e) {
                QUICR_LOGGER_ERROR(logger_,
                                   "Failed to send migration request conn_id: {} request_id: {} error: {}",
                                   connection->GetID(),
                                   request_id,
                                   e.what());
                {
                    std::lock_guard lock(state_mutex_);
                    request_by_stream.erase({ connection->GetID(), request_stream->GetStreamId() });
                    if (const auto migration = migrations_.find(migration_key); migration != migrations_.end()) {
                        migration->second.pending.erase(request_id);
                    }
                }

                quic_transport_->CloseStream(request_stream, StreamOperation::kCancel);
            }
        }
    }

    void Session::CompleteMigration(const std::shared_ptr<Connection>& connection,
                                    std::uint64_t request_id,
                                    MigrationRequestType type,
                                    std::optional<std::uint64_t> received_track_alias)
    {
        PendingMigration pending;
        MigrationKey migration_key;
        std::shared_ptr<Connection> old_connection;
        bool session_wide = false;
        std::shared_ptr<TrackHandler> handler;
        {
            std::lock_guard lock(state_mutex_);
            const auto migration = FindMigration(*connection);
            if (migration == migrations_.end()) {
                return;
            }

            const auto pending_it = migration->second.pending.find(request_id);
            if (pending_it == migration->second.pending.end()) {
                return;
            }

            pending = pending_it->second;
            if (pending.type != type) {
                throw ProtocolViolationException("Migration response does not match its request type");
            }
            if (!pending.request_stream) {
                throw std::logic_error("Migration is missing its replacement request stream");
            }

            migration_key = migration->first;
            old_connection = migration->second.source_connection;
            session_wide = !migration_key.second.has_value();

            handler = GetRequestHandler(FindRequest({ old_connection->GetID(), request_id }));
        }

        if (!handler) {
            QUICR_LOGGER_WARN(logger_,
                              "Migration completed without a request handler conn_id: {} request_id: {}",
                              connection->GetID(),
                              request_id);
            FailMigration(connection, request_id);
            return;
        }

        std::unique_lock migration_lock(handler->binding_operation_mutex_);
        const auto bound_request = handler->GetRequest();
        const auto old_request_stream = bound_request ? bound_request->GetStream() : nullptr;
        if (!old_request_stream) {
            migration_lock.unlock();
            FailMigration(connection, request_id);
            return;
        }
        const auto old_connection_id = old_connection->GetID();
        std::uint64_t old_stream_count = 0;
        if (const auto publisher = handler->Get<PublishTrackHandler>()) {
            publisher->EndAllSubgroupsInternal(false);
            old_stream_count = std::exchange(publisher->publish_track_metrics_.opened_streams, 0);
        }

        bool source_request_missing = false;
        {
            std::lock_guard lock(state_mutex_);
            const RequestKey old_key{ old_connection_id, request_id };
            const RequestKey new_key{ connection->GetID(), request_id };
            if (requests.contains(new_key)) {
                throw std::logic_error("Migration target already has the request");
            }
            if (const auto stream_request = pending.request_stream->request_.lock();
                stream_request && stream_request != bound_request) {
                throw std::logic_error("Migration request stream is already bound to another request");
            }

            auto request = requests.extract(old_key);
            if (request.empty()) {
                source_request_missing = true;
            } else {
                const auto migrated_request = request.mapped();
                request.key() = new_key;
                requests.insert(std::move(request));

                if (old_request_stream->request_.lock() == migrated_request) {
                    old_request_stream->request_.reset();
                }
                migrated_request->SetStream(pending.request_stream);
                pending.request_stream->request_ = migrated_request;

                if (const auto subscriber = handler->Get<SubscribeTrackHandler>()) {
                    if (const auto old_alias = subscriber->GetReceivedTrackAlias()) {
                        sub_by_recv_track_alias.erase({ old_connection_id, *old_alias });
                    }

                    if (received_track_alias.has_value()) {
                        subscriber->SetReceivedTrackAlias(*received_track_alias);
                        sub_by_recv_track_alias[{ connection->GetID(), *received_track_alias }] = subscriber;
                    }
                } else if (const auto publisher = handler->Get<PublishTrackHandler>();
                           publisher && !handler->Get<PublishFetchHandler>()) {
                    const auto track_alias = TrackHash(publisher->GetFullTrackName()).track_fullname_hash;
                    if (auto tracks = pub_tracks_by_track_alias.find(track_alias);
                        tracks != pub_tracks_by_track_alias.end()) {
                        tracks->second.erase(old_connection_id);
                        tracks->second[connection->GetID()] = publisher;
                    }
                }

                request_by_stream.erase({ old_connection_id, old_request_stream->GetStreamId() });

                if (const auto migration = migrations_.find(migration_key); migration != migrations_.end()) {
                    migration->second.pending.erase(request_id);
                    if (migration->second.pending.empty()) {
                        migrations_.erase(migration);
                    }
                }
            }
        }

        if (source_request_missing) {
            migration_lock.unlock();
            FailMigration(connection, request_id);
            return;
        }

        if (handler->Get<PublishTrackHandler>() && !handler->Get<PublishFetchHandler>()) {
            SendPublishDone(old_request_stream,
                            request_id,
                            messages::PublishDoneStatusCode::kGoingAway,
                            old_stream_count,
                            "Migrated");
            quic_transport_->CloseStream(old_request_stream, StreamOperation::kFin);
        } else {
            quic_transport_->CloseStream(old_request_stream, StreamOperation::kCancel);
        }

        bool old_connection_in_use = false;
        {
            std::lock_guard lock(state_mutex_);

            old_connection_in_use = std::ranges::any_of(
              requests, [old_connection_id](const auto& item) { return item.first.first == old_connection_id; });

            if (session_wide && !old_connection_in_use && default_connection_ == old_connection) {
                default_connection_ = connection;
            }
        }

        if (session_wide && !old_connection_in_use) {
            RemoveTracksForConnectionClose(old_connection);

            {
                std::lock_guard lock(connections_mutex_);
                connections_.erase(old_connection_id);
            }

            old_connection->SetDelegate(nullptr);

            if (old_connection->GetStatus() == Connection::Status::kReady) {
                quic_transport_->Close(old_connection);
            }
        }
    }

    bool Session::FailMigration(const std::shared_ptr<Connection>& connection,
                                std::uint64_t request_id,
                                bool close_request_stream)
    {
        std::shared_ptr<Stream> request_stream;
        std::shared_ptr<Connection> old_connection;
        bool close_connection = false;
        {
            std::lock_guard lock(state_mutex_);
            const auto migration = FindMigration(*connection);
            if (migration == migrations_.end()) {
                return false;
            }

            const auto pending = migration->second.pending.find(request_id);
            if (pending == migration->second.pending.end()) {
                return false;
            }

            old_connection = migration->second.source_connection;
            request_stream = pending->second.request_stream;
            request_by_stream.erase({ connection->GetID(), request_stream->GetStreamId() });
            migration->second.pending.erase(pending);

            const bool has_other_requests = !migration->second.pending.empty();
            if (!has_other_requests) {
                migrations_.erase(migration);
            }

            close_connection = !has_other_requests && std::ranges::none_of(requests, [&](const auto& item) {
                return item.first.first == connection->GetID();
            });
        }

        if (close_request_stream) {
            quic_transport_->CloseStream(request_stream, StreamOperation::kCancel);
        }

        if (old_connection && old_connection->GetStatus() != Connection::Status::kReady) {
            RemoveTracksForConnectionClose(old_connection);
        }

        if (close_connection) {
            RemoveTracksForConnectionClose(connection);

            {
                std::lock_guard lock(connections_mutex_);
                connections_.erase(connection->GetID());
            }

            connection->SetDelegate(nullptr);
            quic_transport_->Close(connection);
        }

        return true;
    }

    void Session::ResetSubgroup(const std::shared_ptr<Stream>& stream)
    {
        if (!stream) {
            return;
        }
        quic_transport_->CloseStream(stream, StreamOperation::kReset);
    }

    // -- Resolve Methods --

    void Session::ResolveFetch(uint64_t request_id,
                               std::optional<messages::GroupOrder> group_order,
                               const FetchResponse& response)
    {
        const auto request_it = recv_req_id.find(request_id);
        if (request_it == recv_req_id.end() || request_it->second.stream == nullptr) {
            QUICR_LOGGER_ERROR(logger_, "Cannot resolve FETCH without its request stream request_id: {}", request_id);
            return;
        }

        SendFetchOk(
          request_it->second.stream, response.publisher_default_group_order, false, response.largest_location.value());
    }

    std::shared_ptr<Stream> Session::FindSubscribeNamespaceStream(const TrackNamespace& track_namespace) const
    {
        std::lock_guard lock(state_mutex_);
        for (const auto& [_, request] : requests) {
            const auto handler = GetRequestHandler(request);
            if (!handler) {
                continue;
            }
            if (auto h = handler->Get<SubscribeNamespaceHandler>()) {
                const auto bound_request = h->GetRequest();
                auto request_stream = bound_request ? bound_request->GetStream() : nullptr;
                if (request_stream == nullptr) {
                    continue;
                }

                const auto match = h->GetPrefix().IsPrefixOf(track_namespace);
                if (match == std::partial_ordering::unordered || match == std::partial_ordering::less) {
                    continue;
                }

                return request_stream;
            }
        }

        return nullptr;
    }

    std::optional<std::uint64_t> Session::FindRequestIdByStream(const Stream& stream)
    {
        std::lock_guard _(state_mutex_);
        const auto request_it = request_by_stream.find({ stream.GetConnectionID(), stream.GetStreamId() });
        if (request_it == request_by_stream.end()) {
            return std::nullopt;
        }

        return request_it->second.request_id;
    }

    bool Session::IsCurrentRequestStream(std::uint64_t request_id, const std::shared_ptr<Stream>& stream)
    {
        if (!stream) {
            QUICR_LOGGER_DEBUG(logger_, "Ignoring stale request result request_id: {} without a stream", request_id);
            return false;
        }

        bool is_current = false;
        if (stream->IsOpen()) {
            std::lock_guard lock(state_mutex_);
            const auto request = request_by_stream.find({ stream->GetConnectionID(), stream->GetStreamId() });
            is_current = request != request_by_stream.end() && request->second.is_request_stream &&
                         request->second.request_id == request_id;
        }

        if (!is_current) {
            QUICR_LOGGER_DEBUG(logger_,
                               "Ignoring stale request result conn_id: {} stream_id: {} request_id: {}",
                               stream->GetConnectionID(),
                               stream->GetStreamId(),
                               request_id);
        }
        return is_current;
    }

    // -- Server Relay Methods --

    void Session::BindPublisherTrack(std::uint64_t src_id,
                                     uint64_t request_id,
                                     const std::shared_ptr<PublishTrackHandler>& track_handler,
                                     bool ephemeral)
    {
        if (!track_handler) {
            throw std::invalid_argument("Handler cannot be null");
        }

        track_handler->RegisterSession(GetSharedPtr());

        std::optional<PublishTrackHandler::Status> changed_status;
        {
            std::lock_guard _(state_mutex_);

            // Generate track alias
            const auto& tfn = track_handler->GetFullTrackName();
            auto th = TrackHash(tfn);

            if (!track_handler->GetTrackAlias().has_value()) {
                track_handler->SetTrackAlias(th.track_fullname_hash);
            }

            const auto req_it = recv_req_id.find(request_id);
            if (req_it == recv_req_id.end() || !req_it->second.stream) {
                throw std::logic_error("Publisher track is missing its received request stream");
            }

            const auto& request_stream = req_it->second.stream;
            const RequestKey request_key{ request_stream->GetConnectionID(), request_id };
            if (auto request = FindRequest(request_key)) {
                if (request->type != Request::Type::kSubscribe || request->is_locally_initiated) {
                    throw std::logic_error("Publisher track cannot bind to this request");
                }

                const auto is_bound = std::ranges::any_of(
                  request->handlers, [&](const auto& handler) { return handler.get() == track_handler.get(); });
                if (!is_bound) {
                    BindHandlerToRequest(request, track_handler);
                }
            } else {
                request = std::make_shared<Request>(Request{
                  .request_id = request_id,
                  .type = Request::Type::kSubscribe,
                  .stream = request_stream,
                  .name = tfn,
                  .handlers = {},
                  .is_locally_initiated = false,
                });

                AddRequest(request);
                BindHandlerToRequest(request, track_handler);
            }

            if (!ephemeral) {
                // Hold onto track handler
                pub_tracks_by_name[th.track_namespace_hash][th.track_name_hash] = track_handler;
                pub_tracks_by_track_alias[th.track_fullname_hash][src_id] = track_handler;
            }
            changed_status = track_handler->UpdateStatus(PublishTrackHandler::Status::kOk);
        }
        if (changed_status) {
            track_handler->StatusChanged(*changed_status);
        }
    }

    void Session::UnbindPublisherTrack(std::uint64_t src_id,
                                       const std::shared_ptr<PublishTrackHandler>& track_handler,
                                       bool send_publish_done)
    {
        std::optional<PublishTrackHandler::Status> changed_status;
        {
            std::lock_guard binding_lock(track_handler->binding_operation_mutex_);
            std::lock_guard state_lock(state_mutex_);

            auto th = TrackHash(track_handler->GetFullTrackName());
            const auto request = FindRequestForHandler(*track_handler);
            const auto request_stream = request ? request->GetStream() : nullptr;
            QUICR_LOGGER_DEBUG(
              logger_,
              "Server publish track conn_id: {} full_name_hash: {} namespace_hash: {} name_hash: {} unbind",
              request_stream ? request_stream->GetConnectionID() : 0,
              th.track_fullname_hash,
              th.track_namespace_hash,
              th.track_name_hash);

            if (request) {
                UnbindHandler(request, *track_handler);
            }

            if (auto namespace_it = pub_tracks_by_name.find(th.track_namespace_hash);
                namespace_it != pub_tracks_by_name.end()) {
                const auto name_it = namespace_it->second.find(th.track_name_hash);
                if (name_it != namespace_it->second.end() && name_it->second == track_handler) {
                    namespace_it->second.erase(name_it);
                    if (namespace_it->second.empty()) {
                        pub_tracks_by_name.erase(namespace_it);
                    }
                }
            }

            if (auto alias_it = pub_tracks_by_track_alias.find(th.track_fullname_hash);
                alias_it != pub_tracks_by_track_alias.end()) {
                const auto connection_it = alias_it->second.find(src_id);
                if (connection_it != alias_it->second.end() && connection_it->second == track_handler) {
                    alias_it->second.erase(connection_it);
                    if (alias_it->second.empty()) {
                        pub_tracks_by_track_alias.erase(alias_it);
                    }
                }
            }

            changed_status = track_handler->UpdateStatus(PublishTrackHandler::Status::kNoSubscribers);

            if (pub_tracks_by_name.count(th.track_namespace_hash) == 0) {
                QUICR_LOGGER_DEBUG(logger_,
                                   "Server publish track conn_id: {} full_name_hash: {} namespace_hash: {} unbind",
                                   request_stream ? request_stream->GetConnectionID() : 0,
                                   th.track_fullname_hash,
                                   th.track_namespace_hash);

                pub_tracks_by_name.erase(th.track_namespace_hash);
            }

            track_handler->EndAllSubgroupsInternal(false);

            if (send_publish_done && request_stream) {
                SendPublishDone(request_stream,
                                request->request_id,
                                messages::PublishDoneStatusCode::kSubscriptionEnded,
                                track_handler->publish_track_metrics_.opened_streams,
                                "No publishers");
            }
        }

        if (changed_status) {
            track_handler->StatusChanged(*changed_status);
        }
        track_handler->UnregisterSession(GetSharedPtr());
    }

    void Session::BindFetchTrack(std::uint64_t request_id, std::shared_ptr<PublishFetchHandler> track_handler)
    {
        if (!track_handler) {
            throw std::invalid_argument("Handler cannot be null");
        }

        std::lock_guard lock(state_mutex_);

        const auto request_it = recv_req_id.find(request_id);
        if (request_it == recv_req_id.end() || request_it->second.stream == nullptr) {
            QUICR_LOGGER_ERROR(logger_, "Cannot bind FETCH without its request stream request_id: {}", request_id);
            return;
        }
        track_handler->RegisterSession(GetSharedPtr());
        QUICR_LOGGER_INFO(logger_,
                          "Publish fetch track conn_id: {} subscribe: {}",
                          request_it->second.stream->GetConnectionID(),
                          request_id);

        const auto request = std::make_shared<Request>(Request{
          .request_id = request_id,
          .type = Request::Type::kFetch,
          .stream = request_it->second.stream,
          .name = track_handler->GetFullTrackName(),
          .handlers = {},
          .is_locally_initiated = false,
        });

        track_handler->SetStatus(PublishFetchHandler::Status::kOk);
        AddRequest(request);
        BindHandlerToRequest(request, track_handler);

        // Hold ref to track handler
        pub_fetch_tracks_by_request_id[request_id] = track_handler;
    }

    void Session::UnbindFetchTrack(const std::shared_ptr<PublishFetchHandler>& track_handler)
    {
        std::lock_guard binding_lock(track_handler->binding_operation_mutex_);
        std::lock_guard state_lock(state_mutex_);

        const auto& request = track_handler->GetRequest();
        if (!request) {
            return;
        }
        const auto request_id = request->request_id;
        const auto request_stream = request->GetStream();
        QUICR_LOGGER_DEBUG(logger_,
                           "Server publish fetch track conn_id: {} subscribe id: {} unbind",
                           request_stream ? request_stream->GetConnectionID() : 0,
                           request_id);

        pub_fetch_tracks_by_request_id.erase(request_id);

        // Drain before closing: the fetch's objects are queued on its stream, and unbinding is the
        // normal end of a completed fetch rather than an abort.
        track_handler->EndFetch();
        RemoveRequest(request);
        track_handler->UnregisterSession(GetSharedPtr());
    }

    // -- Private --

    bool Session::ProcessCtrlMessage(const std::shared_ptr<Connection>& connection,
                                     const std::shared_ptr<Stream>& stream,
                                     messages::ControlMessageType msg_type,
                                     BytesSpan msg_bytes)
    {
        switch (msg_type) {
            case messages::ControlMessageType::kSetup: {
                if (connection->IsSetupComplete()) {
                    if (connection == GetDefaultConnection()) {
                        SetStatus(Status::kInternalError);
                    }
                    throw ProtocolViolationException("Duplicate SETUP received");
                }
                const auto setup_options = messages::Message::ParseField<messages::KeyValuePairs>(msg_bytes);

                std::string endpoint_id = "Unknown Endpoint ID";
                if (auto endpoint = setup_options.Get<std::string>(messages::SetupOptionType::kEndpointId)) {
                    endpoint_id = *endpoint;
                }

                if (callbacks_) {
                    const auto result = callbacks_->SetupReceived(GetSharedPtr(), { endpoint_id });
                    if (!result) {
                        const auto& [code, reason, _] = result.error();
                        QUICR_LOGGER_ERROR(logger_,
                                           "Setup rejected conn_id: {} code: {} reason: {}",
                                           connection->GetID(),
                                           static_cast<std::uint64_t>(code),
                                           reason.value_or("unknown"));
                        if (connection == GetDefaultConnection()) {
                            SetStatus(Status::kInternalError);
                        }
                        quic_transport_->Close(connection, AppReasonForClose::kNotAuthorized);
                        return true;
                    }
                }

                if (!connection->RecordSetupReceived()) {
                    if (connection == GetDefaultConnection()) {
                        SetStatus(Status::kInternalError);
                    }
                    throw ProtocolViolationException("Duplicate SETUP received");
                }

                CheckReady(connection);

                QUICR_LOGGER_INFO(logger_, "Setup received conn_id: {} from: {}", connection->GetID(), endpoint_id);

                return true;
            }
            case messages::ControlMessageType::kGoaway: {
                const std::string new_session_uri = [&] {
                    const auto uri_bytes = messages::Message::ParseField<Bytes>(msg_bytes);
                    return std::string(uri_bytes.begin(), uri_bytes.end());
                }();

                const std::uint64_t timeout = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const std::optional<std::uint64_t> first_unprocessed_request_id =
                  messages::Message::ParseOptionalField<std::uint64_t>(msg_bytes);

                if (new_session_uri.size() > detail::kMaxUriLength) {
                    throw ProtocolViolationException(
                      quicr::format("GOAWAY URI exceeds {} bytes", detail::kMaxUriLength));
                }

                if (stream->RecordGoAwayReceived()) {
                    throw ProtocolViolationException(
                      quicr::format("Duplicate GOAWAY received on stream (stream_id={})", stream->GetStreamId()));
                }

                if (!client_mode_) {
                    if (!new_session_uri.empty()) {
                        throw ProtocolViolationException("Client GOAWAY included a new session URI");
                    }

                    return true;
                }

                const auto redirect_uri = new_session_uri.empty() ? client_config_.connect_uri : new_session_uri;
                QUICR_LOGGER_INFO(logger_,
                                  "Received session GOAWAY new session URI: {} timeout_ms: {} first request ID: {}",
                                  redirect_uri,
                                  timeout,
                                  first_unprocessed_request_id.value_or(0));

                Migrate(connection, { .uri = redirect_uri });

                return true;
            }
            default: {
                QUICR_LOGGER_ERROR(
                  logger_, "Unsupported session control message, type: {}", static_cast<std::uint64_t>(msg_type));
                throw ProtocolViolationException("Unsupported session control message");
            }
        }
    }

    bool Session::ProcessRequestMessage(const std::shared_ptr<Connection>& connection,
                                        const std::shared_ptr<Stream>& stream,
                                        messages::ControlMessageType msg_type,
                                        BytesSpan msg_bytes)
    {
        switch (msg_type) {
            case messages::ControlMessageType::kSubscribe:
            case messages::ControlMessageType::kTrackStatus:
            case messages::ControlMessageType::kPublishNamespace:
            case messages::ControlMessageType::kSubscribeTracks:
            case messages::ControlMessageType::kSubscribeNamespace:
            case messages::ControlMessageType::kFetch:
            case messages::ControlMessageType::kPublish: {
                auto request = msg_bytes;
                const auto request_id = messages::Message::ParseField<std::uint64_t>(request);
                const auto first_unprocessed_request_id = connection->GetFirstUnprocessedRequestId();
                if (first_unprocessed_request_id && request_id >= *first_unprocessed_request_id) {
                    SendRequestError(stream, request_id, ErrorCode::kGoingAway, 0ms, "Session is going away");
                    return true;
                }
                break;
            }
            default:
                break;
        }

        switch (msg_type) {
            case messages::ControlMessageType::kSubscribe: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto track_namespace = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                const auto track_name = messages::Message::ParseField<Bytes>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                auto tfn = FullTrackName{ track_namespace, track_name };
                auto th = TrackHash(tfn);
                {
                    std::lock_guard _(state_mutex_);
                    recv_req_id[request_id] = { .track_full_name = tfn, .track_hash = th, .stream = stream };
                    request_by_stream[{ stream->GetConnectionID(), stream->GetStreamId() }] = {
                        .request_id = request_id, .is_request_stream = true
                    };
                }

                if (client_mode_) {
                    auto ptd = GetPubTrackHandler(th);
                    if (ptd == nullptr) {
                        QUICR_LOGGER_WARN(logger_,
                                          "Received subscribe unknown publish track conn_id: {} namespace hash: {} "
                                          "name hash: {} request_id: {}",
                                          connection->GetID(),
                                          th.track_namespace_hash,
                                          th.track_name_hash,
                                          request_id);

                        SendRequestError(
                          stream, request_id, ErrorCode::kDoesNotExist, 0ms, "Published track not found");
                        return true;
                    }

                    {
                        std::lock_guard lock(state_mutex_);
                        if (const auto old_request = FindRequestForHandler(*ptd)) {
                            RemoveRequest(old_request);
                        }
                        const auto request = std::make_shared<Request>(Request{
                          .request_id = request_id,
                          .type = Request::Type::kSubscribe,
                          .stream = stream,
                          .name = tfn,
                          .handlers = {},
                          .is_locally_initiated = false,
                        });
                        AddRequest(request);
                        BindHandlerToRequest(request, ptd);
                    }

                    // TODO: These should not exist and only be set by a callback, but preserving existing behaviour.
                    SubscribeResponse default_response;
                    default_response.expires = std::chrono::milliseconds(kSubscribeExpires);
                    default_response.track_properties = TrackExtensions{}
                                                          .Add(ExtensionType::kDefaultPublisherPriority, 1)
                                                          .Add(ExtensionType::kDynamicGroups, true);

                    SendSubscribeOk(stream, request_id, ptd->GetTrackAlias().value(), default_response);

                    ptd->SetTrackAlias(ptd->GetTrackAlias().value());
                    ptd->SetStatus(PublishTrackHandler::Status::kOk);

                    return true;
                }

                auto delivery_timeout =
                  parameters.Get<std::uint64_t>(messages::ParameterType::kDeliveryTimeout).value_or(0);
                const auto rendezvous_timeout_ms =
                  parameters.Get<std::uint64_t>(messages::ParameterType::kRendezvousTimeout).value_or(0);
                if (rendezvous_timeout_ms > std::chrono::milliseconds::max().count()) {
                    SendRequestError(
                      stream, request_id, ErrorCode::kNotSupported, 0ms, "Rendezvous timeout too large", true);
                    return true;
                }
                const auto rendezvous_timeout = std::chrono::milliseconds{ rendezvous_timeout_ms };
                auto priority = parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority).value_or(128);
                auto group_order = messages::ResolveGroupOrder(parameters);
                const auto publisher_default_group_order = messages::GroupOrder::kAscending;
                auto forward = messages::ResolveForward(parameters, true);
                auto new_group_request_id = parameters.Get<std::uint64_t>(messages::ParameterType::kNewGroupRequest);
                auto filter = messages::ResolveFilter(parameters);

                if (auto callbacks = std::dynamic_pointer_cast<ServerCallbacks>(callbacks_)) {
                    callbacks
                      ->SubscribeReceived(GetSharedPtr(),
                                          request_id,
                                          tfn,
                                          {
                                            .priority = priority,
                                            .group_order = group_order,
                                            .publisher_default_group_order = publisher_default_group_order,
                                            .delivery_timeout = std::chrono::milliseconds{ delivery_timeout },
                                            .expires = std::chrono::milliseconds{ delivery_timeout },
                                            .filter = filter,
                                            .forward = forward,
                                            .new_group_request_id = new_group_request_id,
                                            .is_publisher_initiated = false,
                                            .start_location = {},
                                            .rendezvous_timeout = rendezvous_timeout,
                                          })
                      .Resolve(quic_transport_, [=, this, self = GetSharedPtr()](const auto& result) {
                          if (!self->IsCurrentRequestStream(request_id, stream)) {
                              return;
                          }

                          if (!result) {
                              const auto& [code, reason, redirect] = result.error();

                              // TODO: Should server not send if publisher initiated?
                              self->SendRequestError(stream,
                                                     request_id,
                                                     ToErrorCode(code),
                                                     0ms,
                                                     reason.value_or("Internal error"),
                                                     true,
                                                     redirect);

                              return;
                          }

                          // TODO: These should be set by the callback recipient, but preserving existing defaults for
                          // now. Should remove once laps updated.
                          SubscribeResponse response = result.value();
                          if (response.expires == std::nullopt) {
                              response.expires = std::chrono::milliseconds(kSubscribeExpires);
                          }
                          if (response.track_properties.Get<std::uint64_t>(ExtensionType::kDefaultPublisherPriority) ==
                              std::nullopt) {
                              response.track_properties.Add(ExtensionType::kDefaultPublisherPriority, 1);
                          }
                          if (response.track_properties.Get<bool>(ExtensionType::kDynamicGroups) == std::nullopt) {
                              response.track_properties.Add(ExtensionType::kDynamicGroups, true);
                          }

                          if (self->client_mode_) {
                              self->SendSubscribeOk(stream, request_id, th.track_fullname_hash, response);
                          } else {

                              // Save the latest state for joining fetch.
                              auto req_it = self->recv_req_id.find(request_id);
                              if (req_it == self->recv_req_id.end()) {
                                  QUICR_LOGGER_WARN(self->logger_,
                                                    "Resolve subscribe has no request_id: {} conn_id: {} "
                                                    "track_alias: {}",
                                                    request_id,
                                                    stream->GetConnectionID(),
                                                    th.track_fullname_hash);
                                  return;
                              }

                              req_it->second.largest_location = result->largest_location;

                              if (!result->is_publisher_initiated) {
                                  self->SendSubscribeOk(stream, request_id, th.track_fullname_hash, response);
                              }
                          }

                          if (!new_group_request_id.has_value()) {
                              return;
                          }

                          callbacks->NewGroupRequested(tfn, *new_group_request_id)
                            .Resolve(quic_transport_, [=](const auto& new_group_result) {
                                if (new_group_result) {
                                    return;
                                }

                                const auto& [code, reason, _] = new_group_result.error();
                                QUICR_LOGGER_ERROR(self->logger_,
                                                   "New group request on subscribe failed request_id: {} group_id: {} "
                                                   "code: {} reason: {}",
                                                   request_id,
                                                   *new_group_request_id,
                                                   static_cast<std::uint64_t>(code),
                                                   reason.value_or("unknown"));
                            });
                      });
                }

                return true;
            }
            case messages::ControlMessageType::kSubscribeOk: {
                const auto stream_request_id = FindRequestIdByStream(*stream);
                if (!stream_request_id.has_value()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received SUBSCRIBE_OK for unknown request conn_id: {} stream_id: {}, ignored",
                                      connection->GetID(),
                                      stream->GetStreamId());
                    return true;
                }
                const auto request_id = *stream_request_id;

                const auto track_alias = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);
                const auto track_extensions = messages::Message::ParseField<messages::TrackExtensions>(msg_bytes);
                CompleteMigration(connection, request_id, MigrationRequestType::kSubscribe, track_alias);

                std::shared_ptr<TrackHandler> track_handler;
                {
                    std::lock_guard lock(state_mutex_);
                    track_handler = GetRequestHandler(FindRequest({ connection->GetID(), request_id }));
                }

                if (!track_handler) {
                    QUICR_LOGGER_WARN(
                      logger_,
                      "Received subscribe ok to unknown subscribe track conn_id: {} request_id: {}, ignored",
                      connection->GetID(),
                      request_id);
                    return true;
                }

                if (auto sub_handler = track_handler->Get<SubscribeTrackHandler>()) {
                    const auto publisher_default_group_order =
                      track_extensions.Get<messages::GroupOrder>(messages::ExtensionType::kDefaultPublisherGroupOrder)
                        .value_or(messages::GroupOrder::kAscending);

                    if (client_mode_) {
                        if (auto largest =
                              parameters.Get<messages::Location>(messages::ParameterType::kLargestObject)) {
                            sub_handler->SetLatestLocation(*largest);
                        }
                        sub_handler->SupportNewGroupRequest(true);
                    }

                    sub_handler->SetReceivedTrackAlias(track_alias);
                    sub_handler->SetPublisherDefaultGroupOrder(publisher_default_group_order);
                    sub_handler->SetStatus(SubscribeTrackHandler::Status::kOk);
                    sub_by_recv_track_alias[{ connection->GetID(), track_alias }] = sub_handler;
                }

                return true;
            }
            case messages::ControlMessageType::kRequestOk: {
                // What request is this for?
                const auto stream_request_id = FindRequestIdByStream(*stream);
                if (!stream_request_id.has_value()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received REQUEST_OK for unknown request conn_id: {} stream_id: {}, ignored",
                                      connection->GetID(),
                                      stream->GetStreamId());
                    return true;
                }
                const auto request_id = *stream_request_id;

                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);
                std::shared_ptr<TrackHandler> track_handler;
                {
                    std::lock_guard lock(state_mutex_);
                    track_handler = GetRequestHandler(FindResponseRequest(*connection, request_id));
                }

                if (!track_handler) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received REQUEST_OK to unknown track conn_id: {} request_id: {}, ignored",
                                      connection->GetID(),
                                      request_id);
                    return true;
                }

                if (auto status_handler = track_handler->Get<TrackStatusHandler>()) {
                    TrackStatusResponse response = {
                        .largest_location = parameters.Get<messages::Location>(messages::ParameterType::kLargestObject),
                        .track_properties = Message::ParseField<messages::TrackExtensions>(msg_bytes),
                    };
                    {
                        std::lock_guard lock(state_mutex_);
                        if (const auto request = FindRequestForHandler(*status_handler)) {
                            RemoveRequest(request);
                        }
                    }

                    status_handler->ResponseReceived(std::move(response));
                    return true;
                }

                const auto migration_type = track_handler->Get<PublishTrackHandler>() ? MigrationRequestType::kPublish
                                            : track_handler->Get<SubscribeNamespaceHandler>()
                                              ? MigrationRequestType::kSubscribeNamespace
                                              : MigrationRequestType::kPublishNamespace;
                CompleteMigration(connection, request_id, migration_type);

                const auto request = track_handler->GetRequest();
                const auto request_stream = request ? request->GetStream() : nullptr;
                if (!request_stream || request_stream->GetConnection() != connection) {
                    QUICR_LOGGER_DEBUG(logger_,
                                       "Ignoring late REQUEST_OK from retired conn_id: {} request_id: {}",
                                       connection->GetID(),
                                       request_id);
                    return true;
                }
                track_handler->RequestOkReceived(parameters);
                return true;
            }
            case messages::ControlMessageType::kGoaway: {
                const auto stream_request_id = FindRequestIdByStream(*stream);
                if (!stream_request_id.has_value()) {
                    throw ProtocolViolationException("GOAWAY received on an unknown request stream");
                }

                const std::string new_session_uri = [&] {
                    const auto uri = messages::Message::ParseField<Bytes>(msg_bytes);
                    return std::string(uri.begin(), uri.end());
                }();
                const auto timeout = messages::Message::ParseField<std::uint64_t>(msg_bytes);

                if (!msg_bytes.empty()) {
                    throw ProtocolViolationException("Request-stream GOAWAY contained a Request ID");
                }

                if (new_session_uri.size() > detail::kMaxUriLength) {
                    throw ProtocolViolationException(
                      quicr::format("GOAWAY URI exceeds {} bytes", detail::kMaxUriLength));
                }

                if (stream->RecordGoAwayReceived()) {
                    throw ProtocolViolationException(
                      quicr::format("Duplicate GOAWAY received on stream (stream_id={})", stream->GetStreamId()));
                }

                if (!client_mode_) {
                    if (!new_session_uri.empty()) {
                        throw ProtocolViolationException("Client GOAWAY included a new session URI");
                    }
                    return true;
                }

                const auto redirect_uri = new_session_uri.empty() ? client_config_.connect_uri : new_session_uri;
                QUICR_LOGGER_INFO(logger_,
                                  "Received request GOAWAY request_id: {} new session URI: {} timeout_ms: {}",
                                  *stream_request_id,
                                  redirect_uri,
                                  timeout);

                std::shared_ptr<TrackHandler> handler;
                {
                    std::lock_guard lock(state_mutex_);
                    handler = GetRequestHandler(FindRequest({ connection->GetID(), *stream_request_id }));
                }

                if (!handler) {
                    QUICR_LOGGER_DEBUG(logger_,
                                       "Ignoring late request GOAWAY conn_id: {} request_id: {}",
                                       connection->GetID(),
                                       *stream_request_id);
                    return true;
                }

                Migrate(connection, { .uri = redirect_uri, .request_id = *stream_request_id });
                return true;
            }
            case messages::ControlMessageType::kRequestError: {
                const auto stream_request_id = FindRequestIdByStream(*stream);
                if (!stream_request_id.has_value()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received REQUEST_ERROR for unknown request conn_id: {} stream_id: {}, ignored",
                                      connection->GetID(),
                                      stream->GetStreamId());
                    return true;
                }
                const auto request_id = *stream_request_id;
                const auto error_code = messages::Message::ParseField<ErrorCode>(msg_bytes);
                [[maybe_unused]] const auto retry_interval = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto error_reason = messages::Message::ParseField<Bytes>(msg_bytes);

                std::optional<Redirect> redirect;
                if (error_code == ErrorCode::kRedirect) {
                    const auto uri_bytes = messages::Message::ParseField<Bytes>(msg_bytes);
                    const auto redirect_uri = std::string(uri_bytes.begin(), uri_bytes.end());
                    const auto redirect_namespace = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                    const auto redirect_name = messages::Message::ParseField<Bytes>(msg_bytes);

                    if (!msg_bytes.empty()) {
                        throw ProtocolViolationException("REQUEST_ERROR has trailing bytes after Redirect");
                    }

                    if (redirect_uri.size() > detail::kMaxUriLength) {
                        throw ProtocolViolationException(
                          quicr::format("REQUEST_ERROR redirect URI exceeds {} bytes", detail::kMaxUriLength));
                    }

                    if (!client_mode_ && !redirect_uri.empty()) {
                        throw ProtocolViolationException("Client REQUEST_ERROR included a redirect URI");
                    }

                    redirect = {
                        .uri = redirect_uri.empty() && client_mode_ ? client_config_.connect_uri : redirect_uri,
                        .track_namespace = redirect_namespace,
                        .track_name = redirect_name,
                        .request_id = request_id,
                    };
                } else if (!msg_bytes.empty()) {
                    throw ProtocolViolationException("REQUEST_ERROR Redirect requires REDIRECT error code");
                }

                std::string reason_str(error_reason.begin(), error_reason.end());
                std::shared_ptr<TrackHandler> track_handler;
                {
                    std::lock_guard lock(state_mutex_);
                    track_handler = GetRequestHandler(FindResponseRequest(*connection, request_id));
                }
                const bool migration_failed = FailMigration(connection, request_id);

                if (redirect.has_value() && client_mode_) {
                    const auto request = track_handler ? track_handler->GetRequest() : nullptr;
                    const auto request_stream = request ? request->GetStream() : nullptr;
                    const auto source_connection = request_stream ? request_stream->GetConnection() : nullptr;
                    if (!source_connection || (!migration_failed && source_connection != connection)) {
                        QUICR_LOGGER_DEBUG(logger_,
                                           "Ignoring late REQUEST_ERROR redirect conn_id: {} request_id: {}",
                                           connection->GetID(),
                                           request_id);
                        return true;
                    }
                    QUICR_LOGGER_INFO(logger_,
                                      "Received REQUEST_ERROR redirect conn_id: {} request_id: {} uri: {}",
                                      connection->GetID(),
                                      request_id,
                                      redirect->uri);
                    Migrate(source_connection, *redirect);
                    return true;
                }

                if (migration_failed) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Migration request rejected conn_id: {} request_id: {} reason: {}",
                                      connection->GetID(),
                                      request_id,
                                      reason_str);
                    return true;
                }

                if (auto status_handler = track_handler ? track_handler->Get<TrackStatusHandler>() : nullptr) {
                    {
                        std::lock_guard lock(state_mutex_);
                        if (const auto request = FindRequestForHandler(*status_handler)) {
                            RemoveRequest(request);
                        }
                    }

                    status_handler->RequestError(error_code, reason_str);
                    return true;
                }

                if (client_mode_) {
                    if (!track_handler) {
                        QUICR_LOGGER_WARN(logger_,
                                          "Received REQUEST_ERROR to unknown track conn_id: {} request_id: {}, ignored",
                                          connection->GetID(),
                                          request_id);
                        return true;
                    }
                    const auto request = track_handler->GetRequest();
                    const auto request_stream = request ? request->GetStream() : nullptr;
                    if (!request_stream || request_stream->GetConnection() != connection) {
                        QUICR_LOGGER_DEBUG(logger_,
                                           "Ignoring late REQUEST_ERROR from retired conn_id: {} request_id: {}",
                                           connection->GetID(),
                                           request_id);
                        return true;
                    }

                    track_handler->RequestError(error_code, reason_str);
                    return true;
                }

                QUICR_LOGGER_DEBUG(logger_,
                                   "Received track status error request_id: {} error code: {} reason: {}",
                                   request_id,
                                   static_cast<std::uint64_t>(error_code),
                                   reason_str);
                return true;
            }
            case messages::ControlMessageType::kTrackStatus: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto track_namespace = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                const auto track_name = messages::Message::ParseField<Bytes>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                auto tfn = FullTrackName{ track_namespace, track_name };

                auto th = TrackHash(tfn);
                QUICR_LOGGER_DEBUG(logger_,
                                   "Received track status request_id: {} for full name hash: {}",
                                   request_id,
                                   th.track_fullname_hash);

                {
                    std::lock_guard _(state_mutex_);
                    request_by_stream[{ stream->GetConnectionID(), stream->GetStreamId() }] = {
                        .request_id = request_id, .is_request_stream = true
                    };
                }

                if (callbacks_) {
                    callbacks_->TrackStatusReceived(GetSharedPtr(), tfn)
                      .Resolve(quic_transport_, [=, self = GetSharedPtr()](const auto& result) {
                          if (!self->IsCurrentRequestStream(request_id, stream)) {
                              return;
                          }

                          if (!result) {
                              const auto& [code, reason, redirect] = result.error();

                              switch (code) {
                                  case RequestErrorCode::kDoesNotExist:
                                      self->SendRequestError(stream,
                                                             request_id,
                                                             ErrorCode::kDoesNotExist,
                                                             0ms, // TODO: Figure out retry interval
                                                             reason.value_or("Track does not exist"),
                                                             true,
                                                             redirect);
                                      break;
                                  case RequestErrorCode::kUnauthorized:
                                      self->SendRequestError(stream,
                                                             request_id,
                                                             ErrorCode::kUnauthorized,
                                                             0ms, // TODO: Figure out retry interval
                                                             reason.value_or("Unauthorized"),
                                                             true,
                                                             redirect);
                                      break;
                                  default:
                                      self->SendRequestError(stream,
                                                             request_id,
                                                             ErrorCode::kInternalError,
                                                             0ms,
                                                             "Internal error",
                                                             true,
                                                             redirect);
                                      break;
                              }

                              return;
                          }

                          self->SendTrackStatusOk(stream, result.value());
                      });
                }

                return true;
            }
            case messages::ControlMessageType::kPublishNamespace: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto track_namespace = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                [[maybe_unused]] const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                {
                    std::lock_guard _(state_mutex_);
                    recv_req_id[request_id] = { .track_full_name = { track_namespace, {} },
                                                .track_hash = TrackHash({ track_namespace, {} }),
                                                .stream = stream };
                    recv_publish_namespaces.push_back(request_id);
                    request_by_stream[{ stream->GetConnectionID(), stream->GetStreamId() }] = {
                        .request_id = request_id, .is_request_stream = true
                    };
                }

                if (callbacks_) {
                    callbacks_->PublishNamespaceReceived(GetSharedPtr(), track_namespace, { .request_id = request_id })
                      .Resolve(quic_transport_, [=, self = GetSharedPtr()](const auto& result) {
                          if (!self->IsCurrentRequestStream(request_id, stream)) {
                              return;
                          }

                          if (!result) {
                              const auto& [code, reason, redirect] = result.error();
                              self->SendRequestError(stream,
                                                     request_id,
                                                     ToErrorCode(code),
                                                     0ms,
                                                     reason.value_or("Internal error"),
                                                     true,
                                                     redirect);
                              return;
                          }

                          auto response_stream = stream;
                          std::shared_ptr<TrackHandler> handler;
                          {
                              std::lock_guard lock(self->state_mutex_);
                              handler = GetRequestHandler(self->FindRequest({ stream->GetConnectionID(), request_id }));
                          }

                          if (handler) {
                              if (const auto request = handler->GetRequest()) {
                                  if (const auto request_stream = request->GetStream()) {
                                      response_stream = request_stream;
                                  }
                              }
                          }

                          self->SendPublishNamespaceOk(response_stream);

                          const auto sub_stream = self->FindSubscribeNamespaceStream(track_namespace);
                          if (sub_stream == nullptr) {
                              QUICR_LOGGER_WARN(self->logger_,
                                                "No subscribe namespace stream for publish namespace conn_id: {}",
                                                stream->GetConnectionID());
                              return;
                          }

                          self->SendPublishNamespace(sub_stream, self->GetNextRequestID(), track_namespace);
                      });
                }

                return true;
            }
            case messages::ControlMessageType::kSubscribeTracks:
                [[fallthrough]];
            case messages::ControlMessageType::kSubscribeNamespace: {
                if (client_mode_) {
                    QUICR_LOGGER_ERROR(
                      logger_, "Unsupported MOQT message type: {}, bad stream", static_cast<uint64_t>(msg_type));
                    return false;
                }

                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto track_namespace_prefix = messages::Message::ParseField<TrackNamespace>(msg_bytes);

                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                messages::Filter filter;
                if (parameters.Contains(messages::ParameterType::kSubscriptionFilter)) {
                    filter = parameters.GetFilter(messages::FilterType::kSubscriptionFilter);
                } else if (parameters.Contains(messages::ParameterType::kTrackFilter)) {
                    filter = parameters.GetFilter(messages::FilterType::kTrackFilter);
                }

                const bool subscribe_namespace = msg_type == messages::ControlMessageType::kSubscribeNamespace;
                {
                    std::lock_guard _(state_mutex_);
                    request_by_stream[{ stream->GetConnectionID(), stream->GetStreamId() }] = {
                        .request_id = request_id, .is_request_stream = true
                    };
                }

                if (auto callbacks = std::dynamic_pointer_cast<ServerCallbacks>(callbacks_)) {
                    // Record the subscription before the application callback. A deferred reply can
                    // outlive the request stream, and CloseRequestHandler can only clean it up if it
                    // is already registered.
                    if (subscribe_namespace) {
                        std::lock_guard _(state_mutex_);
                        recv_req_id[request_id] = { .track_full_name = { track_namespace_prefix, {} },
                                                    .track_hash = TrackHash({ track_namespace_prefix, {} }),
                                                    .stream = stream };
                        recv_subscribe_namespaces.push_back(request_id);
                    }

                    const SubscribeNamespaceAttributes attributes{
                        .request_id = request_id,
                        .filter_type = messages::FilterType::kTrackFilter,
                        .filter = filter,
                    };

                    // SUBSCRIBE_TRACKS and SUBSCRIBE_NAMESPACE share parsing above, but must be
                    // dispatched to their own distinct callback.
                    auto reply =
                      (msg_type == messages::ControlMessageType::kSubscribeTracks)
                        ? callbacks->SubscribeTracksReceived(GetSharedPtr(), track_namespace_prefix, attributes)
                        : callbacks->SubscribeNamespaceReceived(GetSharedPtr(), track_namespace_prefix, attributes);

                    reply.Resolve(quic_transport_, [=, self = GetSharedPtr()](const auto& result) {
                        if (!self->IsCurrentRequestStream(request_id, stream)) {
                            return;
                        }

                        if (!result) {
                            if (subscribe_namespace) {
                                std::lock_guard _(self->state_mutex_);
                                std::erase(self->recv_subscribe_namespaces, request_id);
                                self->recv_req_id.erase(request_id);
                            }
                            const auto& [code, reason, redirect] = result.error();
                            self->SendRequestError(stream,
                                                   request_id,
                                                   ToErrorCode(code),
                                                   0ms,
                                                   reason.value_or("Internal error"),
                                                   true,
                                                   redirect);

                            return;
                        }

                        if (subscribe_namespace) {
                            std::lock_guard _(self->state_mutex_);
                            const auto still_subscribed =
                              std::find(self->recv_subscribe_namespaces.begin(),
                                        self->recv_subscribe_namespaces.end(),
                                        request_id) != self->recv_subscribe_namespaces.end();
                            if (!still_subscribed) {
                                return;
                            }
                        }

                        self->SendSubscribeNamespaceOk(stream);

                        // TODO: Could argue that this should be left to a relay implementation?
                        for (const auto& name_space : result.value()) {
                            const auto match = track_namespace_prefix.IsPrefixOf(name_space);
                            if (match == std::partial_ordering::unordered || match == std::partial_ordering::greater) {
                                QUICR_LOGGER_WARN(self->logger_, "Dropping non prefix match");
                                continue;
                            }

                            if (subscribe_namespace) {
                                const auto suffix_size =
                                  name_space.GetEntries().size() - track_namespace_prefix.GetEntries().size();
                                const auto suffix = name_space.GetSuffix(suffix_size);
                                self->SendCtrlMsg(stream, messages::ControlMessageType::kNamespace, suffix);
                            } else {
                                self->SendPublishNamespace(stream, self->GetNextRequestID(), name_space);
                            }
                        }
                    });
                }

                return true;
            }
            case messages::ControlMessageType::kNamespace: {
                const auto suffix = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                const auto stream_request_id = FindRequestIdByStream(*stream);
                if (!stream_request_id.has_value()) {
                    throw ProtocolViolationException("NAMESPACE on an unknown request stream");
                }

                std::shared_ptr<TrackHandler> request_handler;
                {
                    std::lock_guard lock(state_mutex_);
                    request_handler = GetRequestHandler(FindRequest({ connection->GetID(), *stream_request_id }));
                }

                if (!request_handler) {
                    throw ProtocolViolationException("NAMESPACE on an unknown request");
                }

                const auto handler = request_handler->Get<SubscribeNamespaceHandler>();
                if (!handler || handler->GetMode() != SubscribeNamespaceHandler::Mode::kNamespaces) {
                    throw ProtocolViolationException("NAMESPACE on a non-namespace request");
                }

                handler->NamespaceReceived(suffix);
                return true;
            }
            case messages::ControlMessageType::kNamespaceDone: {
                const auto track_namespace = messages::Message::ParseField<TrackNamespace>(msg_bytes);

                if (client_mode_) {
                    const auto stream_request_id = FindRequestIdByStream(*stream);
                    if (!stream_request_id.has_value()) {
                        throw ProtocolViolationException("NAMESPACE_DONE on an unknown request stream");
                    }

                    std::shared_ptr<TrackHandler> request_handler;
                    {
                        std::lock_guard lock(state_mutex_);
                        request_handler = GetRequestHandler(FindRequest({ connection->GetID(), *stream_request_id }));
                    }

                    if (!request_handler) {
                        throw ProtocolViolationException("NAMESPACE_DONE on an unknown request");
                    }

                    const auto handler = request_handler->Get<SubscribeNamespaceHandler>();
                    if (!handler || handler->GetMode() != SubscribeNamespaceHandler::Mode::kNamespaces) {
                        throw ProtocolViolationException("NAMESPACE_DONE on a non-namespace request");
                    }

                    handler->NamespaceDoneReceived(track_namespace);
                    return true;
                }

                // Client unsubscribe. Drop the subscription so later NAMESPACE notifications are
                // not sent on a request the subscriber has already removed.
                if (const auto request_id = FindRequestIdByStream(*stream)) {
                    std::lock_guard _(state_mutex_);
                    std::erase(recv_subscribe_namespaces, *request_id);
                    recv_req_id.erase(*request_id);
                }

                if (auto callbacks = std::dynamic_pointer_cast<ServerCallbacks>(callbacks_)) {
                    callbacks->UnsubscribeNamespaceReceived(GetSharedPtr(), track_namespace);
                }
                return true;
            }
            case messages::ControlMessageType::kPublishDone: {
                const auto request_id = FindRequestIdByStream(*stream);

                if (!request_id.has_value()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received publish done to unknown stream to request_id conn_id: {}",
                                      connection->GetID());
                    break;
                }

                [[maybe_unused]] const auto status_code =
                  messages::Message::ParseField<messages::PublishDoneStatusCode>(msg_bytes);
                const auto stream_count = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                [[maybe_unused]] const auto error_reason = messages::Message::ParseField<Bytes>(msg_bytes);

                std::shared_ptr<TrackHandler> track_handler;
                {
                    std::lock_guard lock(state_mutex_);
                    track_handler = GetRequestHandler(FindRequest({ connection->GetID(), *request_id }));
                }

                if (!track_handler) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received publish done to unknown request_id conn_id: {} request_id: {}",
                                      connection->GetID(),
                                      *request_id);
                    return true;
                }

                if (client_mode_) {
                    if (auto h = track_handler->Get<SubscribeTrackHandler>()) {
                        h->SetPublishDoneStreamCount(stream_count);
                        h->SetStatus(SubscribeTrackHandler::Status::kNotSubscribed);
                    }
                    return true;
                }

                auto tfn = track_handler->GetFullTrackName();
                auto th = TrackHash(tfn);

                QUICR_LOGGER_INFO(logger_,
                                  "Received publish done conn_id: {} request_id: {} track namespace hash: {} "
                                  "name hash: {} track alias: {}",
                                  connection->GetID(),
                                  *request_id,
                                  th.track_namespace_hash,
                                  th.track_name_hash,
                                  th.track_fullname_hash);

                if (auto h = track_handler->Get<SubscribeTrackHandler>()) {
                    h->SetPublishDoneStreamCount(stream_count);
                    h->SetStatus(SubscribeTrackHandler::Status::kNotSubscribed);
                    if (auto callbacks = std::dynamic_pointer_cast<ServerCallbacks>(callbacks_)) {
                        callbacks->PublishDoneReceived(GetSharedPtr(), *request_id);
                    }
                }

                recv_req_id.erase(*request_id);
                return true;
            }
            case messages::ControlMessageType::kFetchOk: {
                [[maybe_unused]] const auto end_of_track = messages::Message::ParseField<std::uint8_t>(msg_bytes);
                const auto end_location = messages::Message::ParseField<messages::Location>(msg_bytes);
                [[maybe_unused]] const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);
                const auto track_extensions = messages::Message::ParseField<messages::TrackExtensions>(msg_bytes);

                const auto stream_request_id = FindRequestIdByStream(*stream);
                if (!stream_request_id.has_value()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received FETCH_OK for unknown request conn_id: {} stream_id: {}, ignored",
                                      connection->GetID(),
                                      stream->GetStreamId());
                    return true;
                }
                const auto request_id = *stream_request_id;
                CompleteMigration(connection, request_id, MigrationRequestType::kFetch);

                std::shared_ptr<TrackHandler> track_handler;
                {
                    std::lock_guard lock(state_mutex_);
                    track_handler = GetRequestHandler(FindRequest({ connection->GetID(), request_id }));
                }

                if (!track_handler) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received fetch ok for unknown fetch track conn_id: {} request_id: {}, ignored",
                                      connection->GetID(),
                                      request_id);
                    return true;
                }

                if (auto h = track_handler->Get<FetchTrackHandler>()) {
                    const auto publisher_default_group_order =
                      track_extensions.Get<messages::GroupOrder>(messages::ExtensionType::kDefaultPublisherGroupOrder)
                        .value_or(messages::GroupOrder::kAscending);
                    h->SetLatestLocation(end_location);
                    h->SetPublisherDefaultGroupOrder(publisher_default_group_order);
                    h->SetStatus(FetchTrackHandler::Status::kOk);
                }

                return true;
            }
            case messages::ControlMessageType::kFetch: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto fetch_type = messages::Message::ParseField<messages::FetchType>(msg_bytes);

                bool relative_joining{ false };

                switch (fetch_type) {
                    case messages::FetchType::kStandalone: {
                        const auto track_namespace = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                        const auto track_name = messages::Message::ParseField<Bytes>(msg_bytes);
                        const auto start = messages::Message::ParseField<messages::Location>(msg_bytes);
                        const auto end = messages::Message::ParseField<messages::Location>(msg_bytes);
                        const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                        FullTrackName tfn{ track_namespace, track_name };
                        const auto th = TrackHash(tfn);

                        {
                            std::lock_guard _(state_mutex_);
                            recv_req_id[request_id] = {
                                .track_full_name = tfn,
                                .track_hash = th,
                                .stream = stream,
                            };
                            request_by_stream[{ stream->GetConnectionID(), stream->GetStreamId() }] = {
                                .request_id = request_id, .is_request_stream = true
                            };
                        }

                        messages::FetchEndLocation end_location;
                        end_location.group = end.group;
                        if (end.object == 0) {
                            end_location.object = std::nullopt;
                        } else {
                            end_location.object = end.object - 1;
                        }

                        auto priority =
                          parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority).value_or(128);
                        auto group_order =
                          messages::ResolveGroupOrder(parameters).value_or(messages::GroupOrder::kAscending);

                        if (callbacks_) {
                            StandaloneFetchAttributes attrs = {
                                .priority = priority,
                                .group_order = group_order,
                                .publisher_default_group_order = messages::GroupOrder::kAscending,
                                .start_location = start,
                                .end_location = end_location,
                            };

                            callbacks_->StandaloneFetchReceived(GetSharedPtr(), request_id, tfn, attrs)
                              .Resolve(quic_transport_, [=, self = GetSharedPtr()](const auto& result) {
                                  if (!self->IsCurrentRequestStream(request_id, stream)) {
                                      return;
                                  }

                                  if (!result) {
                                      const auto& [code, reason, redirect] = result.error();

                                      ErrorCode error_code = ErrorCode::kInternalError;
                                      switch (code) {
                                          case FetchErrorCode::kInvalidRange:
                                              error_code = ErrorCode::kInvalidRange;
                                              break;

                                          case FetchErrorCode::kRedirect:
                                              error_code = ErrorCode::kRedirect;
                                              break;

                                          default:
                                              break;
                                      }

                                      self->SendRequestError(stream,
                                                             request_id,
                                                             error_code,
                                                             0ms,
                                                             reason.value_or("Internal error"),
                                                             true,
                                                             redirect);

                                      return;
                                  }

                                  self->ResolveFetch(request_id, group_order, result.value());
                              });
                        }

                        return true;
                    }
                    case messages::FetchType::kRelativeJoiningFetch: {
                        relative_joining = true;
                        [[fallthrough]];
                    }
                    case messages::FetchType::kAbsoluteJoiningFetch: {
                        const auto joining_request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                        const auto joining_start = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                        const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                        const auto subscribe_state = recv_req_id.find(joining_request_id);
                        if (subscribe_state == recv_req_id.end()) {
                            SendRequestError(stream,
                                             request_id,
                                             ErrorCode::kDoesNotExist,
                                             0ms,
                                             "Corresponding subscribe does not exist");
                            return true;
                        }

                        FullTrackName tfn = subscribe_state->second.track_full_name;
                        const auto th = TrackHash(tfn);

                        {
                            std::lock_guard _(state_mutex_);
                            recv_req_id[request_id] = {
                                .track_full_name = tfn,
                                .track_hash = th,
                                .stream = stream,
                            };
                            request_by_stream[{ stream->GetConnectionID(), stream->GetStreamId() }] = {
                                .request_id = request_id, .is_request_stream = true
                            };
                        }

                        auto priority =
                          parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority).value_or(128);
                        auto group_order =
                          messages::ResolveGroupOrder(parameters).value_or(messages::GroupOrder::kAscending);

                        if (callbacks_) {
                            JoiningFetchAttributes attrs = {
                                .priority = priority,
                                .group_order = group_order,
                                .publisher_default_group_order = messages::GroupOrder::kAscending,
                                .joining_request_id = joining_request_id,
                                .relative = relative_joining,
                                .joining_start = joining_start,
                            };

                            callbacks_->JoiningFetchReceived(GetSharedPtr(), request_id, tfn, attrs)
                              .Resolve(quic_transport_, [=, self = GetSharedPtr()](const auto& result) {
                                  if (!self->IsCurrentRequestStream(request_id, stream)) {
                                      return;
                                  }

                                  if (!result) {
                                      const auto& [code, reason, redirect] = result.error();

                                      ErrorCode error_code = ErrorCode::kInternalError;
                                      switch (code) {
                                          case FetchErrorCode::kInvalidRange:
                                              error_code = ErrorCode::kInvalidRange;
                                              break;

                                          case FetchErrorCode::kRedirect:
                                              error_code = ErrorCode::kRedirect;
                                              break;

                                          default:
                                              break;
                                      }

                                      self->SendRequestError(stream,
                                                             request_id,
                                                             error_code,
                                                             0ms,
                                                             reason.value_or("Internal error"),
                                                             true,
                                                             redirect);

                                      return;
                                  }

                                  self->ResolveFetch(request_id, group_order, result.value());
                              });
                        }
                        return true;
                    }
                    default: {
                        SendRequestError(stream, request_id, ErrorCode::kNotSupported, 0ms, "Unknown fetch type");
                        return true;
                    }
                }
            }
            case messages::ControlMessageType::kPublish: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto track_namespace = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                const auto track_name = messages::Message::ParseField<Bytes>(msg_bytes);
                const auto track_alias = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);
                const auto track_extensions = messages::Message::ParseField<messages::TrackExtensions>(msg_bytes);

                ValidateParameters(parameters,
                                   { ParameterType::kAuthorizationToken,
                                     ParameterType::kExpires,
                                     ParameterType::kLargestObject,
                                     ParameterType::kForward });
                const auto default_publisher_group_order = ResolveDefaultPublisherGroupOrder(track_extensions);
                const auto dynamic_groups = ResolveDynamicGroups(track_extensions);
                const auto default_publisher_priority = ResolveDefaultPublisherPriority(track_extensions);
                const auto max_cache_duration = ResolveMaxCacheDuration(track_extensions);
                const auto delivery_timeout = ResolveDeliveryTimeout(track_extensions);
                const PublishAttributes publish{ .track_full_name = { track_namespace, track_name },
                                                 .track_alias = track_alias,
                                                 .auth_tokens = CollectAuthTokens(parameters),
                                                 .expires = ResolveExpires(parameters),
                                                 .largest_object =
                                                   parameters.Get<Location>(messages::ParameterType::kLargestObject),
                                                 .forward = ResolveForward(parameters, true),
                                                 .default_publisher_group_order = default_publisher_group_order,
                                                 .dynamic_groups = dynamic_groups,
                                                 .publisher_priority = default_publisher_priority,
                                                 .max_cache_duration = max_cache_duration,
                                                 .delivery_timeout = delivery_timeout,
                                                 .track_properties = std::move(track_extensions) };

                auto th = TrackHash(publish.track_full_name);
                {
                    std::lock_guard _(state_mutex_);
                    recv_req_id[request_id] = { .track_full_name = publish.track_full_name,
                                                .track_hash = th,
                                                .stream = stream };
                    request_by_stream[{ stream->GetConnectionID(), stream->GetStreamId() }] = {
                        .request_id = request_id, .is_request_stream = true
                    };
                }

                std::weak_ptr<SubscribeNamespaceHandler> sub_ns_handler;
                if (client_mode_) {
                    std::lock_guard lock(state_mutex_);
                    const auto begin = requests.lower_bound({ connection->GetID(), 0 });
                    const auto end =
                      requests.upper_bound({ connection->GetID(), std::numeric_limits<std::uint64_t>::max() });
                    for (auto request = begin; request != end; ++request) {
                        const auto handler = GetRequestHandler(request->second);
                        if (auto h = handler ? handler->Get<SubscribeNamespaceHandler>() : nullptr) {
                            if (h->GetPrefix().HasSamePrefix(publish.track_full_name.name_space)) {
                                sub_ns_handler = h;
                                break;
                            }
                        }
                    }
                }

                if (callbacks_) {
                    callbacks_->PublishReceived(GetSharedPtr(), request_id, publish, sub_ns_handler)
                      .Resolve(quic_transport_, [=, self = GetSharedPtr()](const auto& result) {
                          if (!self->IsCurrentRequestStream(request_id, stream)) {
                              return;
                          }

                          if (!result) {
                              const auto& [code, reason, redirect] = result.error();

                              ErrorCode error_code;
                              switch (code) {
                                  case PublishErrorCode::kRejected:
                                      error_code = ErrorCode::kUninterested;
                                      break;

                                  case PublishErrorCode::kNotAuthorized:
                                      error_code = ErrorCode::kUnauthorized;
                                      break;

                                  case PublishErrorCode::kNotSupported:
                                      error_code = ErrorCode::kNotSupported;
                                      break;

                                  case PublishErrorCode::kInternalError:
                                      error_code = ErrorCode::kInternalError;
                                      break;
                              }

                              self->SendRequestError(
                                stream, request_id, error_code, 0ms, reason.value_or("unknown"), true, redirect);

                              return;
                          }

                          // A null handler means the application accepted the PUBLISH without retaining
                          // a subscriber. Publisher-initiated handlers are installed here rather than
                          // through SubscribeTrack, which only creates locally initiated SUBSCRIBE requests.
                          if (result->handler) {
                              const auto& handler = result->handler;
                              handler->SetPublishInitiated();
                              handler->SetReceivedTrackAlias(publish.track_alias);
                              handler->SetSubscriberPriority(publish.publisher_priority);
                              // TODO: Optional delivery timeout?
                              const std::uint64_t delivery_timeout_ms = publish.delivery_timeout.value_or(0);
                              handler->SetDeliveryTimeout(std::chrono::milliseconds(delivery_timeout_ms));
                              handler->SetPublisherDefaultGroupOrder(publish.default_publisher_group_order);
                              handler->SupportNewGroupRequest(publish.dynamic_groups);

                              if (!handler->GetTrackAlias().has_value()) {
                                  handler->SetTrackAlias(TrackHash(handler->GetFullTrackName()).track_fullname_hash);
                              }
                              handler->RegisterSession(self);

                              std::lock_guard lock(self->state_mutex_);
                              auto request = self->FindRequest({ stream->GetConnectionID(), request_id });
                              if (request) {
                                  if (request->type != Request::Type::kPublish || request->is_locally_initiated) {
                                      throw std::logic_error("Subscriber track cannot bind to this request");
                                  }
                              } else {
                                  request = std::make_shared<Request>(Request{
                                    .request_id = request_id,
                                    .type = Request::Type::kPublish,
                                    .stream = stream,
                                    .name = handler->GetFullTrackName(),
                                    .handlers = {},
                                    .is_locally_initiated = false,
                                  });
                                  self->AddRequest(request);
                              }

                              self->BindHandlerToRequest(request, handler);
                              self->sub_by_recv_track_alias[{ stream->GetConnectionID(), publish.track_alias }] =
                                handler;
                          }

                          self->SendPublishOk(stream, result->attributes);
                      });
                }

                return true;
            }
            case messages::ControlMessageType::kRequestUpdate: {
                const auto update_request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto stream_request_id = FindRequestIdByStream(*stream);
                if (!stream_request_id.has_value()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received REQUEST_UPDATE on unknown request stream conn_id: {} stream_id: {} "
                                      "update_request_id: {}, ignored",
                                      connection->GetID(),
                                      stream->GetStreamId(),
                                      update_request_id);
                    return true;
                }
                const auto request_id = *stream_request_id;
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                if (client_mode_) {
                    std::vector<std::shared_ptr<TrackHandler>> handlers;
                    {
                        std::lock_guard lock(state_mutex_);
                        if (const auto request = FindRequest({ connection->GetID(), request_id })) {
                            handlers = request->handlers;
                        }
                    }

                    if (handlers.empty()) {
                        QUICR_LOGGER_WARN(logger_,
                                          "Received REQUEST_UPDATE to unknown track conn_id: {} request_id: {}, "
                                          "ignored",
                                          connection->GetID(),
                                          request_id);
                        return true;
                    }

                    std::vector<Reply<messages::Parameters, ErrorCode>> replies;
                    replies.reserve(handlers.size());
                    for (const auto& handler : handlers) {
                        replies.push_back(handler->RequestUpdateReceived(parameters));
                    }

                    struct UpdateResult
                    {
                        std::mutex mutex;
                        std::size_t remaining = 0;
                        std::optional<Error<ErrorCode>> error;
                        std::optional<messages::Parameters> parameters;
                    };
                    const auto aggregate = std::make_shared<UpdateResult>();
                    aggregate->remaining = replies.size();

                    for (auto& reply : replies) {
                        reply.Resolve(
                          quic_transport_, [request_id, stream, aggregate, self = GetSharedPtr()](const auto& result) {
                              std::optional<Error<ErrorCode>> error;
                              std::optional<messages::Parameters> response_parameters;
                              {
                                  std::lock_guard lock(aggregate->mutex);
                                  if (!result) {
                                      if (!aggregate->error) {
                                          aggregate->error = result.error();
                                      }
                                  } else if (!aggregate->parameters) {
                                      aggregate->parameters = result.value();
                                  }

                                  if (--aggregate->remaining != 0) {
                                      return;
                                  }
                                  error = aggregate->error;
                                  response_parameters = aggregate->parameters;
                              }

                              std::lock_guard lock(self->state_mutex_);
                              const auto request = self->FindRequest({ stream->GetConnectionID(), request_id });
                              const auto request_stream = request ? request->GetStream() : nullptr;
                              if (request_stream != stream || !stream->IsOpen()) {
                                  QUICR_LOGGER_DEBUG(self->logger_,
                                                     "Ignoring stale REQUEST_UPDATE result conn_id: {} request_id: {}",
                                                     stream->GetConnectionID(),
                                                     request_id);
                                  return;
                              }

                              if (error) {
                                  self->SendRequestError(stream,
                                                         request_id,
                                                         error->reason_code,
                                                         0ms,
                                                         error->error_reason.value_or("Request update rejected"),
                                                         true,
                                                         error->redirect);
                                  return;
                              }

                              QUICR_LOGGER_DEBUG(self->logger_, "Request update resolved req_id: {}", request_id);
                              self->SendRequestOk(stream, *response_parameters);
                          });
                    }
                    return true;
                }

                auto sub_ctx_it = recv_req_id.find(request_id);
                if (sub_ctx_it == recv_req_id.end()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received subscribe_update for unknown subscription conn_id: {} request_id: {}",
                                      connection->GetID(),
                                      request_id);

                    SendRequestError(
                      stream, request_id, ErrorCode::kDoesNotExist, 0ms, "Subscription not found", false);
                    return true;
                }

                [[maybe_unused]] auto delivery_timeout =
                  parameters.Get<std::uint64_t>(messages::ParameterType::kDeliveryTimeout);
                [[maybe_unused]] auto priority = parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority);
                auto forward = parameters.Get<std::uint8_t>(messages::ParameterType::kForward);
                auto new_group_request_id = parameters.Get<std::uint64_t>(messages::ParameterType::kNewGroupRequest);

                if (new_group_request_id.has_value()) {
                    if (auto callbacks = std::dynamic_pointer_cast<ServerCallbacks>(callbacks_)) {
                        callbacks->NewGroupRequested(sub_ctx_it->second.track_full_name, new_group_request_id.value())
                          .Resolve(
                            quic_transport_,
                            [request_id, group_id = *new_group_request_id, self = GetSharedPtr()](const auto& result) {
                                if (result) {
                                    return;
                                }

                                const auto& [code, reason, _] = result.error();
                                QUICR_LOGGER_ERROR(self->logger_,
                                                   "New group request on update failed request_id: {} group_id: {} "
                                                   "code: {} reason: {}",
                                                   request_id,
                                                   group_id,
                                                   static_cast<std::uint64_t>(code),
                                                   reason.value_or("unknown"));
                            });
                    }
                }

                QUICR_LOGGER_DEBUG(logger_,
                                   "Received subscribe_update to recv request_id: {} forward: {} ngr: {}",
                                   request_id,
                                   forward.value_or(true),
                                   new_group_request_id.has_value());

                std::vector<std::pair<std::shared_ptr<PublishTrackHandler>, PublishTrackHandler::Status>> handlers;
                {
                    std::lock_guard _(state_mutex_);
                    const auto it = pub_tracks_by_track_alias.find(sub_ctx_it->second.track_hash.track_fullname_hash);
                    if (it != pub_tracks_by_track_alias.end()) {
                        for (const auto& [_, handler] : it->second) {
                            const bool forward_state = messages::ResolveForward(
                              parameters, handler->GetStatus() != PublishTrackHandler::Status::kPaused);
                            const auto status = !forward_state ? PublishTrackHandler::Status::kPaused
                                                : new_group_request_id.has_value()
                                                  ? PublishTrackHandler::Status::kNewGroupRequested
                                                  : PublishTrackHandler::Status::kOk;
                            if ((forward.has_value() || new_group_request_id.has_value()) &&
                                handler->UpdateStatus(status)) {
                                handlers.emplace_back(handler, status);
                            }
                        }
                    }
                }
                for (const auto& [handler, status] : handlers) {
                    handler->StatusChanged(status);
                }

                SendRequestUpdateOk(stream, std::nullopt, std::nullopt);
                return true;
            }
            default: {
                QUICR_LOGGER_ERROR(
                  logger_, "Unsupported MOQT message type: {}, bad stream", static_cast<uint64_t>(msg_type));
                return false;
            }
        }

        return false;
    }

} // namespace quicr
