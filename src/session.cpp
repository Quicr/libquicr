// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/session.h"
#include "quicr/connection.h"
#include "quicr/handlers/joining_fetch_handler.h"
#include "quicr/handlers/subscribe_namespace_handler.h"
#include "quicr/log.h"
#include "quicr/messages/ctrl_message_types.h"
#include "quicr/messages/message.h"
#include "quicr/messages/messages.h"
#include "quicr/messages/parameters.h"
#include "quicr/session_callbacks.h"
#include "stream.h"
#include "track_properties.h"
#include "transport_picoquic.h"

#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace quicr {
    using namespace quicr::messages;
    using namespace std::chrono_literals;

    constexpr uint64_t kSubscribeExpires = 0;  ///< Never expires
    constexpr int kReadLoopMaxPerStream = 100; ///< Support packet/frame bursts, but do not allow starving other streams

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
            }
        }

    }

    TransportException::TransportException(TransportError error, std::source_location location)
      : std::runtime_error("Error in transport (error=" + std::to_string(static_cast<int>(error)) + ", " +
                           std::to_string(location.line()) + ", " + location.file_name() + ")")
      , Error(error)
    {
    }

    static std::optional<std::tuple<std::string, uint16_t, TransportProtocol, std::string>> ParseConnectUri(
      const std::string& connect_uri)
    {
        // Support moq://, moqt://, https:// (for WebTransport)
        std::string proto;
        TransportProtocol transport_proto = TransportProtocol::kQuic;

        const std::string moq_proto = "moq://";
        const std::string moqt_proto = "moqt://";
        const std::string https_proto = "https://";

        auto it = connect_uri.begin();

        if (auto moq_it = std::search(it, connect_uri.end(), moq_proto.begin(), moq_proto.end());
            moq_it != connect_uri.end()) {
            proto = moq_proto;
            transport_proto = TransportProtocol::kQuic;
            it = moq_it;
        } else if (auto moqt_it = std::search(it, connect_uri.end(), moqt_proto.begin(), moqt_proto.end());
                   moqt_it != connect_uri.end()) {
            proto = moqt_proto;
            transport_proto = TransportProtocol::kQuic;
            it = moqt_it;
        } else if (auto https_it = std::search(it, connect_uri.end(), https_proto.begin(), https_proto.end());
                   https_it != connect_uri.end()) {
            proto = https_proto;
            transport_proto = TransportProtocol::kWebTransport;
            it = https_it;
        } else {
            return std::nullopt;
        }

        // move to end of proto://
        std::advance(it, proto.length());

        std::string address_str;
        std::string path_str;
        uint16_t port = 0; // 0 indicates no port specified

        // Find first ':' (port) or '/' (path)
        auto colon_it = std::find(it, connect_uri.end(), ':');
        auto slash_it = std::find(it, connect_uri.end(), '/');

        // Determine where address ends
        auto address_end_it = std::min(colon_it, slash_it);

        // Parse address (everything before ':' or '/' or end)
        address_str.assign(it, address_end_it);

        if (address_str.empty()) {
            return std::nullopt;
        }

        it = address_end_it;

        // Parse port if present (: comes before /)
        if (it != connect_uri.end() && *it == ':') {
            ++it; // skip ':'

            // Find where port ends (at '/' or end of string)
            auto port_end_it = std::find(it, connect_uri.end(), '/');

            std::string port_str(it, port_end_it);
            if (!port_str.empty()) {
                try {
                    port = static_cast<uint16_t>(std::stoi(port_str));
                } catch (...) {
                    return std::nullopt; // Invalid port number
                }
            }

            it = port_end_it;
        }

        // Parse path if present (starts with '/')
        if (it != connect_uri.end() && *it == '/') {
            path_str.assign(it, connect_uri.end()); // Include the leading '/'
        }

        return std::make_tuple(address_str, port, transport_proto, path_str);
    }

    Session::Session(const ClientConfig& cfg,
                     std::shared_ptr<Transport> transport,
                     std::shared_ptr<Connection> connection,
                     std::shared_ptr<ClientCallbacks> callbacks,
                     std::shared_ptr<timeq::tick_service> tick_service,
                     std::shared_ptr<Logger> logger)
      : std::enable_shared_from_this<Session>()
      , current_connection_(std::move(connection))
      , callbacks_(std::move(callbacks))
      , next_request_id_(0)
      , client_mode_(true)
      , logger_(std::move(logger))
      , server_config_({})
      , client_config_(cfg)
      , tick_service_(std::move(tick_service))
      , quic_transport_(std::move(transport))
    {
        QUICR_LOGGER_TRACE(logger_, "Created MoQ Session in client mode connected to {}", cfg.connect_uri);
        Init();
    }

    Session::Session(const ServerConfig& cfg,
                     std::shared_ptr<Transport> transport,
                     std::shared_ptr<Connection> connection,
                     std::shared_ptr<ServerCallbacks> callbacks,
                     std::shared_ptr<timeq::tick_service> tick_service,
                     std::shared_ptr<Logger> logger)
      : std::enable_shared_from_this<Session>()
      , current_connection_(std::move(connection))
      , callbacks_(std::move(callbacks))
      , next_request_id_(1)
      , client_mode_(false)
      , logger_(std::move(logger))
      , server_config_(cfg)
      , client_config_({})
      , tick_service_(std::move(tick_service))
      , quic_transport_(std::move(transport))
    {
        tx_ctrl_stream_ = quic_transport_->CreateControlStream(current_connection_);

        QUICR_LOGGER_INFO(
          logger_, "Created MoQ Session in server mode listening on {}:{}", cfg.server_bind_ip, cfg.server_port);
        Init();
    }

    Session::~Session() {}

    void Session::Init()
    {
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

        OnConnectionStatus(current_connection_->GetStatus());
    }

    void Session::Disconnect()
    {
        current_connection_->SetDelegate(nullptr);

        if (quic_transport_) {
            quic_transport_->Close(current_connection_);
        }
    }

    std::uint64_t Session::RequestTrackStatus(const FullTrackName& track_full_name, const SubscribeAttributes&)
    {
        std::lock_guard<std::mutex> _(state_mutex_);

        auto request_id = GetNextRequestID();

        SendTrackStatus(request_id, track_full_name);

        return request_id;
    }

    void Session::SendCtrlMsg(const std::shared_ptr<Stream>& stream,
                              std::shared_ptr<const std::vector<uint8_t>> data,
                              bool close_stream)
    {
        if (stream == nullptr) {
            throw ProtocolViolationException("Control stream not created");
        }

        auto result = quic_transport_->Enqueue(
          current_connection_, stream, std::move(data), 0, 2000, { true, close_stream, false, false });

        if (result != TransportError::kNone) {
            throw TransportException(result);
        }
    }

    void Session::SendSetup()
    try {
        QUICR_LOGGER_DEBUG(logger_, "Sending SETUP to conn_id: {}", current_connection_->GetID());

        KeyValuePairs setup_options;

        if (client_mode_) {
            setup_options.Add(SetupOptionType::kEndpointId, client_config_.endpoint_id);

            const auto [host, port, protocol, path] = *ParseConnectUri(client_config_.connect_uri);
            if (protocol != TransportProtocol::kWebTransport) {
                const auto authority = port > 0 ? host + ":" + std::to_string(port) : host;
                setup_options.Add(SetupOptionType::kAuthority, authority);
                if (!path.empty()) {
                    setup_options.Add(SetupOptionType::kPath, path);
                }
            }

        } else {
            setup_options.Add(SetupOptionType::kEndpointId, server_config_.endpoint_id);
        }
        SendCtrlMsg(tx_ctrl_stream_, ControlMessageType::kSetup, setup_options);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending Setup (error={})", e.what());
        throw e;
    }

    void Session::SendTrackStatusOk(const std::shared_ptr<Stream>& stream,
                                    const std::optional<messages::Location>& largest_object,
                                    const TrackExtensions& track_properties)
    {
        SendRequestOk(
          stream, Parameters().AddOptional(ParameterType::kLargestObject, largest_object), track_properties);
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
                                const TrackExtensions& track_properties)
    try {
        QUICR_LOGGER_DEBUG(logger_,
                           "Sending REQUEST_OK to conn_id: {} stream_id: {}",
                           current_connection_->GetID(),
                           stream->GetStreamId());

        SendCtrlMsg(stream, ControlMessageType::kRequestOk, params, track_properties);
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
                           current_connection_->GetID(),
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
                                   bool close_stream)
    try {
        QUICR_LOGGER_DEBUG(logger_,
                           "Sending REQUEST_ERROR to conn_id: {} request_id: {} error code: {} reason: {}",
                           current_connection_->GetID(),
                           request_id,
                           static_cast<int>(error),
                           reason);

        messages::Message msg = messages::Message{}.PrependType(ControlMessageType::kRequestError).ReserveLength();
        msg.Append(error);
        msg.Append(UintVar(retry_interval.count()));
        msg.Append(AsOwnedBytes(reason));
        SendCtrlMsg(stream, msg.ToBytes(), close_stream);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending REQUEST_ERROR (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendPublishNamespace(const std::shared_ptr<Stream>& stream,
                                       std::uint64_t request_id,
                                       const TrackNamespace& track_namespace)
    try {
        QUICR_LOGGER_DEBUG(logger_,
                           "Sending PublishNamespace to conn_id: {} request_id: {} namespace_hash: {}",
                           current_connection_->GetID(),
                           request_id,
                           TrackHash({ track_namespace, {} }).track_namespace_hash);

        SendCtrlMsg(stream, ControlMessageType::kPublishNamespace, UintVar(request_id), track_namespace, Parameters{});
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending PublishNamespace (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendTrackStatus(std::uint64_t request_id, const FullTrackName& tfn)
    try {
        QUICR_LOGGER_DEBUG(
          logger_, "Sending TRACK_STATUS to conn_id: {} request_id: {}", current_connection_->GetID(), request_id);

        SendCtrlMsg(tx_ctrl_stream_, ControlMessageType::kTrackStatus, UintVar(request_id), tfn.name_space, tfn.name);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending Trac (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendSubscribe(const std::shared_ptr<Stream>& stream,
                                uint64_t request_id,
                                const FullTrackName& tfn,
                                TrackHash th, // TODO: This is only for a debug message, should be removed
                                std::uint8_t priority,
                                std::optional<GroupOrder> group_order,
                                const Filter& filter,
                                std::optional<std::chrono::milliseconds> delivery_timeout)
    try {
        /* Available parameters:
         * - AUTHORIZATION TOKEN (0x03): Conveys information to authorize the subscription.
         * - DELIVERY TIMEOUT (0x02): Duration the relay should attempt forwarding objects.
         * - SUBSCRIBER PRIORITY (0x20): Priority of the subscription relative to others.
         * - GROUP ORDER (0x22): Preference for group delivery order (Ascending/Descending).
         * - SUBSCRIPTION FILTER (0x21): Specifies which objects the publisher should send.
         * - FORWARD (0x10): Specifies the Forwarding State (0 or 1).
         * - NEW GROUP REQUEST (0x32): Requests the publisher to start a new group.
         */
        auto params =
          Parameters{}
            .Add(ParameterType::kSubscriberPriority, priority)
            .AddOptional(ParameterType::kGroupOrder, group_order)
            .Add(ParameterType::kForward, 1)
            .AddOptional(ParameterType::kDeliveryTimeout,
                         delivery_timeout.has_value() ? std::make_optional(delivery_timeout->count()) : std::nullopt);

        if (auto filter_type = GetFilterParameterType(filter); filter_type != ParameterType::kInvalid) {
            params.Add(filter_type, filter);
        }

        QUICR_LOGGER_DEBUG(logger_,
                           "Sending SUBSCRIBE to conn_id: {} request_id: {} track namespace hash: {} name hash: {}",
                           current_connection_->GetID(),
                           request_id,
                           th.track_namespace_hash,
                           th.track_name_hash);

        SendCtrlMsg(stream, ControlMessageType::kSubscribe, UintVar(request_id), tfn.name_space, tfn.name, params);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending Subscribe (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendPublish(const std::shared_ptr<Stream>& stream,
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
                            .Add(ExtensionType::kDefaultPublisherPriority, publish.default_publisher_priority)
                            .Add(ExtensionType::kDynamicGroups, publish.dynamic_groups);

        QUICR_LOGGER_DEBUG(logger_,
                           "Sending PUBLISH to conn_id: {} request_id: {} track alias: {}",
                           current_connection_->GetID(),
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
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending Publish (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
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
                                  uint64_t expires,
                                  const std::optional<Location>& largest_location,
                                  messages::GroupOrder publisher_default_group_order)
    try {
        auto params = Parameters{}
                        .Add(ParameterType::kExpires, expires)
                        .AddOptional(ParameterType::kLargestObject, largest_location);

        auto extensions = TrackExtensions{}
                            .Add(ExtensionType::kDeliveryTimeout, 0)
                            .Add(ExtensionType::kMaxCacheDuration, 0)
                            .Add(ExtensionType::kDefaultPublisherGroupOrder, publisher_default_group_order)
                            .Add(ExtensionType::kDefaultPublisherPriority, 1)
                            .Add(ExtensionType::kDynamicGroups, true);

        QUICR_LOGGER_DEBUG(
          logger_, "Sending SUBSCRIBE OK to conn_id: {} request_id: {}", current_connection_->GetID(), request_id);

        SendCtrlMsg(stream, ControlMessageType::kSubscribeOk, UintVar(track_alias), params, extensions);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending SubscribeOk (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendPublishDone(const std::shared_ptr<Stream>& stream,
                                  uint64_t request_id,
                                  messages::PublishDoneStatusCode status,
                                  const std::string& reason)
    try {
        QUICR_LOGGER_DEBUG(logger_,
                           "Sending PUBLISH_DONE to conn_id: {} request_id: {} status: {}",
                           current_connection_->GetID(),
                           request_id,
                           static_cast<uint64_t>(status));

        SendCtrlMsg(
          stream, ControlMessageType::kPublishDone, UintVar(request_id), status, UintVar(0), AsOwnedBytes(reason));
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending PUBLISH_DONE (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendSubscribeNamespace(const std::shared_ptr<Stream>& stream,
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
                           current_connection_->GetID(),
                           request_id,
                           th.track_namespace_hash);

        SendCtrlMsg(stream, type, UintVar(request_id), prefix, params);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending subscribe namespace (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendUnsubscribeNamespace(const std::shared_ptr<Stream>& stream, const TrackNamespace& prefix)
    try {
        [[maybe_unused]] auto th = TrackHash({ prefix, {} });

        QUICR_LOGGER_DEBUG(logger_,
                           "Sending UNSUBSCRIBE_NAMESPACE to conn_id: {} prefix_hash: {}",
                           current_connection_->GetID(),
                           th.track_namespace_hash);

        SendCtrlMsg(stream, ControlMessageType::kNamespaceDone, prefix);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending UNSUBSCRIBE_NAMESPACE (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SubscribeNamespace(std::shared_ptr<SubscribeNamespaceHandler> handler)
    {
        const auto& prefix = handler->GetPrefix();
        handler->connection_id_ = current_connection_->GetID();

        [[maybe_unused]] auto th = TrackHash({ prefix, {} });

        QUICR_LOGGER_INFO(logger_,
                          "Subscribe namespace conn_id: {} prefix_hash: {} mode: {}",
                          current_connection_->GetID(),
                          th.track_namespace_hash,
                          handler->GetMode() == SubscribeNamespaceHandler::Mode::kNamespaces ? "namespaces" : "tracks");

        std::lock_guard<std::mutex> lock(state_mutex_);

        handler->SetRequestId(GetNextRequestID());
        handler->SetTransport(GetSharedPtr());

        if (auto [_, is_new] = request_handlers.try_emplace(handler->GetRequestId().value(), handler); !is_new) {
            QUICR_LOGGER_WARN(logger_, "Namespace already subscribed to (prefix_hash={})", th.track_namespace_hash);
            return;
        }

        const auto message_type = handler->GetMode() == SubscribeNamespaceHandler::Mode::kNamespaces
                                    ? ControlMessageType::kSubscribeNamespace
                                    : ControlMessageType::kSubscribeTracks;

        const auto request_stream = quic_transport_->CreateRequestStream(current_connection_);
        handler->SetRequestStream(request_stream);
        request_by_stream[request_stream->GetStreamId()] = { .request_id = handler->GetRequestId().value(),
                                                             .is_request_stream = true };

        SendSubscribeNamespace(
          request_stream, handler->GetRequestId().value(), prefix, handler->GetFilter(), message_type);
    }

    void Session::UnsubscribeNamespace(const std::shared_ptr<SubscribeNamespaceHandler>& handler)
    {
        const auto& prefix = handler->GetPrefix();
        [[maybe_unused]] auto th = TrackHash({ prefix, {} });

        QUICR_LOGGER_INFO(logger_,
                          "Unsubscribe namespace conn_id: {} prefix_hash: {}",
                          current_connection_->GetID(),
                          th.track_namespace_hash);

        std::lock_guard<std::mutex> lock(state_mutex_);

        RemoveSubscribeNamespace(*handler);
    }

    void Session::SendFetch(const std::shared_ptr<Stream>& stream,
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
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending Fetch (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
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

    void Session::SubscribeTrack(std::shared_ptr<SubscribeTrackHandler> track_handler)
    {
        const auto& tfn = track_handler->GetFullTrackName();
        track_handler->connection_id_ = current_connection_->GetID();

        // Track hash is the track alias for now.
        auto th = TrackHash(tfn);

        auto proposed_track_alias = track_handler->GetTrackAlias();
        if (not proposed_track_alias.has_value()) {
            track_handler->SetTrackAlias(th.track_fullname_hash);
        } else {
            th.track_fullname_hash = proposed_track_alias.value();
        }

        QUICR_LOGGER_INFO(
          logger_, "Subscribe track conn_id: {} track_alias: {}", current_connection_->GetID(), th.track_fullname_hash);

        std::lock_guard<std::mutex> _(state_mutex_);

        if (!track_handler->IsPublisherInitiated()) {
            // increment and get the next request id if not initiated by publisher, which request Id is reused
            track_handler->SetRequestId(GetNextRequestID());

        } else {
            if (!track_handler->GetReceivedTrackAlias().has_value()) {
                throw std::runtime_error("Missing received track alias for publisher initiated subscribe");
            }

            if (!track_handler->GetRequestId().has_value()) {
                throw std::runtime_error("Missing request id for publisher initiated subscribe");
            }

            const auto req_it = recv_req_id.find(*track_handler->GetRequestId());
            if (req_it != recv_req_id.end() && req_it->second.stream) {
                track_handler->SetRequestStream(req_it->second.stream);
            }

            sub_by_recv_track_alias[*track_handler->GetReceivedTrackAlias()] = track_handler;
        }

        auto priority = track_handler->GetPriority();
        auto group_order = track_handler->GetGroupOrder();
        const auto& filter = track_handler->GetFilter();
        auto delivery_timeout = track_handler->GetDeliveryTimeout();

        track_handler->SetTransport(GetSharedPtr());

        if (!track_handler->IsPublisherInitiated()) {
            if (auto [_, is_new] = request_handlers.try_emplace(*track_handler->GetRequestId(), track_handler);
                !is_new) {
                QUICR_LOGGER_WARN(logger_,
                                  "Track already subscribed conn_id: {} track_alias: {}",
                                  current_connection_->GetID(),
                                  th.track_fullname_hash);
                return;
            }

            const auto request_stream = quic_transport_->CreateRequestStream(current_connection_);
            track_handler->SetRequestStream(request_stream);
            request_by_stream[request_stream->GetStreamId()] = { .request_id = track_handler->GetRequestId().value(),
                                                                 .is_request_stream = true };

            SendSubscribe(
              request_stream, *track_handler->GetRequestId(), tfn, th, priority, group_order, filter, delivery_timeout);

            // Handle joining fetch, if requested.
            auto joining_fetch = track_handler->GetJoiningFetch();
            if (track_handler->GetJoiningFetch()) {
                const auto& info = *joining_fetch;
                // Make a joining fetch handler.
                const auto joining_fetch_handler = std::make_shared<JoiningFetchHandler>(
                  track_handler, info.group_order.value_or(messages::GroupOrder::kAscending));
                const auto fetch_rid = GetNextRequestID();
                QUICR_LOGGER_INFO(logger_,
                                  "Subscribe with joining fetch conn_id: {} track_alias: {} subscribe id: {} "
                                  "joining subscribe id: {}",
                                  current_connection_->GetID(),
                                  th.track_fullname_hash,
                                  fetch_rid,
                                  *track_handler->GetRequestId());
                joining_fetch_handler->SetRequestId(fetch_rid);
                joining_fetch_handler->SetConnectionId(current_connection_->GetID());
                joining_fetch_handler->SetTransport(GetSharedPtr());
                const auto fetch_stream = quic_transport_->CreateRequestStream(current_connection_);
                joining_fetch_handler->SetRequestStream(fetch_stream);
                request_by_stream[fetch_stream->GetStreamId()] = { .request_id = fetch_rid, .is_request_stream = true };
                request_handlers[fetch_rid] = std::move(joining_fetch_handler);
                SendJoiningFetch(fetch_stream,
                                 fetch_rid,
                                 info.priority,
                                 info.group_order,
                                 *track_handler->GetRequestId(),
                                 info.joining_start,
                                 info.absolute);
            }
        } else {
            request_handlers[*track_handler->GetRequestId()] = track_handler;
        }
    }

    void Session::UnsubscribeTrack(const std::shared_ptr<SubscribeTrackHandler>& track_handler)
    {
        const auto& tfn = track_handler->GetFullTrackName();
        auto th = TrackHash(tfn);

        QUICR_LOGGER_INFO(logger_,
                          "Unsubscribe track conn_id: {} track_alias: {}",
                          current_connection_->GetID(),
                          th.track_fullname_hash);

        std::lock_guard<std::mutex> lock(state_mutex_);

        RemoveSubscribeTrack(*track_handler);
    }

    void Session::UpdateTrackSubscription(std::shared_ptr<SubscribeTrackHandler> track_handler)
    {
        const auto& tfn = track_handler->GetFullTrackName();
        auto th = TrackHash(tfn);

        QUICR_LOGGER_INFO(
          logger_, "Subscribe track conn_id: {} hash: {}", current_connection_->GetID(), th.track_fullname_hash);

        std::lock_guard<std::mutex> _(state_mutex_);

        if (not track_handler->GetRequestId().has_value()) {
            return;
        }

        auto priority = track_handler->GetPriority();
        const auto request_stream = track_handler->GetRequestStream();
        if (request_stream == nullptr) {
            QUICR_LOGGER_ERROR(
              logger_, "Subscribe track update missing request stream conn_id: {}", current_connection_->GetID());
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
                        quic_transport_->CloseStream(
                          current_connection_, handler.GetRequestStream(), StreamOperation::kCancel);
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
            if (handler.GetRequestId().has_value()) {
                request_handlers.erase(*handler.GetRequestId());
            }

            if (handler.GetReceivedTrackAlias().has_value()) {
                sub_by_recv_track_alias.erase(handler.GetReceivedTrackAlias().value());
            }
        }
    }

    void Session::RemoveSubscribeNamespace(SubscribeNamespaceHandler& handler,
                                           bool remove_handler,
                                           bool send_unsubscribe)
    {
        switch (handler.GetStatus()) {
            case SubscribeNamespaceHandler::Status::kOk:
                try {
                    if (const auto stream = handler.GetRequestStream(); send_unsubscribe && stream != nullptr) {
                        SendUnsubscribeNamespace(stream, handler.GetPrefix());
                    }
                } catch (const std::exception& e) {
                    QUICR_LOGGER_ERROR(logger_, "Failed to send unsubscribe namespace: {}", e.what());
                }

                handler.SetStatus(SubscribeNamespaceHandler::Status::kNotSubscribed);
                break;

            default:
                break;
        }

        if (remove_handler && handler.GetRequestId().has_value()) {
            request_handlers.erase(*handler.GetRequestId());
        }
    }

    void Session::ClosePublishTrackLocal(

      PublishTrackHandler& handler,
      std::uint64_t stream_id,
      bool is_reset)
    {
        handler.StreamClosed(stream_id, is_reset);

        // TODO: There is more complicated state here around PUBLISH_DONE and REQUEST_ERROR.
        handler.SetStatus(is_reset ? PublishTrackHandler::Status::kUnsubscribed
                                   : PublishTrackHandler::Status::kDoneByFin);

        const auto th = TrackHash(handler.GetFullTrackName());
        pub_tracks_by_track_alias.erase(th.track_fullname_hash);

        if (handler.GetRequestId().has_value()) {
            recv_req_id.erase(*handler.GetRequestId());
        }

        if (auto pub_ns_it = pub_tracks_by_name.find(th.track_namespace_hash); pub_ns_it != pub_tracks_by_name.end()) {
            pub_ns_it->second.erase(th.track_name_hash);
            if (pub_ns_it->second.empty()) {
                pub_tracks_by_name.erase(pub_ns_it);
            }
        }

        // TODO: is_reset should propagate down here?
        handler.EndAllSubgroups();
    }

    void Session::CloseRequestHandler(

      std::uint64_t request_id,
      std::uint64_t stream_id,
      StreamClosedFlag flag)
    {
        std::unique_lock lock(state_mutex_);

        // Incoming PUBNS requests are not handler based.
        if (std::erase(recv_publish_namespaces, request_id) > 0) {
            recv_req_id.erase(request_id);

            lock.unlock();

            if (auto callbacks = std::dynamic_pointer_cast<ServerCallbacks>(callbacks_)) {
                callbacks->PublishNamespaceDoneReceived(GetSharedPtr(), request_id)
                  .Resolve([request_id, self = GetSharedPtr()](const auto& result) {
                      if (result) {
                          return;
                      }

                      const auto& [code, reason] = result.error();
                      QUICR_LOGGER_ERROR(self->logger_,
                                         "Publish namespace done failed conn_id: {} request_id: {} code: {} "
                                         "reason: {}",
                                         self->current_connection_->GetID(),
                                         request_id,
                                         static_cast<std::uint64_t>(code),
                                         reason.value_or("unknown"));
                  });
            }
            return;
        }

        const auto handler_it = request_handlers.find(request_id);
        if (handler_it == request_handlers.end()) {
            QUICR_LOGGER_DEBUG(logger_,
                               "Stream closed for unknown request_id conn_id: {} request_id: {}",
                               current_connection_->GetID(),
                               request_id);
            recv_req_id.erase(request_id);
            return;
        }

        const bool is_reset = flag == StreamClosedFlag::kReset;

        if (const auto request_stream_id = handler_it->second->GetRequestStreamId()) {
            request_by_stream.erase(*request_stream_id);
        }

        QUICR_LOGGER_INFO(logger_,
                          "Closing request handler conn_id: {} request_id: {} stream_id: {} reset: {}",
                          current_connection_->GetID(),
                          request_id,
                          stream_id,
                          is_reset);

        if (auto sub_handler = handler_it->second->Get<SubscribeTrackHandler>()) {
            sub_handler->SetStatus(is_reset ? SubscribeTrackHandler::Status::kDoneByReset
                                            : SubscribeTrackHandler::Status::kDoneByFin);

            RemoveSubscribeTrack(*sub_handler, false);
            request_handlers.erase(handler_it);
            if (sub_handler->GetReceivedTrackAlias().has_value()) {
                sub_by_recv_track_alias.erase(sub_handler->GetReceivedTrackAlias().value());
            }

            recv_req_id.erase(request_id);

            lock.unlock();
            if (auto callbacks = std::dynamic_pointer_cast<ServerCallbacks>(callbacks_)) {
                callbacks->UnsubscribeReceived(GetSharedPtr(), request_id)
                  .Resolve([request_id, self = GetSharedPtr()](const auto& result) {
                      if (result) {
                          return;
                      }

                      const auto& [code, reason] = result.error();
                      QUICR_LOGGER_ERROR(self->logger_,
                                         "Unsubscribe of subscribe track failed conn_id: {} request_id: {} code: {} "
                                         "reason: {}",
                                         self->current_connection_->GetID(),
                                         request_id,
                                         static_cast<std::uint64_t>(code),
                                         reason.value_or("unknown"));
                  });
            }

            return;
        }

        if (auto pub_handler = handler_it->second->Get<PublishTrackHandler>()) {

            ClosePublishTrackLocal(*pub_handler, stream_id, is_reset);
            request_handlers.erase(handler_it);

            lock.unlock();
            if (auto callbacks = std::dynamic_pointer_cast<ServerCallbacks>(callbacks_)) {
                callbacks->UnsubscribeReceived(GetSharedPtr(), request_id)
                  .Resolve([request_id, self = GetSharedPtr()](const auto& result) {
                      if (result) {
                          return;
                      }

                      const auto& [code, reason] = result.error();
                      QUICR_LOGGER_ERROR(self->logger_,
                                         "Unsubscribe of publish track failed conn_id: {} request_id: {} code: {} "
                                         "reason: {}",
                                         self->current_connection_->GetID(),
                                         request_id,
                                         static_cast<std::uint64_t>(code),
                                         reason.value_or("unknown"));
                  });
            }

            return;
        }

        if (auto ns_handler = handler_it->second->Get<SubscribeNamespaceHandler>()) {
            RemoveSubscribeNamespace(*ns_handler, false, false);
            request_handlers.erase(handler_it);
            recv_req_id.erase(request_id);
            return;
        }

        if (auto pub_ns_handler = handler_it->second->Get<PublishNamespaceHandler>()) {
            pub_ns_handler->SetStatus(PublishNamespaceHandler::Status::kNotPublished);
            request_handlers.erase(handler_it);
            recv_req_id.erase(request_id);
            return;
        }

        if (auto fetch_handler = handler_it->second->Get<FetchTrackHandler>()) {
            fetch_handler->SetStatus(is_reset ? FetchTrackHandler::Status::kDoneByReset
                                              : FetchTrackHandler::Status::kDoneByFin);
            request_handlers.erase(handler_it);
            recv_req_id.erase(request_id);
            return;
        }

        request_handlers.erase(handler_it);
        recv_req_id.erase(request_id);
    }

    void Session::UnpublishTrack(const std::shared_ptr<PublishTrackHandler>& track_handler)
    {
        // Generate track alias
        auto tfn = track_handler->GetFullTrackName();
        auto th = TrackHash(tfn);

        QUICR_LOGGER_INFO(
          logger_, "Unpublish track conn_id: {} hash: {}", current_connection_->GetID(), th.track_fullname_hash);

        std::unique_lock<std::mutex> lock(state_mutex_);

        pub_tracks_by_track_alias.erase(th.track_fullname_hash);

        if (!track_handler->GetRequestId().has_value()) {
            return;
        }

        request_handlers.erase(track_handler->GetRequestId().value());

        /*
         * This is a round about way to send subscribe done because of the announce flow. This
         * will go away if we stop using the announce flow. For now, it works for both announce
         * and publish flows.
         */
        auto pub_ns_it = pub_tracks_by_name.find(th.track_namespace_hash);
        if (pub_ns_it != pub_tracks_by_name.end()) {
            auto pub_n_it = pub_ns_it->second.find(th.track_name_hash);
            if (pub_n_it != pub_ns_it->second.end()) {
                const auto ctrl_stream = pub_n_it->second->GetRequestStream();

                // Send subscribe done if track has subscriber and is sending
                if (pub_n_it->second->GetStatus() == PublishTrackHandler::Status::kOk &&
                    pub_n_it->second->GetRequestId().has_value() && ctrl_stream != nullptr) {
                    QUICR_LOGGER_INFO(logger_,
                                      "Unpublish track namespace hash: {} track_name_hash: {} track_alias: {}, sending "
                                      "publish_done",
                                      th.track_namespace_hash,
                                      th.track_name_hash,
                                      th.track_fullname_hash);
                    SendPublishDone(ctrl_stream,
                                    *pub_n_it->second->GetRequestId(),
                                    PublishDoneStatusCode::kSubscribtionEnded,
                                    "Unpublish track");
                } else {
                    QUICR_LOGGER_INFO(logger_,
                                      "Unpublish track namespace hash: {} track_name_hash: {} track_alias: {}",
                                      th.track_namespace_hash,
                                      th.track_name_hash,
                                      th.track_fullname_hash);
                }

                lock.unlock();

                // We continue to use the kNotAnnounced state when removing. Might make sense to use kDestroyed instead
                pub_n_it->second->SetStatus(PublishTrackHandler::Status::kNotAnnounced);

                lock.lock();

                pub_ns_it->second.erase(pub_n_it);
            }

            // Close whatever the application left open, so its objects drain rather than the streams
            // lingering for the life of the connection.
            track_handler->EndAllSubgroups();
        }
    }

    void Session::PublishTrack(std::shared_ptr<PublishTrackHandler> track_handler)
    {
        const auto tfn = track_handler->GetFullTrackName();
        auto th = TrackHash(tfn);
        QUICR_LOGGER_INFO(logger_,
                          "Publish track conn_id: {} hash: {} tfn: {} ({})",
                          current_connection_->GetID(),
                          th.track_fullname_hash,
                          tfn.NamespaceStr(),
                          tfn.NameStr());

        std::unique_lock<std::mutex> lock(state_mutex_);

        track_handler->SetRequestId(GetNextRequestID());

        if (!track_handler->GetTrackAlias().has_value()) {
            track_handler->SetTrackAlias(th.track_fullname_hash);
        }

        // Add state to received request ID since a subscribe will not be received for this request
        recv_req_id[*track_handler->GetRequestId()] = { track_handler->GetFullTrackName(), th };

        track_handler->SetStatus(PublishTrackHandler::Status::kPendingPublishOk);

        const auto ctrl_stream = quic_transport_->CreateRequestStream(current_connection_);
        track_handler->SetRequestStream(ctrl_stream);
        request_by_stream[ctrl_stream->GetStreamId()] = { .request_id = track_handler->GetRequestId().value(),
                                                          .is_request_stream = true };

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
                                         .default_publisher_priority = track_handler->GetDefaultPriority(),
                                         .max_cache_duration = std::nullopt,
                                         .delivery_timeout = track_handler->GetDefaultTTL(),
                                         .track_properties = {} };

        SendPublish(ctrl_stream, *track_handler->GetRequestId(), publish);

        track_handler->connection_id_ = current_connection_->GetID();
        QUICR_LOGGER_INFO(logger_,
                          "Publish track connId {}, track namespace hash: {}, name hash: {}",
                          current_connection_->GetID(),
                          th.track_namespace_hash,
                          th.track_name_hash);

        // Set this transport as the one for the publisher to use.
        track_handler->SetTransport(GetSharedPtr());

        // Hold ref to track handler
        pub_tracks_by_name[th.track_namespace_hash][th.track_name_hash] = track_handler;
        pub_tracks_by_track_alias[th.track_fullname_hash][current_connection_->GetID()] = track_handler;
        request_handlers[*track_handler->GetRequestId()] = track_handler;
    }

    void Session::PublishNamespace(std::shared_ptr<PublishNamespaceHandler> ns_handler, bool passive)
    {
        auto prefix_hash = hash(ns_handler->GetPrefix());
        QUICR_LOGGER_INFO(
          logger_, "Publish namespace conn_id: {0} hash: {1}", current_connection_->GetID(), prefix_hash);

        std::unique_lock<std::mutex> lock(state_mutex_);

        if (!passive) {
            ns_handler->SetRequestId(GetNextRequestID());

            QUICR_LOGGER_INFO(logger_, "Publishing to namespace hash: {0} sending ANNOUNCE message", prefix_hash);

            lock.unlock();

            ns_handler->SetStatus(PublishNamespaceHandler::Status::kPendingResponse);

            const auto request_stream = quic_transport_->CreateRequestStream(current_connection_);
            ns_handler->SetRequestStream(request_stream);

            lock.lock();

            request_by_stream[request_stream->GetStreamId()] = { .request_id = ns_handler->GetRequestId().value(),
                                                                 .is_request_stream = true };

            SendPublishNamespace(request_stream, *ns_handler->GetRequestId(), ns_handler->GetPrefix());
            request_handlers[*ns_handler->GetRequestId()] = ns_handler;

        } else {
            ns_handler->SetStatus(PublishNamespaceHandler::Status::kOk);
        }

        ns_handler->SetConnectionId(current_connection_->GetID());
        ns_handler->SetTransport(GetSharedPtr());
    }

    void Session::PublishNamespaceDone(const std::shared_ptr<PublishNamespaceHandler>& track_handler)
    {
        const auto& prefix = track_handler->GetPrefix();
        const auto prefix_hash = hash(prefix);

        QUICR_LOGGER_INFO(
          logger_, "PublishNamespaceDone (conn_id={}, prefix_hash={})", current_connection_->GetID(), prefix_hash);

        std::lock_guard<std::mutex> lock(state_mutex_);

        const auto request_stream = track_handler->GetRequestStream();
        if (request_stream == nullptr) {
            QUICR_LOGGER_ERROR(logger_,
                               "PublishNamespaceDone missing request context conn_id: {} prefix_hash: {}",
                               current_connection_->GetID(),
                               prefix_hash);
            return;
        }

        quic_transport_->CloseStream(current_connection_, request_stream, StreamOperation::kCancel);
        request_handlers.erase(track_handler->GetRequestId().value());
    }

    void Session::FetchTrack(std::shared_ptr<FetchTrackHandler> track_handler)
    {
        const auto& tfn = track_handler->GetFullTrackName();
        auto th = TrackHash(tfn);

        track_handler->SetTrackAlias(th.track_fullname_hash);

        QUICR_LOGGER_INFO(
          logger_, "Fetch track conn_id: {} hash: {}", current_connection_->GetID(), th.track_fullname_hash);

        std::lock_guard<std::mutex> _(state_mutex_);

        track_handler->SetRequestId(GetNextRequestID());
        track_handler->SetConnectionId(current_connection_->GetID());
        track_handler->SetTransport(GetSharedPtr());

        QUICR_LOGGER_DEBUG(logger_, "subscribe id (from fetch) to add to memory: {}", *track_handler->GetRequestId());

        auto priority = track_handler->GetPriority();
        auto group_order = track_handler->GetGroupOrder();
        auto start_location = track_handler->GetStartLocation();
        auto end_location = track_handler->GetEndLocation();

        track_handler->SetStatus(FetchTrackHandler::Status::kPendingResponse);

        const auto request_id = *track_handler->GetRequestId();
        request_handlers[*track_handler->GetRequestId()] = track_handler;

        const auto request_stream = quic_transport_->CreateRequestStream(current_connection_);
        track_handler->SetRequestStream(request_stream);
        request_by_stream[request_stream->GetStreamId()] = { .request_id = request_id, .is_request_stream = true };

        SendFetch(request_stream, request_id, tfn, priority, group_order, start_location, end_location);
    }

    void Session::CancelFetchTrack(std::shared_ptr<FetchTrackHandler> track_handler)
    {
        std::lock_guard<std::mutex> _(state_mutex_);

        const auto sub_id = track_handler->GetRequestId();
        if (!sub_id.has_value()) {
            return;
        }

        request_handlers.erase(track_handler->GetRequestId().value());

        track_handler->SetRequestId(std::nullopt);

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

    void Session::RemoveAllTracksForConnectionClose()
    {
        // clean up subscriber handlers on disconnect
        for (auto& [req_id, req] : request_handlers) {
            if (auto h = req->Get<SubscribeTrackHandler>()) {

                RemoveSubscribeTrack(*h, false);
                if (req->GetConnectionId() == current_connection_->GetID()) {
                    if (auto h = req->Get<SubscribeTrackHandler>()) {
                        h->SetStatus(SubscribeTrackHandler::Status::kNotConnected);
                    }
                }
            } else if (auto h = req->Get<PublishTrackHandler>()) {
                h->SetStatus(PublishTrackHandler::Status::kNotConnected);
                h->SetRequestId(std::nullopt);
            }
        }

        pub_tracks_by_name.clear();
        recv_req_id.clear();
        recv_publish_namespaces.clear();
        request_handlers.clear();
        sub_by_recv_track_alias.clear();
    }

    // ---------------------------------------------------------------------------------------
    // Transport handler callbacks
    // ---------------------------------------------------------------------------------------

    void Session::OnConnectionStatus(Connection::Status status)
    {
        QUICR_LOGGER_DEBUG(
          logger_, "Connection status conn_id: {} status: {}", current_connection_->GetID(), static_cast<int>(status));
        bool remove_connection = false;

        switch (status) {
            case Connection::Status::kReady: {
                if (client_mode_) {
                    QUICR_LOGGER_INFO(logger_, "Connection established, creating bi-dir stream and sending SETUP");

                    tx_ctrl_stream_ = quic_transport_->CreateControlStream(current_connection_);

                    SendSetup();

                    if (client_mode_) {
                        SetStatus(Status::kPendingServerSetup);
                    } else {
                        SetStatus(Status::kReady);
                    }
                }
                break;
            }

            case Connection::Status::kConnecting:
                if (client_mode_) {
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
                SetStatus(Status::kNotReady);
                break;
        }

        if (remove_connection) {
            RemoveAllTracksForConnectionClose();
        }
    }

    void Session::SetStatus(Status status)
    {
        status_ = status;
        if (callbacks_) {
            if (auto self = weak_from_this().lock()) {
                callbacks_->StatusChanged(self, status);
            }
        }
    }

    void Session::OnRecvStream(uint64_t stream_id,
                               const std::shared_ptr<StreamRxContext>& rx_ctx,
                               const std::shared_ptr<Stream>& stream,
                               const bool is_bidir)
    try {
        if (rx_ctx == nullptr) {
            return;
        }

        /*
         * RX data queue may have more messages at time of this callback. Attempt to
         *      process all of them, up to a max. Setting a max prevents blocking
         *      of other streams, etc.
         */
        for (int i = 0; i < kReadLoopMaxPerStream; i++) {
            if (rx_ctx->data_queue.Empty()) {
                break;
            }

            auto data_opt = rx_ctx->data_queue.Front();
            if (not data_opt.has_value()) {
                break;
            }

            auto& data = *data_opt.value(); // TODO: What's this double de-ref.
            std::optional<std::uint64_t> initial_stream_type;
            bool initial_data_buffered = false;
            BytesSpan initial_cursor;

            // All bidir streams are requests.
            const bool is_request_stream = is_bidir;

            // Single unidirection recv control stream.
            bool is_control_stream = !is_request_stream && stream_id == rx_ctrl_stream_id_;

            // Get message type if new stream
            if (rx_ctx->is_new && !is_request_stream && !is_control_stream) {
                // Store arriving data into stream's buffer.
                rx_ctx->data_queue.PopFront();
                auto& initial_buffer = stream_buffers[stream_id];
                initial_buffer.buffer.Push(data);
                initial_buffer.source_buffers.push_back(std::move(*data_opt));
                initial_data_buffered = true;

                // Attempt to peek what type this message is.
                initial_cursor = initial_buffer.buffer.Data();
                initial_stream_type = TryDecodeUintV(initial_cursor);
                if (!initial_stream_type.has_value()) {
                    QUICR_LOGGER_DEBUG(
                      logger_,
                      "New stream {} bidir: {} does not have enough bytes to process start of stream yet",
                      stream_id,
                      is_bidir);
                    continue;
                }

                QUICR_LOGGER_DEBUG(logger_,
                                   "New stream conn_id: {} stream_id: {} bidir: {} data size: {} msg_type: {}",
                                   current_connection_->GetID(),
                                   stream_id,
                                   is_bidir,
                                   initial_buffer.buffer.Size(),
                                   *initial_stream_type);

                // This might be incoming control stream arriving.
                if (static_cast<ControlMessageType>(*initial_stream_type) == ControlMessageType::kSetup) {
                    is_control_stream = true;
                    rx_ctrl_stream_id_ = stream_id;
                    initial_buffer.source_buffers.clear();
                }
            }

            // Control or request handling.
            if (is_control_stream || is_request_stream) {
                if (!initial_data_buffered) {
                    // Append.
                    stream_buffers[stream_id].buffer.Push(data);
                    rx_ctx->data_queue.PopFront();
                }

                rx_ctx->is_new = false;

                auto& stream_buffer = stream_buffers.at(stream_id).buffer;

                QUICR_LOGGER_DEBUG(logger_,
                                   "Transport:ControlMessageReceived conn_id: {} stream_id: {} data size: {}",
                                   current_connection_->GetID(),
                                   stream_id,
                                   stream_buffer.Size());

                // Parse control messages out of this stream data.
                while (!stream_buffer.Empty()) {
                    const auto message_view = stream_buffer.Data();
                    auto cursor = message_view;

                    // Type.
                    const auto decoded_type = TryDecodeUintV(cursor);
                    if (!decoded_type.has_value()) {
                        i = kReadLoopMaxPerStream - 4;
                        break;
                    }
                    const auto msg_type = static_cast<ControlMessageType>(*decoded_type);

                    // Length.
                    std::uint16_t payload_len;
                    if (cursor.size() < sizeof(payload_len)) {
                        i = kReadLoopMaxPerStream - 4;
                        break;
                    }
                    std::memcpy(&payload_len, cursor.data(), sizeof(payload_len));
                    payload_len = SwapBytes(payload_len);
                    cursor = cursor.subspan(sizeof(payload_len));

                    // Payload.
                    if (cursor.size() < payload_len) {
                        i = kReadLoopMaxPerStream - 4;
                        break;
                    }
                    const auto payload = cursor.first(payload_len);
                    // Consume completed message.
                    const auto message_size = message_view.size() - cursor.size() + payload_len;

                    bool processed = false;
                    try {
                        if (is_control_stream) {
                            processed = ProcessCtrlMessage(msg_type, payload);
                        } else if (is_request_stream) {
                            if (stream == nullptr) {
                                throw std::invalid_argument("Missing request stream");
                            }
                            processed = ProcessRequestMessage(stream, msg_type, payload);
                        }
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

                    stream_buffer.Pop(message_size);
                    if (!processed) {
                        current_connection_->metrics.invalid_ctrl_stream_msg++;
                    }
                }
                continue;
            } // end of control message processing

            // DATA OBJECT
            if (rx_ctx->is_new) {
                QUICR_LOGGER_TRACE(
                  logger_, "Received stream message type: 0x{:02x} ({})", *initial_stream_type, *initial_stream_type);

                bool parsed_header = false;
                switch (GetStreamMessageType(*initial_stream_type)) {
                    case StreamMessageType::kSubgroupHeader: {
                        // Subgroup needs at least track alias decoded before handoff.
                        const auto track_alias = TryDecodeUintV(initial_cursor);
                        if (!track_alias.has_value()) {
                            continue; // Need more bytes, will try again.
                        }
                        parsed_header = OnRecvSubgroup(*track_alias, *rx_ctx, stream_id);
                        break;
                    }
                    case StreamMessageType::kFetchHeader: {
                        // Fetch needs at least request ID decoded before handoff.
                        const auto request_id = TryDecodeUintV(initial_cursor);
                        if (!request_id.has_value()) {
                            continue; // Need more bytes, will try again.
                        }
                        parsed_header = OnRecvFetch(*request_id, *rx_ctx, stream_id);
                        break;
                    }
                    default:
                        QUICR_LOGGER_WARN(logger_,
                                          "Received start of stream with invalid header type {}, dropping",
                                          *initial_stream_type);
                        current_connection_->metrics.rx_stream_invalid_type++;

                        // TODO(tievens): Need to reset this stream as this is invalid.
                        return;
                }

                if (!parsed_header) {
                    // TODO: We ignore invalid parses for now, but set an expiry for how long we'll keep the stream
                    if (!rx_ctx->unknown_expiry_tick_ms) {
                        uint64_t age_ms = client_mode_ ? client_config_.unknown_stream_expiry_ms
                                                       : server_config_.unknown_stream_expiry_ms;
                        rx_ctx->unknown_expiry_tick_ms = static_cast<uint64_t>(
                          std::chrono::duration_cast<std::chrono::milliseconds>(tick_service_->get()).count());
                        rx_ctx->unknown_expiry_tick_ms += age_ms;

                        QUICR_LOGGER_INFO(
                          logger_,
                          "Setting stream_id: {} unknown expiry to {}ms (current time is {}ms)",
                          stream_id,
                          rx_ctx->unknown_expiry_tick_ms,
                          static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(tick_service_->get()).count()));
                    }

                    rx_ctx->unknown_expiry_tick_ms = 0;
                    break;
                }

            } else {
                rx_ctx->data_queue.PopFront();

                // fast processing for existing stream using weak pointer to subscribe handler
                if (auto sub_handler = rx_ctx->handler.lock()) {
                    try {
                        sub_handler->StreamDataRecv(stream_id, *data_opt);
                    } catch (const ProtocolViolationException& e) {
                        QUICR_LOGGER_ERROR(logger_, "Protocol violation on stream data recv: {}", e.reason);
                        throw ProtocolViolationException(e.reason);
                    } catch (std::exception& e) {
                        QUICR_LOGGER_ERROR(logger_, "Caught exception on stream data recv: {}", e.what());
                        throw e;
                    }
                } else {
                    QUICR_LOGGER_ERROR(
                      logger_,
                      "Received data on existing stream_id: {} with no handler anymore, resetting stream",
                      stream_id);

                    if (stream != nullptr) {
                        quic_transport_->CloseStream(current_connection_, stream, StreamOperation::kReset);
                    }
                }
            }
        } // end of for loop rx data queue
    } catch (const TransportException& e) {
        QUICR_LOGGER_INFO(logger_, "OnRecvStream: connection or stream no longer exists (error={})", e.what());
    } catch (const std::exception& e) {
        // NOTE: Whatever message was being processed when this was thrown (e.g. a control message) was not
        // removed from its buffer, so the connection cannot make forward progress on that stream if left
        // alone: any partially buffered data at the front will simply be retried (and fail again) the next
        // time data arrives, or - if no more data ever arrives - the connection will silently hang forever
        // instead of surfacing an error. Rather than letting that exception disappear into the transport's
        // callback notifier (which only logs and ignores it), close the connection so the failure is visible
        // and the peer is not left waiting indefinitely.
        QUICR_LOGGER_ERROR(logger_, "Caught exception on receiving stream, closing connection. (error={})", e.what());
        current_connection_->metrics.invalid_ctrl_stream_msg++;
        Disconnect();

        // TODO(tievens): Add metrics to track if this happens
    }

    void Session::OnStreamClosed(std::uint64_t stream_id,
                                 std::shared_ptr<StreamRxContext> rx_ctx,
                                 StreamClosedFlag flag)
    {
        QUICR_LOGGER_DEBUG(logger_, "Stream {} closed", stream_id);

        if (callbacks_ != nullptr) {
            callbacks_->OnStreamClosed(stream_id, flag);
        }

        {
            std::lock_guard lock(state_mutex_);
            stream_buffers.erase(stream_id);
        }

        try {
            std::unique_lock lock(state_mutex_);

            const auto req_it = request_by_stream.find(stream_id);
            if (req_it != request_by_stream.end()) {
                if (req_it->second.is_request_stream) {
                    if (flag == StreamClosedFlag::kStopSending) {
                        return;
                    }
                    const auto request_id = req_it->second.request_id;
                    request_by_stream.erase(req_it);

                    lock.unlock();
                    CloseRequestHandler(request_id, stream_id, flag);
                    return;
                }
                const auto handler_it = request_handlers.find(req_it->second.request_id);
                if (handler_it != request_handlers.end()) {
                    if (const auto handler = handler_it->second->Get<PublishTrackHandler>()) {
                        lock.unlock();
                        handler->StreamClosed(stream_id, flag != StreamClosedFlag::kFin);
                        return;
                    }
                }
            }

        } catch (const std::exception& e) {
            QUICR_LOGGER_ERROR(logger_, "Caught exception on stream closed: {}", e.what());
            return;
        }

        // TODO: Replace this check with control stream IDs check.
        if ((stream_id & 2) == 0) { // bidir
            switch (flag) {
                case StreamClosedFlag::kFin:
                    if (tx_ctrl_stream_ != nullptr && tx_ctrl_stream_->GetStreamId() == stream_id) {
                        throw ProtocolViolationException("Primary control stream FIN");
                    }
                    break;
                case StreamClosedFlag::kReset:
                    if (tx_ctrl_stream_ != nullptr && tx_ctrl_stream_->GetStreamId() == stream_id) {
                        throw ProtocolViolationException("Primary control stream RESET");
                    }
                    break;
                case StreamClosedFlag::kStopSending:
                    break;
            }

            return;
        }

        if (rx_ctx == nullptr) {
            return;
        }

        const auto handler_ptr = rx_ctx->handler.lock();
        if (handler_ptr == nullptr) {
            QUICR_LOGGER_WARN(logger_, "Received stream closed for unknown handler");
            return;
        }

        try {
            auto handler = handler_ptr.get();
            switch (flag) {
                case StreamClosedFlag::kFin:
                    if (handler->is_fetch_handler_) {
                        handler->SetStatus(FetchTrackHandler::Status::kDoneByFin);
                    }
                    handler->StreamClosed(stream_id, false);
                    break;
                case StreamClosedFlag::kReset:
                    if (handler->is_fetch_handler_) {
                        handler->SetStatus(FetchTrackHandler::Status::kDoneByReset);
                    }
                    handler->StreamClosed(stream_id, true);
                    break;
                case StreamClosedFlag::kStopSending:
                    break;
            }
        } catch (const ProtocolViolationException& e) {
            QUICR_LOGGER_ERROR(logger_, "Protocol violation on stream data recv: {}", e.reason);
            throw ProtocolViolationException(e.reason);
        } catch (const std::exception& e) {
            QUICR_LOGGER_ERROR(logger_, "Caught exception on stream data recv: {}", e.what());
            throw std::runtime_error("Internal error");
        }
    }

    bool Session::OnRecvSubgroup(std::uint64_t track_alias, StreamRxContext& rx_ctx, std::uint64_t stream_id)
    {
        auto sub_it = sub_by_recv_track_alias.find(track_alias);
        if ((sub_it == sub_by_recv_track_alias.end() || sub_it->second == nullptr)) {
            current_connection_->metrics.rx_stream_unknown_track_alias++;
            QUICR_LOGGER_WARN(
              logger_,
              "Received stream_header_subgroup to unknown subscribe track track_alias: {} stream: {}, ignored",
              track_alias,
              stream_id);

            return false;
        }

        const auto stream_it = stream_buffers.find(stream_id);
        if (stream_it == stream_buffers.end()) {
            QUICR_LOGGER_ERROR(logger_, "Missing expected pending stream buffer");
            return false;
        }
        auto initial_buffer = std::move(stream_it->second);
        stream_buffers.erase(stream_it);

        rx_ctx.is_new = false;
        rx_ctx.handler = sub_it->second;
        try {
            sub_it->second->StreamDataRecv(stream_id, std::move(initial_buffer));
        } catch (const std::exception& e) {
            QUICR_LOGGER_ERROR(
              logger_, "Encountered an error while receiving stream data (stream={}, error={})", stream_id, e.what());
        }
        return true;
    }

    bool Session::OnRecvFetch(std::uint64_t request_id, StreamRxContext& rx_ctx, std::uint64_t stream_id)
    {
        const auto fetch_it = request_handlers.find(request_id);
        if (fetch_it == request_handlers.end()) {
            // TODO: Metrics.
            QUICR_LOGGER_WARN(logger_,
                              "Received fetch_header to unknown fetch track request_id: {} stream: {}, ignored",
                              request_id,
                              stream_id);

            // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging
            return false;
        }

        if (auto h = fetch_it->second->Get<SubscribeTrackHandler>()) {
            const auto stream_it = stream_buffers.find(stream_id);
            if (stream_it == stream_buffers.end()) {
                QUICR_LOGGER_ERROR(logger_, "Missing expected pending stream buffer");
                return false;
            }
            auto initial_buffer = std::move(stream_it->second);
            stream_buffers.erase(stream_it);

            rx_ctx.is_new = false;
            rx_ctx.handler = h;
            try {
                h->StreamDataRecv(stream_id, std::move(initial_buffer));
            } catch (const std::exception& e) {
                QUICR_LOGGER_ERROR(logger_,
                                   "Encountered an error while receiving stream data (stream={}, error={})",
                                   stream_id,
                                   e.what());
            }
            return true;
        }

        return false;
    }

    std::shared_ptr<Stream> Session::CreateStream(std::uint64_t request_id, uint8_t priority)
    {
        const auto stream = quic_transport_->CreateDataStream(current_connection_, priority);

        // Streams report their own metrics, so the session needs to know which track each belongs to.
        std::lock_guard _(state_mutex_);
        request_by_stream[stream->GetStreamId()] = { .request_id = request_id, .is_request_stream = false };

        return stream;
    }

    void Session::OnRecvDgram()
    {
        for (int i = 0; i < kReadLoopMaxPerStream; i++) {
            auto data = quic_transport_->Dequeue(current_connection_);
            if (data && !data->empty() && data->size() > 3) {
                auto msg_type = data->front();

                // Message type needs to be either datagram header types or status types.
                if (!DatagramHeaderProperties::IsValid(msg_type)) {
                    QUICR_LOGGER_DEBUG(
                      logger_, "Received datagram that is not a supported datagram type, dropping: {}", msg_type);
                    current_connection_->metrics.rx_dgram_invalid_type++;
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

                auto sub_it = sub_by_recv_track_alias.find(track_alias);
                if (sub_it == sub_by_recv_track_alias.end()) {
                    current_connection_->metrics.rx_dgram_unknown_track_alias++;

                    QUICR_LOGGER_DEBUG(
                      logger_, "Received datagram to unknown subscribe track track alias: {}, ignored", track_alias);

                    // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                    continue;
                }

                QUICR_LOGGER_TRACE(logger_,
                                   "Received object datagram conn_id: {} track_alias: {} data size: {}",
                                   current_connection_->GetID(),
                                   track_alias,
                                   data->size());

                auto handler = static_cast<SubscribeTrackHandler*>(sub_it->second.get());

                try {
                    handler->DgramDataRecv(data);
                } catch (const std::exception& e) {
                    QUICR_LOGGER_ERROR(logger_, "Caught exception in ObjectStatusReceived. (error={})", e.what());
                }
            } else if (data) {
                current_connection_->metrics.rx_dgram_decode_failed++;

                QUICR_LOGGER_DEBUG(logger_,
                                   "Failed to decode datagram conn_id: {} size: {}",
                                   current_connection_->GetID(),
                                   data->size());
            }
        }
    }

    void Session::OnConnectionMetricsSampled(const MetricsTimeStamp sample_time,
                                             const QuicConnectionMetrics& quic_connection_metrics)
    {
        auto& conn = current_connection_;

        conn->metrics.last_sample_time = sample_time.time_since_epoch() / std::chrono::microseconds(1);
        conn->metrics.quic = quic_connection_metrics;

        if (callbacks_) {
            if (auto self = weak_from_this().lock()) {
                callbacks_->MetricsSampled(self, conn->metrics);
            }
        }

        // The connection sample closes the period, so every handler's counters are final by now.
        // Handlers are sampled whether or not any of their streams reported, so a track that was idle
        // for the period still gets a sample.
        const auto last_sample_time = conn->metrics.last_sample_time;
        for (const auto& [_, req] : request_handlers) {
            if (auto h = req->Get<SubscribeTrackHandler>()) {
                h->subscribe_track_metrics_.last_sample_time = last_sample_time;
                h->MetricsSampled(h->subscribe_track_metrics_);
            } else if (auto h = req->Get<PublishTrackHandler>()) {
                h->publish_track_metrics_.last_sample_time = last_sample_time;
                h->MetricsSampled(h->publish_track_metrics_);

                // Unlike the counters beside them, these describe a period, so the next starts empty.
                h->publish_track_metrics_.quic.tx_callback_ms.Clear();
                h->publish_track_metrics_.quic.tx_object_duration_us.Clear();
                h->publish_track_metrics_.quic.tx_queue_size.Clear();
            }
        }
    }

    void Session::OnStreamMetricsStampled(const MetricsTimeStamp,
                                          const std::uint64_t stream_id,
                                          const QuicStreamMetrics& quic_stream_metrics,
                                          const bool is_final)
    {
        std::lock_guard _(state_mutex_);

        const auto req_it = request_by_stream.find(stream_id);
        if (req_it == request_by_stream.end()) {
            return;
        }

        if (const auto req_handler_it = request_handlers.find(req_it->second.request_id);
            req_handler_it != request_handlers.end()) {

            // A track can be carried by several streams, so these accumulate across the period and
            // are handed to the handler when the connection sample closes it.
            if (auto h = req_handler_it->second->Get<SubscribeTrackHandler>()) {
                h->subscribe_track_metrics_.bytes_received += quic_stream_metrics.rx_stream_bytes;

            } else if (auto h = req_handler_it->second->Get<PublishTrackHandler>()) {
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

    TransportError Session::Enqueue(const std::shared_ptr<Stream>& stream,
                                    std::shared_ptr<const std::vector<uint8_t>> bytes,
                                    const uint8_t priority,
                                    const uint32_t ttl_ms,
                                    const Transport::EnqueueFlags flags)
    {
        if (!flags.use_reliable) {
            return quic_transport_->EnqueueDatagram(current_connection_, std::move(bytes), priority, ttl_ms);
        }

        return quic_transport_->Enqueue(current_connection_, stream, std::move(bytes), priority, ttl_ms, flags);
    }

    // -- Resolve Methods --

    void Session::ResolveFetch(uint64_t request_id,
                               std::optional<messages::GroupOrder> group_order,
                               const FetchResponse& response)
    {
        const auto request_it = recv_req_id.find(request_id);
        if (request_it == recv_req_id.end() || request_it->second.stream == nullptr) {
            QUICR_LOGGER_ERROR(logger_,
                               "Cannot resolve FETCH without its request stream conn_id: {} request_id: {}",
                               current_connection_->GetID(),
                               request_id);
            return;
        }

        SendFetchOk(
          request_it->second.stream, response.publisher_default_group_order, false, response.largest_location.value());
    }

    std::shared_ptr<Stream> Session::FindSubscribeNamespaceStream(const TrackNamespace& track_namespace) const
    {
        for (const auto& [_, handler] : request_handlers) {
            if (auto h = handler->Get<SubscribeNamespaceHandler>()) {
                auto request_stream = h->GetRequestStream();
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

    std::shared_ptr<Stream> Session::ResponseStream(const std::uint64_t request_id) const
    {
        const auto recv_it = recv_req_id.find(request_id);
        if (recv_it != recv_req_id.end() && recv_it->second.stream != nullptr) {
            return recv_it->second.stream;
        }

        return tx_ctrl_stream_;
    }

    // -- Server Relay Methods --

    void Session::BindPublisherTrack(std::uint64_t src_id,
                                     uint64_t request_id,
                                     const std::shared_ptr<PublishTrackHandler>& track_handler,
                                     bool ephemeral)
    {
        // Generate track alias
        const auto& tfn = track_handler->GetFullTrackName();
        auto th = TrackHash(tfn);

        std::unique_lock<std::mutex> lock(state_mutex_);

        if (!track_handler->GetTrackAlias().has_value()) {
            track_handler->SetTrackAlias(th.track_fullname_hash);
        }

        track_handler->SetRequestId(request_id);
        request_handlers[request_id] = track_handler;

        track_handler->connection_id_ = current_connection_->GetID();

        const auto req_it = recv_req_id.find(request_id);
        if (req_it != recv_req_id.end() && req_it->second.stream) {
            track_handler->SetRequestStream(req_it->second.stream);
        }

        // Set this transport as the one for the publisher to use.
        track_handler->SetTransport(GetSharedPtr());

        if (!ephemeral) {
            // Hold onto track handler
            pub_tracks_by_name[th.track_namespace_hash][th.track_name_hash] = track_handler;
            pub_tracks_by_track_alias[th.track_fullname_hash][src_id] = track_handler;
        }

        lock.unlock();
        track_handler->SetStatus(PublishTrackHandler::Status::kOk);
    }

    void Session::UnbindPublisherTrack(std::uint64_t src_id,
                                       const std::shared_ptr<PublishTrackHandler>& track_handler,
                                       bool send_publish_done)
    {
        std::unique_lock lock(state_mutex_);

        auto th = TrackHash(track_handler->GetFullTrackName());
        QUICR_LOGGER_DEBUG(
          logger_,
          "Server publish track conn_id: {} full_name_hash: {} namespace_hash: {} name_hash: {} unbind",
          current_connection_->GetID(),
          th.track_fullname_hash,
          th.track_namespace_hash,
          th.track_name_hash);

        request_handlers.erase(*track_handler->GetRequestId());
        pub_tracks_by_name[th.track_namespace_hash].erase(th.track_name_hash);

        pub_tracks_by_track_alias[th.track_fullname_hash].erase(src_id);
        if (pub_tracks_by_track_alias[th.track_fullname_hash].empty()) {
            pub_tracks_by_track_alias.erase(th.track_fullname_hash);
        }

        if (pub_tracks_by_name.count(th.track_namespace_hash) == 0) {
            QUICR_LOGGER_DEBUG(logger_,
                               "Server publish track conn_id: {} full_name_hash: {} namespace_hash: {} unbind",
                               current_connection_->GetID(),
                               th.track_fullname_hash,
                               th.track_namespace_hash);

            pub_tracks_by_name.erase(th.track_namespace_hash);
        }

        track_handler->EndAllSubgroups();

        const auto request_stream = track_handler->GetRequestStream();
        if (send_publish_done && request_stream != nullptr) {
            SendPublishDone(request_stream,
                            track_handler->GetRequestId().value(),
                            messages::PublishDoneStatusCode::kSubscribtionEnded,
                            "No publishers");
        }

        lock.unlock();

        // The subscriber this handler served is gone, so further publishes must be refused rather
        // than opening streams on a track the session no longer routes.
        track_handler->SetStatus(PublishTrackHandler::Status::kNoSubscribers);
    }

    void Session::BindFetchTrack(std::shared_ptr<PublishFetchHandler> track_handler)
    {
        const std::uint64_t request_id = *track_handler->GetRequestId();
        QUICR_LOGGER_INFO(
          logger_, "Publish fetch track conn_id: {} subscribe: {}", current_connection_->GetID(), request_id);

        std::lock_guard lock(state_mutex_);

        track_handler->SetStatus(PublishFetchHandler::Status::kOk);
        track_handler->connection_id_ = current_connection_->GetID();

        track_handler->SetTransport(GetSharedPtr());

        // Hold ref to track handler
        pub_fetch_tracks_by_request_id[request_id] = track_handler;
    }

    void Session::UnbindFetchTrack(const std::shared_ptr<PublishFetchHandler>& track_handler)
    {
        std::lock_guard lock(state_mutex_);

        auto request_id = *track_handler->GetRequestId();
        QUICR_LOGGER_DEBUG(logger_,
                           "Server publish fetch track conn_id: {} subscribe id: {} unbind",
                           current_connection_->GetID(),
                           request_id);

        pub_fetch_tracks_by_request_id.erase(request_id);

        // Drain before closing: the fetch's objects are queued on its stream, and unbinding is the
        // normal end of a completed fetch rather than an abort.
        track_handler->EndFetch();
    }

    // -- Private --

    bool Session::ProcessCtrlMessage(messages::ControlMessageType msg_type, BytesSpan msg_bytes)
    {
        switch (msg_type) {
            case messages::ControlMessageType::kSetup: {
                const auto setup_options = messages::Message::ParseField<messages::KeyValuePairs>(msg_bytes);

                std::string endpoint_id = "Unknown Endpoint ID";
                if (auto endpoint = setup_options.GetOptional<std::string>(messages::SetupOptionType::kEndpointId)) {
                    endpoint_id = *endpoint;
                }

                if (client_mode_) {
                    if (auto callbacks = std::dynamic_pointer_cast<ClientCallbacks>(callbacks_)) {
                        callbacks->ServerSetupReceived(GetSharedPtr(), { 0, endpoint_id })
                          .Resolve([self = GetSharedPtr()](const auto& result) {
                              if (result) {
                                  return;
                              }

                              const auto& [code, reason] = result.error();
                              QUICR_LOGGER_ERROR(self->logger_,
                                                 "Server setup rejected conn_id: {} code: {} reason: {}",
                                                 self->current_connection_->GetID(),
                                                 static_cast<std::uint64_t>(code),
                                                 reason.value_or("unknown"));
                          });
                    }
                } else {
                    if (auto callbacks = std::dynamic_pointer_cast<ServerCallbacks>(callbacks_)) {
                        callbacks->ClientSetupReceived(GetSharedPtr(), { endpoint_id })
                          .Resolve([self = GetSharedPtr()](const auto& result) {
                              if (!result) {
                                  const auto& [code, reason] = result.error();
                                  QUICR_LOGGER_ERROR(self->logger_,
                                                     "Client setup rejected, not sending SETUP conn_id: {} code: {} "
                                                     "reason: {}",
                                                     self->current_connection_->GetID(),
                                                     static_cast<std::uint64_t>(code),
                                                     reason.value_or("unknown"));
                                  return;
                              }

                              self->SendSetup();
                          });
                    }
                }

                SetStatus(Status::kReady);

                QUICR_LOGGER_INFO(
                  logger_, "Setup received conn_id: {} from: {}", current_connection_->GetID(), endpoint_id);

                return true;
            }
            case messages::ControlMessageType::kGoaway: {
                // Session GOAWAY.
                const auto new_session_uri = messages::Message::ParseField<Bytes>(msg_bytes);
                std::string new_sess_uri(new_session_uri.begin(), new_session_uri.end());
                QUICR_LOGGER_INFO(logger_, "Received session goaway new session uri: {}", new_sess_uri);
                return true;
            }
            default: {
                QUICR_LOGGER_ERROR(
                  logger_, "Unsupported session control message, type: {}", static_cast<std::uint64_t>(msg_type));
                throw ProtocolViolationException("Unsupported session control message");
            }
        }
    }

    bool Session::ProcessRequestMessage(const std::shared_ptr<Stream>& stream,
                                        messages::ControlMessageType msg_type,
                                        BytesSpan msg_bytes)
    {
        switch (msg_type) {
            case messages::ControlMessageType::kSubscribe: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto track_namespace = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                const auto track_name = messages::Message::ParseField<Bytes>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                auto tfn = FullTrackName{ track_namespace, track_name };
                auto th = TrackHash(tfn);
                recv_req_id[request_id] = { .track_full_name = tfn, .track_hash = th, .stream = stream };
                request_by_stream[stream->GetStreamId()] = { .request_id = request_id, .is_request_stream = true };

                if (client_mode_) {
                    auto ptd = GetPubTrackHandler(th);
                    if (ptd == nullptr) {
                        QUICR_LOGGER_WARN(logger_,
                                          "Received subscribe unknown publish track conn_id: {} namespace hash: {} "
                                          "name hash: {} request_id: {}",
                                          current_connection_->GetID(),
                                          th.track_namespace_hash,
                                          th.track_name_hash,
                                          request_id);

                        SendRequestError(
                          stream, request_id, ErrorCode::kDoesNotExist, 0ms, "Published track not found");
                        return true;
                    }

                    ptd->SetRequestStream(stream);

                    SendSubscribeOk(ResponseStream(request_id),
                                    request_id,
                                    ptd->GetTrackAlias().value(),
                                    kSubscribeExpires,
                                    std::nullopt,
                                    messages::GroupOrder::kAscending);

                    ptd->SetRequestId(request_id);
                    ptd->SetTrackAlias(ptd->GetTrackAlias().value());
                    ptd->SetStatus(PublishTrackHandler::Status::kOk);

                    return true;
                }

                auto delivery_timeout = parameters.Get<std::uint64_t>(messages::ParameterType::kDeliveryTimeout);
                auto priority = parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority);
                auto group_order = parameters.GetOptional<messages::GroupOrder>(messages::ParameterType::kGroupOrder);
                const auto publisher_default_group_order = messages::GroupOrder::kAscending;
                auto forward = parameters.Get<bool>(messages::ParameterType::kForward);
                auto new_group_request_id =
                  parameters.GetOptional<std::uint64_t>(messages::ParameterType::kNewGroupRequest);

                messages::Filter filter;
                if (parameters.Contains(messages::ParameterType::kSubscriptionFilter)) {
                    filter = parameters.GetFilter(messages::FilterType::kSubscriptionFilter);
                } else if (parameters.Contains(messages::ParameterType::kTrackFilter)) {
                    filter = parameters.GetFilter(messages::FilterType::kTrackFilter);
                }

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
                                          })
                      .Resolve([=, self = GetSharedPtr()](const auto& result) {
                          if (!result) {
                              const auto& [_, reason] = result.error();

                              // TODO: Should server not send if publisher initiated?
                              self->SendRequestError(self->ResponseStream(request_id),
                                                     request_id,
                                                     ErrorCode::kInternalError,
                                                     0ms,
                                                     reason.value_or("Internal error"));

                              return;
                          }

                          if (self->client_mode_) {
                              self->SendSubscribeOk(self->ResponseStream(request_id),
                                                    request_id,
                                                    th.track_fullname_hash,
                                                    kSubscribeExpires,
                                                    result->largest_location,
                                                    result->publisher_default_group_order);
                          } else {

                              // Save the latest state for joining fetch.
                              auto req_it = self->recv_req_id.find(request_id);
                              if (req_it == self->recv_req_id.end()) {
                                  QUICR_LOGGER_WARN(self->logger_,
                                                    "Resolve subscribe has no request_id: {} conn_id: {} "
                                                    "track_alias: {}",
                                                    request_id,
                                                    self->current_connection_->GetID(),
                                                    th.track_fullname_hash);
                                  return;
                              }

                              req_it->second.largest_location = result->largest_location;

                              if (!result->is_publisher_initiated) {
                                  self->SendSubscribeOk(self->ResponseStream(request_id),
                                                        request_id,
                                                        th.track_fullname_hash,
                                                        kSubscribeExpires,
                                                        result->largest_location,
                                                        result->publisher_default_group_order);
                              }
                          }

                          if (!new_group_request_id.has_value()) {
                              return;
                          }

                          callbacks->NewGroupRequested(tfn, *new_group_request_id)
                            .Resolve([=](const auto& new_group_result) {
                                if (new_group_result) {
                                    return;
                                }

                                const auto& [code, reason] = new_group_result.error();
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
                const auto request_it = request_by_stream.find(stream->GetStreamId());
                if (request_it == request_by_stream.end()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received SUBSCRIBE_OK for unknown request conn_id: {} stream_id: {}, ignored",
                                      current_connection_->GetID(),
                                      stream->GetStreamId());
                    return true;
                }
                const auto request_id = request_it->second.request_id;

                const auto track_alias = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);
                const auto track_extensions = messages::Message::ParseField<messages::TrackExtensions>(msg_bytes);

                auto sub_it = request_handlers.find(request_id);
                if (sub_it == request_handlers.end()) {
                    QUICR_LOGGER_WARN(
                      logger_,
                      "Received subscribe ok to unknown subscribe track conn_id: {} request_id: {}, ignored",
                      current_connection_->GetID(),
                      request_id);
                    return true;
                }

                if (auto sub_handler = sub_it->second->Get<SubscribeTrackHandler>()) {
                    const auto publisher_default_group_order =
                      track_extensions
                        .GetOptional<messages::GroupOrder>(messages::ExtensionType::kDefaultPublisherGroupOrder)
                        .value_or(messages::GroupOrder::kAscending);

                    if (client_mode_) {
                        if (parameters.Contains(messages::ParameterType::kLargestObject)) {
                            sub_handler->SetLatestLocation(
                              parameters.Get<messages::Location>(messages::ParameterType::kLargestObject));
                        }
                        sub_handler->SupportNewGroupRequest(true);
                    }

                    sub_handler->SetReceivedTrackAlias(track_alias);
                    sub_handler->SetPublisherDefaultGroupOrder(publisher_default_group_order);
                    sub_handler->SetStatus(SubscribeTrackHandler::Status::kOk);
                    sub_by_recv_track_alias[track_alias] = sub_handler;
                }

                return true;
            }
            case messages::ControlMessageType::kRequestOk: {
                // What request is this for?
                const auto req_it = request_by_stream.find(stream->GetStreamId());
                if (req_it == request_by_stream.end()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received REQUEST_OK for unknown request conn_id: {} stream_id: {}, ignored",
                                      current_connection_->GetID(),
                                      stream->GetStreamId());
                    return true;
                }
                const auto request_id = req_it->second.request_id;

                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);
                const auto track_properties = Message::ParseField<messages::TrackExtensions>(msg_bytes);
                // TODO: If track properties exist on anything other than TRACK_STATUS_OK, protocol violation.
                // We can't tell here.

                auto track_it = request_handlers.find(request_id);
                if (track_it == request_handlers.end()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received REQUEST_OK to unknown track conn_id: {} request_id: {}, ignored",
                                      current_connection_->GetID(),
                                      request_id);
                    return true;
                }

                track_it->second->RequestOkReceived(parameters);
                return true;
            }
            case messages::ControlMessageType::kRequestError: {
                const auto request_it = request_by_stream.find(stream->GetStreamId());
                if (request_it == request_by_stream.end()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received REQUEST_ERROR for unknown request conn_id: {} stream_id: {}, ignored",
                                      current_connection_->GetID(),
                                      stream->GetStreamId());
                    return true;
                }
                const auto request_id = request_it->second.request_id;
                const auto error_code = messages::Message::ParseField<ErrorCode>(msg_bytes);
                [[maybe_unused]] const auto retry_interval = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto error_reason = messages::Message::ParseField<Bytes>(msg_bytes);

                std::string reason_str(error_reason.begin(), error_reason.end());

                if (client_mode_) {
                    auto track_it = request_handlers.find(request_id);
                    if (track_it == request_handlers.end()) {
                        QUICR_LOGGER_WARN(logger_,
                                          "Received REQUEST_ERROR to unknown track conn_id: {} request_id: {}, ignored",
                                          current_connection_->GetID(),
                                          request_id);
                        return true;
                    }

                    track_it->second->RequestError(error_code, reason_str);
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

                auto tfn = FullTrackName{ track_namespace, track_name };

                auto th = TrackHash(tfn);
                QUICR_LOGGER_DEBUG(logger_,
                                   "Received track status request_id: {} for full name hash: {}",
                                   request_id,
                                   th.track_fullname_hash);

                request_by_stream[stream->GetStreamId()] = { .request_id = request_id, .is_request_stream = true };

                if (callbacks_) {
                    callbacks_->TrackStatusReceived(GetSharedPtr(), request_id, tfn)
                      .Resolve([=, self = GetSharedPtr()](const auto& result) {
                          if (!result) {
                              const auto& [code, reason] = result.error();

                              switch (code) {
                                  case RequestErrorCode::kDoesNotExist:
                                      self->SendRequestError(self->ResponseStream(request_id),
                                                             request_id,
                                                             ErrorCode::kDoesNotExist,
                                                             0ms, // TODO: Figure out retry interval
                                                             reason.value_or("Track does not exist"));
                                      break;
                                  case RequestErrorCode::kUnauthorized:
                                      self->SendRequestError(self->ResponseStream(request_id),
                                                             request_id,
                                                             ErrorCode::kUnauthorized,
                                                             0ms, // TODO: Figure out retry interval
                                                             reason.value_or("Unauthorized"));
                                      break;
                                  default:
                                      self->SendRequestError(self->ResponseStream(request_id),
                                                             request_id,
                                                             ErrorCode::kInternalError,
                                                             0ms,
                                                             "Internal error");
                                      break;
                              }

                              return;
                          }

                          // TODO: TrackProperties should be in the subscribe_response.
                          self->SendTrackStatusOk(
                            self->ResponseStream(request_id), result.value().largest_location, TrackExtensions());
                      });
                }

                return true;
            }
            case messages::ControlMessageType::kPublishNamespace: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto track_namespace = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                [[maybe_unused]] const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                recv_req_id[request_id] = { .track_full_name = { track_namespace, {} },
                                            .track_hash = TrackHash({ track_namespace, {} }),
                                            .stream = stream };
                recv_publish_namespaces.push_back(request_id);
                request_by_stream[stream->GetStreamId()] = { .request_id = request_id, .is_request_stream = true };

                if (callbacks_) {
                    callbacks_->PublishNamespaceReceived(GetSharedPtr(), track_namespace, { .request_id = request_id })
                      .Resolve([=, self = GetSharedPtr()](const auto& result) {
                          if (!result) {
                              // TODO: Send announce error.

                              return;
                          }

                          auto response_stream = self->ResponseStream(request_id);
                          if (const auto pub_ns_it = self->request_handlers.find(request_id);
                              pub_ns_it != self->request_handlers.end()) {
                              if (auto handler_stream = pub_ns_it->second->GetRequestStream()) {
                                  response_stream = std::move(handler_stream);
                              }
                          }

                          self->SendPublishNamespaceOk(response_stream);

                          const auto sub_stream = self->FindSubscribeNamespaceStream(track_namespace);
                          if (sub_stream == nullptr) {
                              QUICR_LOGGER_WARN(self->logger_,
                                                "No subscribe namespace stream for publish namespace conn_id: {}",
                                                self->current_connection_->GetID());
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

                if (msg_type == messages::ControlMessageType::kSubscribeNamespace) {
                    // TODO: Figure out what we should do with these in the case of Subscribe Namespace.
                    [[maybe_unused]] const auto subscribe_options =
                      messages::Message::ParseField<messages::SubscribeOptions>(msg_bytes);
                }

                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                messages::Filter filter;
                if (parameters.Contains(messages::ParameterType::kSubscriptionFilter)) {
                    filter = parameters.GetFilter(messages::FilterType::kSubscriptionFilter);
                } else if (parameters.Contains(messages::ParameterType::kTrackFilter)) {
                    filter = parameters.GetFilter(messages::FilterType::kTrackFilter);
                }

                request_by_stream[stream->GetStreamId()] = { .request_id = request_id, .is_request_stream = true };

                if (auto callbacks = std::dynamic_pointer_cast<ServerCallbacks>(callbacks_)) {
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

                    reply.Resolve([=, self = GetSharedPtr()](const auto& result) {
                        if (!result) {
                            const auto& [code, reason] = result.error();
                            self->SendRequestError(
                              stream, request_id, ToErrorCode(code), 0ms, reason.value_or("Internal error"));

                            return;
                        }

                        self->SendSubscribeNamespaceOk(stream);

                        // Fan out PUBLISH_NAMESPACE for matching namespaces.
                        for (const auto& name_space : result.value()) {
                            const auto match = track_namespace_prefix.IsPrefixOf(name_space);
                            if (match == std::partial_ordering::unordered || match == std::partial_ordering::less) {
                                QUICR_LOGGER_WARN(self->logger_, "Dropping non prefix match");
                                continue;
                            }

                            auto pub_ns_request_id = self->GetNextRequestID();
                            self->SendPublishNamespace(stream, pub_ns_request_id, name_space);
                        }
                    });
                }

                return true;
            }
            case messages::ControlMessageType::kNamespaceDone: {
                if (client_mode_) {
                    QUICR_LOGGER_ERROR(
                      logger_, "Unsupported MOQT message type: {}, bad stream", static_cast<uint64_t>(msg_type));
                    return false;
                }

                if (auto callbacks = std::dynamic_pointer_cast<ServerCallbacks>(callbacks_)) {
                    const auto track_namespace_suffix = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                    callbacks->UnsubscribeNamespaceReceived(GetSharedPtr(), track_namespace_suffix)
                      .Resolve([self = GetSharedPtr()](const auto& result) {
                          if (result) {
                              return;
                          }

                          const auto& [code, reason] = result.error();
                          QUICR_LOGGER_ERROR(self->logger_,
                                             "Unsubscribe namespace failed conn_id: {} code: {} reason: {}",
                                             self->current_connection_->GetID(),
                                             static_cast<std::uint64_t>(code),
                                             reason.value_or("unknown"));
                      });
                }
                return true;
            }
            case messages::ControlMessageType::kPublishDone: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                [[maybe_unused]] const auto status_code =
                  messages::Message::ParseField<messages::PublishDoneStatusCode>(msg_bytes);
                [[maybe_unused]] const auto stream_count = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                [[maybe_unused]] const auto error_reason = messages::Message::ParseField<Bytes>(msg_bytes);

                auto sub_it = request_handlers.find(request_id);
                if (sub_it == request_handlers.end()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received publish done to unknown request_id conn_id: {} request_id: {}",
                                      current_connection_->GetID(),
                                      request_id);
                    return true;
                }

                if (client_mode_) {
                    if (auto h = sub_it->second->Get<SubscribeTrackHandler>()) {
                        h->SetStatus(SubscribeTrackHandler::Status::kNotSubscribed);
                    }
                    return true;
                }

                auto tfn = sub_it->second->GetFullTrackName();
                auto th = TrackHash(tfn);

                QUICR_LOGGER_INFO(logger_,
                                  "Received publish done conn_id: {} request_id: {} track namespace hash: {} "
                                  "name hash: {} track alias: {}",
                                  current_connection_->GetID(),
                                  request_id,
                                  th.track_namespace_hash,
                                  th.track_name_hash,
                                  th.track_fullname_hash);

                if (auto h = sub_it->second->Get<SubscribeTrackHandler>()) {
                    h->SetStatus(SubscribeTrackHandler::Status::kNotSubscribed);
                    if (auto callbacks = std::dynamic_pointer_cast<ServerCallbacks>(callbacks_)) {
                        callbacks->PublishDoneReceived(GetSharedPtr(), request_id)
                          .Resolve([request_id, self = GetSharedPtr()](const auto& result) {
                              if (result) {
                                  return;
                              }

                              const auto& [code, reason] = result.error();
                              QUICR_LOGGER_ERROR(self->logger_,
                                                 "Publish done failed conn_id: {} request_id: {} code: {} reason: {}",
                                                 self->current_connection_->GetID(),
                                                 request_id,
                                                 static_cast<std::uint64_t>(code),
                                                 reason.value_or("unknown"));
                          });
                    }
                }

                recv_req_id.erase(request_id);
                return true;
            }
            case messages::ControlMessageType::kGoaway: {
                // Request GOAWAY.
                const auto new_session_uri = messages::Message::ParseField<Bytes>(msg_bytes);
                std::string new_sess_uri(new_session_uri.begin(), new_session_uri.end());
                QUICR_LOGGER_INFO(logger_, "Received request goaway new session uri: {}", new_sess_uri);
                return true;
            }
            case messages::ControlMessageType::kFetchOk: {
                [[maybe_unused]] const auto end_of_track = messages::Message::ParseField<std::uint8_t>(msg_bytes);
                const auto end_location = messages::Message::ParseField<messages::Location>(msg_bytes);
                [[maybe_unused]] const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);
                const auto track_extensions = messages::Message::ParseField<messages::TrackExtensions>(msg_bytes);

                const auto request_it = request_by_stream.find(stream->GetStreamId());
                if (request_it == request_by_stream.end()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received FETCH_OK for unknown request conn_id: {} stream_id: {}, ignored",
                                      current_connection_->GetID(),
                                      stream->GetStreamId());
                    return true;
                }
                const auto request_id = request_it->second.request_id;

                auto fetch_it = request_handlers.find(request_id);
                if (fetch_it == request_handlers.end()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received fetch ok for unknown fetch track conn_id: {} request_id: {}, ignored",
                                      current_connection_->GetID(),
                                      request_id);
                    return true;
                }

                if (auto h = fetch_it->second->Get<FetchTrackHandler>()) {
                    const auto publisher_default_group_order =
                      track_extensions
                        .GetOptional<messages::GroupOrder>(messages::ExtensionType::kDefaultPublisherGroupOrder)
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

                        recv_req_id[request_id] = {
                            .track_full_name = tfn,
                            .track_hash = th,
                            .stream = stream,
                        };
                        request_by_stream[stream->GetStreamId()] = { .request_id = request_id,
                                                                     .is_request_stream = true };

                        messages::FetchEndLocation end_location;
                        end_location.group = end.group;
                        if (end.object == 0) {
                            end_location.object = std::nullopt;
                        } else {
                            end_location.object = end.object - 1;
                        }

                        auto priority = parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority);
                        auto group_order =
                          parameters.GetOptional<messages::GroupOrder>(messages::ParameterType::kGroupOrder);

                        if (callbacks_) {
                            StandaloneFetchAttributes attrs = {
                                .priority = priority,
                                .group_order = group_order,
                                .publisher_default_group_order = messages::GroupOrder::kAscending,
                                .start_location = start,
                                .end_location = end_location,
                            };

                            callbacks_->StandaloneFetchReceived(GetSharedPtr(), request_id, tfn, attrs)
                              .Resolve([=, self = GetSharedPtr()](const auto& result) {
                                  if (!result) {
                                      const auto& [code, reason] = result.error();

                                      ErrorCode error_code = ErrorCode::kInternalError;
                                      switch (code) {
                                          case FetchErrorCode::kInvalidRange:
                                              error_code = ErrorCode::kInvalidRange;
                                              break;

                                          default:
                                              break;
                                      }

                                      self->SendRequestError(
                                        stream, request_id, error_code, 0ms, reason.value_or("Internal error"));

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

                        recv_req_id[request_id] = {
                            .track_full_name = tfn,
                            .track_hash = th,
                            .stream = stream,
                        };
                        request_by_stream[stream->GetStreamId()] = { .request_id = request_id,
                                                                     .is_request_stream = true };

                        auto priority = parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority);
                        auto group_order =
                          parameters.GetOptional<messages::GroupOrder>(messages::ParameterType::kGroupOrder);

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
                              .Resolve([=, self = GetSharedPtr()](const auto& result) {
                                  if (!result) {
                                      const auto& [code, reason] = result.error();

                                      ErrorCode error_code = ErrorCode::kInternalError;
                                      switch (code) {
                                          case FetchErrorCode::kInvalidRange:
                                              error_code = ErrorCode::kInvalidRange;
                                              break;

                                          default:
                                              break;
                                      }

                                      self->SendRequestError(
                                        stream, request_id, error_code, 0ms, reason.value_or("Internal error"));

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
                                                 .largest_object = parameters.GetOptional<Location>(
                                                   messages::ParameterType::kLargestObject),
                                                 .forward = ResolveForward(parameters, true),
                                                 .default_publisher_group_order = default_publisher_group_order,
                                                 .dynamic_groups = dynamic_groups,
                                                 .default_publisher_priority = default_publisher_priority,
                                                 .max_cache_duration = max_cache_duration,
                                                 .delivery_timeout = delivery_timeout,
                                                 .track_properties = std::move(track_extensions) };

                auto th = TrackHash(publish.track_full_name);
                recv_req_id[request_id] = { .track_full_name = publish.track_full_name,
                                            .track_hash = th,
                                            .stream = stream };
                request_by_stream[stream->GetStreamId()] = { .request_id = request_id, .is_request_stream = true };

                std::weak_ptr<SubscribeNamespaceHandler> sub_ns_handler;
                if (client_mode_) {
                    for (auto& [_, track] : request_handlers) {
                        if (auto h = track->Get<SubscribeNamespaceHandler>()) {
                            if (h->GetPrefix().HasSamePrefix(publish.track_full_name.name_space)) {
                                sub_ns_handler = h;
                                break;
                            }
                        }
                    }
                }

                if (callbacks_) {
                    callbacks_->PublishReceived(GetSharedPtr(), request_id, publish, sub_ns_handler)
                      .Resolve([=, self = GetSharedPtr()](const auto& result) {
                          if (!result) {
                              const auto& [code, reason] = result.error();

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

                              self->SendRequestError(self->ResponseStream(request_id),
                                                     request_id,
                                                     error_code,
                                                     0ms,
                                                     reason.value_or("unknown"));

                              return;
                          }

                          // Update the handler to correctly work with publisher initiated subscribe
                          if (result->handler) {
                              result->handler->SetPublishInitiated();

                              result->handler->SetConnectionId(self->current_connection_->GetID());
                              result->handler->SetRequestId(request_id);
                              result->handler->SetReceivedTrackAlias(publish.track_alias);
                              result->handler->SetPriority(publish.default_publisher_priority);
                              // TODO: Optional delivery timeout?
                              const std::uint64_t delivery_timeout_ms = publish.delivery_timeout.value_or(0);
                              result->handler->SetDeliveryTimeout(std::chrono::milliseconds(delivery_timeout_ms));
                              result->handler->SetPublisherDefaultGroupOrder(publish.default_publisher_group_order);
                              result->handler->SupportNewGroupRequest(publish.dynamic_groups);

                              self->SubscribeTrack(result->handler);
                          }

                          self->SendPublishOk(self->ResponseStream(request_id), result->attributes);
                      });
                }

                return true;
            }
            case messages::ControlMessageType::kRequestUpdate: {
                const auto update_request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto request_it = request_by_stream.find(stream->GetStreamId());
                if (request_it == request_by_stream.end()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received REQUEST_UPDATE on unknown request stream conn_id: {} stream_id: {} "
                                      "update_request_id: {}, ignored",
                                      current_connection_->GetID(),
                                      stream->GetStreamId(),
                                      update_request_id);
                    return true;
                }
                const auto request_id = request_it->second.request_id;
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                if (client_mode_) {
                    auto track_it = request_handlers.find(request_id);
                    if (track_it == request_handlers.end()) {
                        QUICR_LOGGER_WARN(logger_,
                                          "Received REQUEST_UPDATE to unknown track conn_id: {} request_id: {}, "
                                          "ignored",
                                          current_connection_->GetID(),
                                          request_id);
                        return true;
                    }

                    track_it->second->RequestUpdateReceived(parameters)
                      .Resolve([request_id, self = GetSharedPtr()](const auto& result) {
                          std::lock_guard lock(self->state_mutex_);

                          const auto handler_it = self->request_handlers.find(request_id);
                          if (handler_it == self->request_handlers.end()) {
                              QUICR_LOGGER_ERROR(
                                self->logger_, "Resolve REQUEST_UPDATE for request {} had no handler", request_id);
                              return;
                          }

                          const auto request_stream = handler_it->second->GetRequestStream();
                          if (request_stream == nullptr) {
                              QUICR_LOGGER_WARN(self->logger_,
                                                "Resolve REQUEST_UPDATE missing handler request stream conn_id: {} "
                                                "request_id: {}",
                                                self->current_connection_->GetID(),
                                                request_id);
                              return;
                          }

                          if (!result) {
                              const auto& [code, reason] = result.error();
                              self->SendRequestError(
                                request_stream, request_id, code, 0ms, reason.value_or("Request update rejected"));
                              return;
                          }

                          QUICR_LOGGER_DEBUG(self->logger_, "Request update resolved req_id: {}", request_id);

                          self->SendRequestOk(request_stream, result.value());
                      });
                    return true;
                }

                auto sub_ctx_it = recv_req_id.find(request_id);
                if (sub_ctx_it == recv_req_id.end()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received subscribe_update for unknown subscription conn_id: {} request_id: {}",
                                      current_connection_->GetID(),
                                      request_id);

                    SendRequestError(
                      stream, request_id, ErrorCode::kDoesNotExist, 0ms, "Subscription not found", false);
                    return true;
                }

                [[maybe_unused]] auto delivery_timeout =
                  parameters.Get<std::uint64_t>(messages::ParameterType::kDeliveryTimeout);
                [[maybe_unused]] auto priority = parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority);
                auto forward = parameters.Get<bool>(messages::ParameterType::kForward);
                auto new_group_request_id =
                  parameters.GetOptional<std::uint64_t>(messages::ParameterType::kNewGroupRequest);

                if (new_group_request_id.has_value()) {
                    if (auto callbacks = std::dynamic_pointer_cast<ServerCallbacks>(callbacks_)) {
                        callbacks->NewGroupRequested(sub_ctx_it->second.track_full_name, new_group_request_id.value())
                          .Resolve(
                            [request_id, group_id = *new_group_request_id, self = GetSharedPtr()](const auto& result) {
                                if (result) {
                                    return;
                                }

                                const auto& [code, reason] = result.error();
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
                                   forward,
                                   new_group_request_id.has_value());

                for (const auto& pub : pub_tracks_by_track_alias[sub_ctx_it->second.track_hash.track_fullname_hash]) {
                    if (not forward) {
                        pub.second->SetStatus(PublishTrackHandler::Status::kPaused);
                    } else {
                        pub.second->SetStatus(PublishTrackHandler::Status::kNewGroupRequested);
                    }
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
