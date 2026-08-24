// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/session.h"
#include "data_context.h"
#include "quicr/connection.h"
#include "quicr/handlers/joining_fetch_handler.h"
#include "quicr/handlers/subscribe_namespace_handler.h"
#include "quicr/log.h"
#include "quicr/messages/ctrl_message_types.h"
#include "quicr/messages/message.h"
#include "quicr/messages/messages.h"
#include "quicr/messages/parameters.h"
#include "quicr/session_callbacks.h"
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

        RequestErrorCode FromErrorCode(messages::ErrorCode error_code)
        {
            switch (error_code) {
                case messages::ErrorCode::kInternalError:
                    return RequestErrorCode::kInternalError;
                case messages::ErrorCode::kUnauthorized:
                    return RequestErrorCode::kUnauthorized;
                case messages::ErrorCode::kTimeout:
                    return RequestErrorCode::kTimeout;
                case messages::ErrorCode::kNotSupported:
                    return RequestErrorCode::kNotSupported;
                case messages::ErrorCode::kMalformedAuthToken:
                    return RequestErrorCode::kMalformedAuthToken;
                case messages::ErrorCode::kExpiredAuthToken:
                    return RequestErrorCode::kExpiredAuthToken;
                case messages::ErrorCode::kDoesNotExist:
                    return RequestErrorCode::kDoesNotExist;
                case messages::ErrorCode::kInvalidRange:
                    return RequestErrorCode::kInvalidRange;
                case messages::ErrorCode::kMalformedTrack:
                    return RequestErrorCode::kMalformedTrack;
                case messages::ErrorCode::kDuplicateSubscription:
                    return RequestErrorCode::kDuplicateSubscription;
                case messages::ErrorCode::kUninterested:
                    return RequestErrorCode::kUninterested;
                case messages::ErrorCode::kPrefixOverlap:
                    return RequestErrorCode::kPrefixOverlap;
                case messages::ErrorCode::kInvalidJoiningRequestId:
                    return RequestErrorCode::kInvalidJoiningRequestId;
            }
        }

        messages::ErrorCode ToErrorCode(RequestErrorCode error_code)
        {
            switch (error_code) {
                case RequestErrorCode::kInternalError:
                    return messages::ErrorCode::kInternalError;
                case RequestErrorCode::kUnauthorized:
                    return messages::ErrorCode::kUnauthorized;
                case RequestErrorCode::kTimeout:
                    return messages::ErrorCode::kTimeout;
                case RequestErrorCode::kNotSupported:
                    return messages::ErrorCode::kNotSupported;
                case RequestErrorCode::kMalformedAuthToken:
                    return messages::ErrorCode::kMalformedAuthToken;
                case RequestErrorCode::kExpiredAuthToken:
                    return messages::ErrorCode::kExpiredAuthToken;
                case RequestErrorCode::kDoesNotExist:
                    return messages::ErrorCode::kDoesNotExist;
                case RequestErrorCode::kInvalidRange:
                    return messages::ErrorCode::kInvalidRange;
                case RequestErrorCode::kMalformedTrack:
                    return messages::ErrorCode::kMalformedTrack;
                case RequestErrorCode::kDuplicateSubscription:
                    return messages::ErrorCode::kDuplicateSubscription;
                case RequestErrorCode::kUninterested:
                    return messages::ErrorCode::kUninterested;
                case RequestErrorCode::kPrefixOverlap:
                    return messages::ErrorCode::kPrefixOverlap;
                case RequestErrorCode::kInvalidJoiningRequestId:
                    return messages::ErrorCode::kInvalidJoiningRequestId;
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
        tx_ctrl_data_ctx_ = quic_transport_->CreateDataContext(current_connection_, true, 0, false);
        tx_ctrl_stream_id_ = quic_transport_->CreateStream(current_connection_, tx_ctrl_data_ctx_, 0);

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

    void Session::SendCtrlMsg(const std::shared_ptr<DataContext>& data_ctx,
                              std::shared_ptr<const std::vector<uint8_t>> data)
    {
        if (tx_ctrl_data_ctx_ == nullptr) {
            throw ProtocolViolationException("Control bidir data context not created");
        }

        auto result = quic_transport_->Enqueue(current_connection_,
                                               data_ctx,
                                               0 /* not use for bidir streams */,
                                               std::move(data),
                                               0,
                                               2000,
                                               0,
                                               { true, false, false, false });

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
        SendCtrlMsg(tx_ctrl_data_ctx_, ControlMessageType::kSetup, setup_options);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending Setup (error={})", e.what());
        throw e;
    }

    void Session::SendTrackStatusOk(const std::shared_ptr<DataContext>& data_ctx,
                                    const std::optional<messages::Location>& largest_object,
                                    const TrackExtensions& track_properties)
    {
        SendRequestOk(
          data_ctx, Parameters().AddOptional(ParameterType::kLargestObject, largest_object), track_properties);
    }

    void Session::SendSubscribeNamespaceOk(const std::shared_ptr<DataContext>& data_ctx)
    {
        SendRequestOk(data_ctx, {});
    }

    void Session::SendRequestUpdateOk(const std::shared_ptr<DataContext>& data_ctx,
                                      std::optional<std::uint64_t> expires,
                                      const std::optional<messages::Location>& largest_object)
    {
        SendRequestOk(data_ctx,
                      Parameters()
                        .AddOptional(ParameterType::kExpires, expires)
                        .AddOptional(ParameterType::kLargestObject, largest_object));
    }

    void Session::SendRequestOk(const std::shared_ptr<DataContext>& data_ctx,
                                const messages::Parameters& params,
                                const TrackExtensions& track_properties)
    try {
        QUICR_LOGGER_DEBUG(logger_,
                           "Sending REQUEST_OK to conn_id: {} request_id: {}",
                           current_connection_->GetID(),
                           request_id_by_data_ctx.at(data_ctx->GetID()));

        SendCtrlMsg(data_ctx, ControlMessageType::kRequestOk, params, track_properties);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending REQUEST_OK (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendRequestUpdate(const std::shared_ptr<DataContext>& data_ctx,
                                    [[maybe_unused]] quicr::TrackHash th,
                                    std::optional<std::uint64_t> end_group_id,
                                    std::uint8_t priority,
                                    bool forward)
    try {
        auto params = Parameters{}
                        .Add(ParameterType::kSubscriberPriority, priority)
                        .Add(ParameterType::kForward, forward)
                        .AddOptional(ParameterType::kNewGroupRequest, end_group_id);

        QUICR_LOGGER_DEBUG(logger_,
                           "Sending REQUEST_UPDATE to conn_id: {} request_id: {} track namespace hash: {} name "
                           "hash: {} forward: {} ngr: {}",
                           current_connection_->GetID(),
                           GetNextRequestID(),
                           th.track_namespace_hash,
                           th.track_name_hash,
                           forward,
                           end_group_id.has_value());

        SendCtrlMsg(data_ctx, ControlMessageType::kRequestUpdate, UintVar(GetNextRequestID()), params);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending REQUEST_UPDATE (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendRequestError(const std::shared_ptr<DataContext>& data_ctx,
                                   [[maybe_unused]] uint64_t request_id,
                                   ErrorCode error,
                                   std::chrono::milliseconds retry_interval,
                                   const std::string& reason)
    try {
        QUICR_LOGGER_DEBUG(logger_,
                           "Sending REQUEST_ERROR to conn_id: {} request_id: {} error code: {} reason: {}",
                           current_connection_->GetID(),
                           request_id,
                           static_cast<int>(error),
                           reason);

        SendCtrlMsg(
          data_ctx, ControlMessageType::kRequestError, error, UintVar(retry_interval.count()), AsOwnedBytes(reason));
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending REQUEST_ERROR (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendPublishNamespace(const std::shared_ptr<DataContext>& data_ctx,
                                       std::uint64_t request_id,
                                       const TrackNamespace& track_namespace)
    try {
        QUICR_LOGGER_DEBUG(logger_,
                           "Sending PublishNamespace to conn_id: {} request_id: {} namespace_hash: {}",
                           current_connection_->GetID(),
                           request_id,
                           TrackHash({ track_namespace, {} }).track_namespace_hash);

        SendCtrlMsg(
          data_ctx, ControlMessageType::kPublishNamespace, UintVar(request_id), track_namespace, Parameters{});
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending PublishNamespace (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendTrackStatus(std::uint64_t request_id, const FullTrackName& tfn)
    try {
        QUICR_LOGGER_DEBUG(
          logger_, "Sending TRACK_STATUS to conn_id: {} request_id: {}", current_connection_->GetID(), request_id);

        SendCtrlMsg(tx_ctrl_data_ctx_, ControlMessageType::kTrackStatus, UintVar(request_id), tfn.name_space, tfn.name);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending Trac (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendSubscribe(const std::shared_ptr<DataContext>& data_ctx,
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

        SendCtrlMsg(data_ctx, ControlMessageType::kSubscribe, UintVar(request_id), tfn.name_space, tfn.name, params);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending Subscribe (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendPublish(const std::shared_ptr<DataContext>& data_ctx,
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

        SendCtrlMsg(data_ctx,
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

    void Session::SendPublishOk(const std::shared_ptr<DataContext>& data_ctx, const PublishOkAttributes& attributes)
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

        SendRequestOk(data_ctx, params);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending Publish Ok (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendSubscribeOk(const std::shared_ptr<DataContext>& data_ctx,
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

        SendCtrlMsg(data_ctx, ControlMessageType::kSubscribeOk, UintVar(track_alias), params, extensions);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending SubscribeOk (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendPublishDone(const std::shared_ptr<DataContext>& data_ctx,
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
          data_ctx, ControlMessageType::kPublishDone, UintVar(request_id), status, UintVar(0), AsOwnedBytes(reason));
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending PUBLISH_DONE (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendSubscribeNamespace(const std::shared_ptr<DataContext>& data_ctx,
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

        SendCtrlMsg(data_ctx, type, UintVar(request_id), prefix, params);
    } catch (const std::exception& e) {
        QUICR_LOGGER_ERROR(logger_, "Caught exception sending subscribe namespace (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Session::SendUnsubscribeNamespace(const std::shared_ptr<DataContext>& data_ctx, const TrackNamespace& prefix)
    try {
        [[maybe_unused]] auto th = TrackHash({ prefix, {} });

        QUICR_LOGGER_DEBUG(logger_,
                           "Sending UNSUBSCRIBE_NAMESPACE to conn_id: {} prefix_hash: {}",
                           current_connection_->GetID(),
                           th.track_namespace_hash);

        SendCtrlMsg(data_ctx, ControlMessageType::kNamespaceDone, prefix);
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

        const auto data_ctx = quic_transport_->CreateDataContext(current_connection_, true, 0, true);
        handler->SetDataContext(data_ctx);
        handler->SetRequestStreamId(quic_transport_->CreateStream(current_connection_, data_ctx, 0));
        request_id_by_data_ctx[data_ctx->GetID()] = handler->GetRequestId().value();

        SendSubscribeNamespace(data_ctx, handler->GetRequestId().value(), prefix, handler->GetFilter(), message_type);
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

    void Session::SendFetch(const std::shared_ptr<DataContext>& data_ctx,
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

        SendCtrlMsg(data_ctx,
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

    void Session::SendJoiningFetch(const std::shared_ptr<DataContext>& data_ctx,
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

        SendCtrlMsg(data_ctx,
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

    void Session::SendFetchOk(const std::shared_ptr<DataContext>& data_ctx,
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

        SendCtrlMsg(data_ctx, ControlMessageType::kFetchOk, end_of_track, largest_location, params, extensions);
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
            if (req_it != recv_req_id.end() && req_it->second.data_ctx) {
                track_handler->SetDataContext(req_it->second.data_ctx);
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

            const auto data_ctx = quic_transport_->CreateDataContext(current_connection_, true, 0, true);
            track_handler->SetDataContext(data_ctx);
            track_handler->SetRequestStreamId(quic_transport_->CreateStream(current_connection_, data_ctx, 0));
            request_id_by_data_ctx[data_ctx->GetID()] = track_handler->GetRequestId().value();

            SendSubscribe(
              data_ctx, *track_handler->GetRequestId(), tfn, th, priority, group_order, filter, delivery_timeout);

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
                const auto data_ctx = quic_transport_->CreateDataContext(current_connection_, true, 0, true);
                joining_fetch_handler->SetDataContext(data_ctx);
                joining_fetch_handler->SetRequestStreamId(
                  quic_transport_->CreateStream(current_connection_, data_ctx, 0));
                request_id_by_data_ctx[data_ctx->GetID()] = fetch_rid;
                request_handlers[fetch_rid] = std::move(joining_fetch_handler);
                SendJoiningFetch(data_ctx,
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
        const auto data_ctx = track_handler->GetDataContext();
        if (data_ctx == nullptr) {
            QUICR_LOGGER_ERROR(
              logger_, "Subscribe track update missing data context conn_id: {}", current_connection_->GetID());
            return;
        }

        SendRequestUpdate(data_ctx, th, track_handler->pending_new_group_request_id_, priority, true);
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
                        if (const auto data_ctx = handler.GetDataContext();
                            data_ctx != nullptr && handler.GetRequestStreamId().has_value()) {
                            quic_transport_->CloseStream(
                              current_connection_, data_ctx, *handler.GetRequestStreamId(), true);
                        }
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
                    if (const auto data_ctx = handler.GetDataContext(); send_unsubscribe && data_ctx != nullptr) {
                        SendUnsubscribeNamespace(data_ctx, handler.GetPrefix());
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

        if (const auto data_ctx = handler.GetPublishDataContext()) {
            // TODO: is_reset should propagate down here?
            quic_transport_->DeleteDataContext(current_connection_, data_ctx);
            handler.ResetPublishDataContext();
        }
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

        if (const auto data_ctx = handler_it->second->GetDataContext()) {
            request_id_by_data_ctx.erase(data_ctx->GetID());
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
                                         code,
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
                                         code,
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
                const auto ctrl_data_ctx = pub_n_it->second->GetDataContext();

                // Send subscribe done if track has subscriber and is sending
                if (pub_n_it->second->GetStatus() == PublishTrackHandler::Status::kOk &&
                    pub_n_it->second->GetRequestId().has_value() && ctrl_data_ctx != nullptr) {
                    QUICR_LOGGER_INFO(logger_,
                                      "Unpublish track namespace hash: {} track_name_hash: {} track_alias: {}, sending "
                                      "publish_done",
                                      th.track_namespace_hash,
                                      th.track_name_hash,
                                      th.track_fullname_hash);
                    SendPublishDone(ctrl_data_ctx,
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

                pub_n_it->second->ResetPublishDataContext();

                lock.unlock();

                // We continue to use the kNotAnnounced state when removing. Might make sense to use kDestroyed instead
                pub_n_it->second->SetStatus(PublishTrackHandler::Status::kNotAnnounced);

                lock.lock();

                pub_ns_it->second.erase(pub_n_it);
            }

            quic_transport_->DeleteDataContext(current_connection_, track_handler->GetPublishDataContext());
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

        const auto ctrl_data_ctx = quic_transport_->CreateDataContext(current_connection_, true, 0, true);
        track_handler->SetDataContext(ctrl_data_ctx);
        quic_transport_->CreateStream(current_connection_, ctrl_data_ctx, 0);
        request_id_by_data_ctx[ctrl_data_ctx->GetID()] = track_handler->GetRequestId().value();

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

        SendPublish(ctrl_data_ctx, *track_handler->GetRequestId(), publish);

        track_handler->connection_id_ = current_connection_->GetID();
        QUICR_LOGGER_INFO(logger_,
                          "Publish track creating new data context connId {}, track namespace hash: {}, name hash: {}",
                          current_connection_->GetID(),
                          th.track_namespace_hash,
                          th.track_name_hash);
        const auto publish_data_ctx =
          quic_transport_->CreateDataContext(current_connection_,
                                             track_handler->default_track_mode_ == TrackMode::kDatagram ? false : true,
                                             track_handler->default_priority_,
                                             false);
        track_handler->SetPublishDataContext(publish_data_ctx);

        request_id_by_data_ctx[publish_data_ctx->GetID()] = track_handler->GetRequestId().value();

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

            const auto data_ctx = quic_transport_->CreateDataContext(current_connection_, true, 0, true);
            ns_handler->SetDataContext(data_ctx);
            ns_handler->SetRequestStreamId(quic_transport_->CreateStream(current_connection_, data_ctx, 0));

            lock.lock();

            request_id_by_data_ctx[data_ctx->GetID()] = ns_handler->GetRequestId().value();

            SendPublishNamespace(data_ctx, *ns_handler->GetRequestId(), ns_handler->GetPrefix());
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

        const auto data_ctx = track_handler->GetDataContext();
        const auto request_stream_id = track_handler->GetRequestStreamId();
        if (data_ctx == nullptr || !request_stream_id.has_value()) {
            QUICR_LOGGER_ERROR(logger_,
                               "PublishNamespaceDone missing request context conn_id: {} prefix_hash: {}",
                               current_connection_->GetID(),
                               prefix_hash);
            return;
        }

        quic_transport_->CloseStream(current_connection_, data_ctx, *request_stream_id, true);
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

        const auto data_ctx = quic_transport_->CreateDataContext(current_connection_, true, 0, true);
        track_handler->SetDataContext(data_ctx);
        track_handler->SetRequestStreamId(quic_transport_->CreateStream(current_connection_, data_ctx, 0));
        request_id_by_data_ctx[data_ctx->GetID()] = request_id;

        SendFetch(data_ctx, request_id, tfn, priority, group_order, start_location, end_location);
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

                    tx_ctrl_data_ctx_ = quic_transport_->CreateDataContext(current_connection_, true, 0, false);
                    tx_ctrl_stream_id_ = quic_transport_->CreateStream(current_connection_, tx_ctrl_data_ctx_, 0);

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

    void Session::OnRecvStream(uint64_t stream_id, const std::shared_ptr<DataContext>& data_ctx, const bool is_bidir)
    try {
        // TODO: This is circuitous, maybe we can inline this.
        auto rx_ctx = quic_transport_->GetStreamRxContext(current_connection_, stream_id);

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
                            if (data_ctx == nullptr) {
                                throw std::invalid_argument("Missing data context");
                            }
                            processed = ProcessRequestMessage(data_ctx, msg_type, payload);
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

            } else if (rx_ctx->caller_any.has_value()) {
                rx_ctx->data_queue.PopFront();

                // fast processing for existing stream using weak pointer to subscribe handler
                auto sub_handler_weak = std::any_cast<std::weak_ptr<SubscribeTrackHandler>>(rx_ctx->caller_any);
                if (auto sub_handler = sub_handler_weak.lock()) {
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

                    if (data_ctx != nullptr) {
                        quic_transport_->CloseStream(current_connection_, data_ctx, stream_id, true);
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
                                 const std::shared_ptr<DataContext>& data_ctx,
                                 StreamClosedFlag flag)
    {
        QUICR_LOGGER_DEBUG(logger_, "Stream {} closed", stream_id);

        if (auto callbacks = std::dynamic_pointer_cast<ServerCallbacks>(callbacks_)) {
            callbacks->OnStreamClosed(stream_id, flag);
        }

        {
            std::lock_guard lock(state_mutex_);
            stream_buffers.erase(stream_id);
        }

        if (data_ctx != nullptr) {
            try {
                std::unique_lock lock(state_mutex_);

                // This is a request stream.
                const auto req_it = request_id_by_data_ctx.find(data_ctx->GetID());
                if (req_it != request_id_by_data_ctx.end()) {
                    const auto request_id = req_it->second;
                    request_id_by_data_ctx.erase(req_it);

                    lock.unlock();
                    CloseRequestHandler(request_id, stream_id, flag);
                    return;
                }

            } catch (const std::exception& e) {
                QUICR_LOGGER_ERROR(logger_, "Caught exception on stream closed: {}", e.what());
            }
            return;
        }

        try {
            // TODO: Replace this check with control stream IDs check.
            if ((stream_id & 2) == 0) { // bidir
                switch (flag) {
                    case StreamClosedFlag::kFin:
                        if (tx_ctrl_stream_id_.has_value() && tx_ctrl_stream_id_ == stream_id) {
                            throw ProtocolViolationException("Primary control stream FIN");
                        }
                        break;
                    case StreamClosedFlag::kReset:
                        if (tx_ctrl_stream_id_.has_value() && tx_ctrl_stream_id_ == stream_id) {
                            throw ProtocolViolationException("Primary control stream RESET");
                        }
                        break;
                }

                return;
            }

            if (rx_ctx == nullptr) {
                return;
            }

            const auto handler_weak = std::any_cast<std::weak_ptr<SubscribeTrackHandler>>(rx_ctx->caller_any);
            if (const auto handler_ptr = handler_weak.lock()) {
                try {
                    if (auto handler = handler_ptr.get()) {
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
                        }
                    }
                } catch (const ProtocolViolationException& e) {
                    QUICR_LOGGER_ERROR(logger_, "Protocol violation on stream data recv: {}", e.reason);
                    throw ProtocolViolationException(e.reason);
                } catch (const std::exception& e) {
                    QUICR_LOGGER_ERROR(logger_, "Caught exception on stream data recv: {}", e.what());
                    throw std::runtime_error("Internal error");
                }
            }
        } catch (const std::bad_any_cast&) {
            QUICR_LOGGER_WARN(logger_, "Received stream closed for unknown handler");
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
        rx_ctx.caller_any = std::make_any<std::weak_ptr<SubscribeTrackHandler>>(sub_it->second);
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
            rx_ctx.caller_any = std::make_any<std::weak_ptr<SubscribeTrackHandler>>(h);
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

    std::uint64_t Session::CreateStream(const std::shared_ptr<DataContext>& data_ctx, uint8_t priority)
    {
        return quic_transport_->CreateStream(current_connection_, data_ctx, priority);
    }

    void Session::OnRecvDgram(const std::shared_ptr<DataContext>& data_ctx)
    {
        for (int i = 0; i < kReadLoopMaxPerStream; i++) {
            auto data = quic_transport_->Dequeue(current_connection_, data_ctx);
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
                                   "Received object datagram conn_id: {} data_ctx_id: {} "
                                   "track_alias: {} data size: {}",
                                   current_connection_->GetID(),
                                   (data_ctx ? data_ctx->GetID() : 0),
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
                                   "Failed to decode datagram conn_id: {} data_ctx_id: {} size: {}",
                                   current_connection_->GetID(),
                                   (data_ctx ? data_ctx->GetID() : 0),
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

        MetricsSampled(conn->metrics);
    }

    void Session::OnDataMetricsStampled(const MetricsTimeStamp sample_time,
                                        const std::shared_ptr<DataContext>& data_ctx,
                                        const QuicDataContextMetrics& quic_data_context_metrics)
    {
        const auto req_it = request_id_by_data_ctx.find(data_ctx->GetID());
        if (req_it != request_id_by_data_ctx.end()) {
            const auto req_handler_it = request_handlers.find(req_it->second);
            if (req_handler_it != request_handlers.end()) {
                if (auto h = req_handler_it->second->Get<SubscribeTrackHandler>();
                    h && h->GetDataContext() == data_ctx) {

                    h->subscribe_track_metrics_.last_sample_time =
                      sample_time.time_since_epoch() / std::chrono::microseconds(1);

                    h->subscribe_track_metrics_.bytes_received += quic_data_context_metrics.rx_stream_bytes;

                    h->MetricsSampled(h->subscribe_track_metrics_);

                } else if (auto h = req_handler_it->second->Get<PublishTrackHandler>();
                           h && h->GetPublishDataContext() == data_ctx) {

                    h->publish_track_metrics_.last_sample_time =
                      sample_time.time_since_epoch() / std::chrono::microseconds(1);

                    h->publish_track_metrics_.quic.tx_buffer_drops += quic_data_context_metrics.tx_buffer_drops;
                    h->publish_track_metrics_.quic.tx_callback_ms = quic_data_context_metrics.tx_callback_ms;
                    h->publish_track_metrics_.quic.tx_delayed_callback += quic_data_context_metrics.tx_delayed_callback;
                    h->publish_track_metrics_.quic.tx_object_duration_us =
                      quic_data_context_metrics.tx_object_duration_us;
                    h->publish_track_metrics_.quic.tx_queue_discards += quic_data_context_metrics.tx_queue_discards;
                    h->publish_track_metrics_.quic.tx_queue_expired += quic_data_context_metrics.tx_queue_expired;
                    h->publish_track_metrics_.quic.tx_queue_size = quic_data_context_metrics.tx_queue_size;
                    h->publish_track_metrics_.quic.tx_reset_wait += quic_data_context_metrics.tx_reset_wait;

                    h->MetricsSampled(h->publish_track_metrics_);
                }
            }

            for (const auto& [_, req] : request_handlers) {
                if (auto h = req->Get<SubscribeTrackHandler>(); h) {
                    h->MetricsSampled(h->subscribe_track_metrics_);
                }
            }
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

    TransportError Session::Enqueue(const std::shared_ptr<DataContext>& data_ctx,
                                    std::uint64_t stream_id,
                                    std::shared_ptr<const std::vector<uint8_t>> bytes,
                                    const uint8_t priority,
                                    const uint32_t ttl_ms,
                                    const uint32_t delay_ms,
                                    const Transport::EnqueueFlags flags)
    {
        return quic_transport_->Enqueue(
          current_connection_, data_ctx, stream_id, std::move(bytes), priority, ttl_ms, delay_ms, flags);
    }

    // -- Resolve Methods --

    void Session::ResolveFetch(uint64_t request_id,
                               std::optional<messages::GroupOrder> group_order,
                               const FetchResponse& response)
    {
        const auto request_it = recv_req_id.find(request_id);
        if (request_it == recv_req_id.end() || request_it->second.data_ctx == nullptr) {
            QUICR_LOGGER_ERROR(logger_,
                               "Cannot resolve FETCH without its request stream conn_id: {} request_id: {}",
                               current_connection_->GetID(),
                               request_id);
            return;
        }
        const auto data_ctx = request_it->second.data_ctx;

        SendFetchOk(data_ctx, response.publisher_default_group_order, false, response.largest_location.value());
    }

    void Session::ResolveRequestUpdate(std::uint64_t request_id, const RequestUpdateResponse& response)
    {
        auto track_it = request_handlers.find(request_id);
        if (track_it == request_handlers.end()) {
            QUICR_LOGGER_ERROR(logger_, "Resolve REQUEST_UPDATE for request {} had no handler", request_id);
            return;
        }

        QUICR_LOGGER_DEBUG(logger_, "Request Updated resolve req_id: {}", request_id);

        const auto data_ctx = track_it->second->GetDataContext();
        if (data_ctx == nullptr) {
            QUICR_LOGGER_WARN(logger_,
                              "ResolveRequestUpdate missing handler data context conn_id: {} request_id: {}",
                              current_connection_->GetID(),
                              request_id);
            return;
        }

        if (response.error.has_value()) {
            SendRequestError(
              data_ctx, request_id, response.error->error_code, response.error->retry_interval, response.error->reason);
        } else {
            // TODO: Type the params in resolve, fill in here.
            SendRequestUpdateOk(data_ctx, std::nullopt, std::nullopt);
        }
    }

    std::shared_ptr<DataContext> Session::FindSubscribeNamespaceDataContext(const TrackNamespace& track_namespace) const
    {
        for (const auto& [_, handler] : request_handlers) {
            if (auto h = handler->Get<SubscribeNamespaceHandler>()) {
                auto data_ctx = h->GetDataContext();
                if (data_ctx == nullptr) {
                    continue;
                }

                const auto match = h->GetPrefix().IsPrefixOf(track_namespace);
                if (match == std::partial_ordering::unordered || match == std::partial_ordering::less) {
                    continue;
                }

                return data_ctx;
            }
        }

        return nullptr;
    }

    const std::shared_ptr<DataContext>& Session::ResponseDataContext(const std::uint64_t request_id) const
    {
        const auto recv_it = recv_req_id.find(request_id);
        if (recv_it != recv_req_id.end() && recv_it->second.data_ctx != nullptr) {
            return recv_it->second.data_ctx;
        }

        return tx_ctrl_data_ctx_;
    }

    // -- Client Callbacks --

    void Session::MetricsSampled(const ConnectionMetrics&) {}

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
        if (req_it != recv_req_id.end() && req_it->second.data_ctx) {
            track_handler->SetDataContext(req_it->second.data_ctx);
        }

        const auto publish_data_ctx =
          quic_transport_->CreateDataContext(current_connection_,
                                             track_handler->default_track_mode_ == TrackMode::kDatagram ? false : true,
                                             track_handler->default_priority_,
                                             false);
        track_handler->SetPublishDataContext(publish_data_ctx);

        request_id_by_data_ctx[publish_data_ctx->GetID()] = request_id;

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
        std::lock_guard lock(state_mutex_);

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

        if (const auto publish_data_ctx = track_handler->GetPublishDataContext()) {
            request_id_by_data_ctx.erase(publish_data_ctx->GetID());

            quic_transport_->DeleteDataContext(current_connection_, publish_data_ctx);

            // Stop observing the context now that it is scheduled for deletion.
            track_handler->ResetPublishDataContext();
        }

        const auto data_ctx = track_handler->GetDataContext();
        if (send_publish_done && data_ctx != nullptr) {
            SendPublishDone(data_ctx,
                            track_handler->GetRequestId().value(),
                            messages::PublishDoneStatusCode::kSubscribtionEnded,
                            "No publishers");
        }
    }

    void Session::BindFetchTrack(std::shared_ptr<PublishFetchHandler> track_handler)
    {
        const std::uint64_t request_id = *track_handler->GetRequestId();
        QUICR_LOGGER_INFO(
          logger_, "Publish fetch track conn_id: {} subscribe: {}", current_connection_->GetID(), request_id);

        std::lock_guard lock(state_mutex_);

        track_handler->SetStatus(PublishFetchHandler::Status::kOk);
        track_handler->connection_id_ = current_connection_->GetID();
        track_handler->SetPublishDataContext(
          quic_transport_->CreateDataContext(current_connection_, true, track_handler->GetDefaultPriority(), false));

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
        quic_transport_->DeleteDataContext(current_connection_, track_handler->GetPublishDataContext(), true);
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
                                                 code,
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
                                                     code,
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

    bool Session::ProcessRequestMessage(const std::shared_ptr<DataContext>& data_ctx,
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
                recv_req_id[request_id] = { .track_full_name = tfn, .track_hash = th, .data_ctx = data_ctx };
                request_id_by_data_ctx[data_ctx->GetID()] = request_id;

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
                          data_ctx, request_id, messages::ErrorCode::kDoesNotExist, 0ms, "Published track not found");
                        return true;
                    }

                    ptd->SetDataContext(data_ctx);

                    SendSubscribeOk(ResponseDataContext(request_id),
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
                              self->SendRequestError(self->ResponseDataContext(request_id),
                                                     request_id,
                                                     messages::ErrorCode::kInternalError,
                                                     0ms,
                                                     reason.value_or("Internal error"));

                              return;
                          }

                          if (self->client_mode_) {
                              self->SendSubscribeOk(self->ResponseDataContext(request_id),
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
                                  self->SendSubscribeOk(self->ResponseDataContext(request_id),
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
                                                   code,
                                                   reason.value_or("unknown"));
                            });
                      });
                }

                return true;
            }
            case messages::ControlMessageType::kSubscribeOk: {
                const auto request_it = request_id_by_data_ctx.find(data_ctx->GetID());
                if (request_it == request_id_by_data_ctx.end()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received SUBSCRIBE_OK for unknown request conn_id: {} data_ctx_id: {}, ignored",
                                      current_connection_->GetID(),
                                      data_ctx->GetID());
                    return true;
                }
                const auto request_id = request_it->second;

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
                const auto req_it = request_id_by_data_ctx.find(data_ctx->GetID());
                if (req_it == request_id_by_data_ctx.end()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received REQUEST_OK for unknown request conn_id: {} data_ctx_id: {}, ignored",
                                      current_connection_->GetID(),
                                      data_ctx->GetID());
                    return true;
                }
                const auto request_id = req_it->second;

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
                const auto request_it = request_id_by_data_ctx.find(data_ctx->GetID());
                if (request_it == request_id_by_data_ctx.end()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received REQUEST_ERROR for unknown request conn_id: {} data_ctx_id: {}, ignored",
                                      current_connection_->GetID(),
                                      data_ctx->GetID());
                    return true;
                }
                const auto request_id = request_it->second;
                const auto error_code = messages::Message::ParseField<messages::ErrorCode>(msg_bytes);
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

                request_id_by_data_ctx[data_ctx->GetID()] = request_id;

                if (callbacks_) {
                    callbacks_->TrackStatusReceived(GetSharedPtr(), request_id, tfn)
                      .Resolve([=, self = GetSharedPtr()](const auto& result) {
                          if (!result) {
                              const auto& [code, reason] = result.error();

                              switch (code) {
                                  case RequestErrorCode::kDoesNotExist:
                                      self->SendRequestError(self->ResponseDataContext(request_id),
                                                             request_id,
                                                             ErrorCode::kDoesNotExist,
                                                             0ms, // TODO: Figure out retry interval
                                                             reason.value_or("Track does not exist"));
                                      break;
                                  case RequestErrorCode::kUnauthorized:
                                      self->SendRequestError(self->ResponseDataContext(request_id),
                                                             request_id,
                                                             ErrorCode::kUnauthorized,
                                                             0ms, // TODO: Figure out retry interval
                                                             reason.value_or("Unauthorized"));
                                      break;
                                  default:
                                      self->SendRequestError(self->ResponseDataContext(request_id),
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
                            self->ResponseDataContext(request_id), result.value().largest_location, TrackExtensions());
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
                                            .data_ctx = data_ctx };
                recv_publish_namespaces.push_back(request_id);
                request_id_by_data_ctx[data_ctx->GetID()] = request_id;

                if (callbacks_) {
                    callbacks_->PublishNamespaceReceived(GetSharedPtr(), track_namespace, { .request_id = request_id })
                      .Resolve([=, self = GetSharedPtr()](const auto& result) {
                          if (!result) {
                              // TODO: Send announce error.

                              return;
                          }

                          auto response_data_ctx = self->ResponseDataContext(request_id);
                          if (const auto pub_ns_it = self->request_handlers.find(request_id);
                              pub_ns_it != self->request_handlers.end()) {
                              if (auto handler_data_ctx = pub_ns_it->second->GetDataContext()) {
                                  response_data_ctx = std::move(handler_data_ctx);
                              }
                          }

                          self->SendPublishNamespaceOk(response_data_ctx);

                          const auto sub_data_ctx = self->FindSubscribeNamespaceDataContext(track_namespace);
                          if (sub_data_ctx == nullptr) {
                              QUICR_LOGGER_WARN(self->logger_,
                                                "No subscribe namespace data context for publish namespace conn_id: {}",
                                                self->current_connection_->GetID());
                              return;
                          }

                          self->SendPublishNamespace(sub_data_ctx, self->GetNextRequestID(), track_namespace);
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

                request_id_by_data_ctx[data_ctx->GetID()] = request_id;

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
                              data_ctx, request_id, ToErrorCode(code), 0ms, reason.value_or("Internal error"));

                            return;
                        }

                        self->SendSubscribeNamespaceOk(data_ctx);

                        // Fan out PUBLISH_NAMESPACE for matching namespaces.
                        for (const auto& name_space : result.value()) {
                            const auto match = track_namespace_prefix.IsPrefixOf(name_space);
                            if (match == std::partial_ordering::unordered || match == std::partial_ordering::less) {
                                QUICR_LOGGER_WARN(self->logger_, "Dropping non prefix match");
                                continue;
                            }

                            auto pub_ns_request_id = self->GetNextRequestID();
                            self->SendPublishNamespace(data_ctx, pub_ns_request_id, name_space);
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
                                             code,
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
                                                 code,
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

                const auto request_it = request_id_by_data_ctx.find(data_ctx->GetID());
                if (request_it == request_id_by_data_ctx.end()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received FETCH_OK for unknown request conn_id: {} data_ctx_id: {}, ignored",
                                      current_connection_->GetID(),
                                      data_ctx->GetID());
                    return true;
                }
                const auto request_id = request_it->second;

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
                            .data_ctx = data_ctx,
                        };
                        request_id_by_data_ctx[data_ctx->GetID()] = request_id;

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

                                      messages::ErrorCode error_code = messages::ErrorCode::kInternalError;
                                      switch (code) {
                                          case FetchErrorCode::kInvalidRange:
                                              error_code = messages::ErrorCode::kInvalidRange;
                                              break;

                                          default:
                                              break;
                                      }

                                      self->SendRequestError(
                                        data_ctx, request_id, error_code, 0ms, reason.value_or("Internal error"));

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
                            SendRequestError(data_ctx,
                                             request_id,
                                             messages::ErrorCode::kDoesNotExist,
                                             0ms,
                                             "Corresponding subscribe does not exist");
                            return true;
                        }

                        FullTrackName tfn = subscribe_state->second.track_full_name;
                        const auto th = TrackHash(tfn);

                        recv_req_id[request_id] = {
                            .track_full_name = tfn,
                            .track_hash = th,
                            .data_ctx = data_ctx,
                        };
                        request_id_by_data_ctx[data_ctx->GetID()] = request_id;

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

                                      messages::ErrorCode error_code = messages::ErrorCode::kInternalError;
                                      switch (code) {
                                          case FetchErrorCode::kInvalidRange:
                                              error_code = messages::ErrorCode::kInvalidRange;
                                              break;

                                          default:
                                              break;
                                      }

                                      self->SendRequestError(
                                        data_ctx, request_id, error_code, 0ms, reason.value_or("Internal error"));

                                      return;
                                  }

                                  self->ResolveFetch(request_id, group_order, result.value());
                              });
                        }
                        return true;
                    }
                    default: {
                        SendRequestError(
                          data_ctx, request_id, messages::ErrorCode::kNotSupported, 0ms, "Unknown fetch type");
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
                                            .data_ctx = data_ctx };
                request_id_by_data_ctx[data_ctx->GetID()] = request_id;

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

                              messages::ErrorCode error_code;
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

                              self->SendRequestError(self->ResponseDataContext(request_id),
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

                          self->SendPublishOk(self->ResponseDataContext(request_id), result->attributes);
                      });
                }

                return true;
            }
            case messages::ControlMessageType::kRequestUpdate: {
                const auto update_request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto request_it = request_id_by_data_ctx.find(data_ctx->GetID());
                if (request_it == request_id_by_data_ctx.end()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received REQUEST_UPDATE on unknown request stream conn_id: {} data_ctx_id: {} "
                                      "update_request_id: {}, ignored",
                                      current_connection_->GetID(),
                                      data_ctx->GetID(),
                                      update_request_id);
                    return true;
                }
                const auto request_id = request_it->second;
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

                    track_it->second->RequestUpdateReceived(parameters);
                    return true;
                }

                auto sub_ctx_it = recv_req_id.find(request_id);
                if (sub_ctx_it == recv_req_id.end()) {
                    QUICR_LOGGER_WARN(logger_,
                                      "Received subscribe_update for unknown subscription conn_id: {} request_id: {}",
                                      current_connection_->GetID(),
                                      request_id);

                    SendRequestError(
                      data_ctx, request_id, messages::ErrorCode::kDoesNotExist, 0ms, "Subscription not found");
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
                                                   code,
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

                SendRequestUpdateOk(data_ctx, std::nullopt, std::nullopt);
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
