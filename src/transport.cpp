// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/detail/transport.h"
#include "quicr/detail/ctrl_message_types.h"
#include "quicr/detail/message.h"
#include "quicr/detail/messages.h"
#include "quicr/subscribe_namespace_handler.h"

#include <iomanip>
#include <quicr/detail/joining_fetch_handler.h>
#include <sstream>

namespace quicr {
    using namespace quicr::messages;
    using namespace std::chrono_literals;

    namespace {
        std::shared_ptr<spdlog::logger> SafeLoggerGet(const std::string& name)
        {
            if (auto logger = spdlog::get(name)) {
                return logger;
            }

            return spdlog::stderr_color_mt(name);
        }
    }

    TransportException::TransportException(TransportError error, std::source_location location)
      : std::runtime_error("Error in transport (error=" + std::to_string(static_cast<int>(error)) + ", " +
                           std::to_string(location.line()) + ", " + location.file_name() + ")")
      , Error(error)
    {
    }

    [[maybe_unused]]
    static std::string ToHex(std::vector<uint8_t> data)
    {
        std::stringstream hex(std::ios_base::out);
        hex.flags(std::ios::hex);
        for (const auto& byte : data) {
            hex << std::setw(2) << std::setfill('0') << int(byte);
        }
        return hex.str();
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

    Transport::Transport(const ClientConfig& cfg, std::shared_ptr<timeq::tick_service> tick_service)
      : std::enable_shared_from_this<Transport>()
      , client_mode_(true)
      , logger_(SafeLoggerGet("MTC"))
      , server_config_({})
      , client_config_(cfg)
      , tick_service_(std::move(tick_service))
      , quic_transport_({})
    {
        SPDLOG_LOGGER_TRACE(logger_, "Created Moq instance in client mode connecting to {}", cfg.connect_uri);
        Init();
    }

    Transport::Transport(const ServerConfig& cfg, std::shared_ptr<timeq::tick_service> tick_service)
      : std::enable_shared_from_this<Transport>()
      , client_mode_(false)
      , logger_(SafeLoggerGet("MTS"))
      , server_config_(cfg)
      , client_config_({})
      , tick_service_(std::move(tick_service))
      , quic_transport_({})
    {
        SPDLOG_LOGGER_INFO(
          logger_, "Created Moq instance in server mode listening on {}:{}", cfg.server_bind_ip, cfg.server_port);
        Init();
    }

    Transport::~Transport()
    {
        spdlog::drop(logger_->name());
    }

    void Transport::Init()
    {
        if (client_mode_) {
            // client init items

            if (client_config_.transport_config.debug) {
                logger_->set_level(spdlog::level::debug);
            }
        } else {
            // Server init items

            if (server_config_.transport_config.debug) {
                logger_->set_level(spdlog::level::debug);
            }
        }
    }

    Transport::StartTransportResult Transport::StartTransport()
    {
        if (!weak_from_this().lock()) {
            throw std::runtime_error("Transport is not shared_ptr");
        }

        if (client_mode_) {
            TransportRemote relay;
            auto parse_result = ParseConnectUri(client_config_.connect_uri);
            if (!parse_result) {
                return { Status::kInvalidParams, std::nullopt };
            }
            auto [address, port, protocol, path] = parse_result.value();
            relay.host_or_ip = address;
            relay.port = port;
            relay.proto = protocol;
            relay.path = path;

            quic_transport_ =
              ITransport::MakeClientTransport(relay, client_config_.transport_config, *this, tick_service_, logger_);

            auto conn_id = quic_transport_->Start();

            if (!conn_id) { // Error, connection ID should always be greater than one
                SPDLOG_LOGGER_ERROR(logger_, "Client connection failed");
                status_ = Status::kFailedToConnect;
                return { status_, std::nullopt };
            }

            status_ = Status::kConnecting;
            StatusChanged(status_);

            SPDLOG_LOGGER_INFO(logger_, "Connecting session conn_id: {}...", conn_id);
            auto [conn_ctx, _] = connections_.try_emplace(conn_id, ConnectionContext{});
            conn_ctx->second.connection_handle = conn_id;

            return { status_, conn_id };
        }

        TransportRemote server;
        server.host_or_ip = server_config_.server_bind_ip;
        server.port = server_config_.server_port;
        // Note: server.proto is ignored in server mode - the server automatically
        // supports both raw QUIC (moq-00) and WebTransport (h3) simultaneously.
        // The transport mode is determined per-connection based on ALPN negotiation.
        // Any value can be set here; it won't affect server behavior.
        server.proto = TransportProtocol::kQuic; // Ignored by server
        server.path = "/relay";

        quic_transport_ =
          ITransport::MakeServerTransport(server, server_config_.transport_config, *this, tick_service_, logger_);
        quic_transport_->Start();

        if (quic_transport_->Status() == TransportStatus::kShutdown) {
            status_ = Status::kInternalError;
        } else {
            status_ = Status::kReady;
        }

        return { status_, std::nullopt };
    }

    Transport::Status Transport::StopTransport()
    {
        return Status();
    }

    uint64_t Transport::RequestTrackStatus(ConnectionHandle connection_handle,
                                           const FullTrackName& track_full_name,
                                           const messages::SubscribeAttributes&)
    {
        std::lock_guard<std::mutex> _(state_mutex_);
        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "RequestTrackStatus conn_id: {} does not exist.", connection_handle);
            return 0;
        }

        auto request_id = conn_it->second.GetNextRequestId();

        SendTrackStatus(conn_it->second, request_id, track_full_name);

        return request_id;
    }

    void Transport::RequestOkReceived(ConnectionHandle, uint64_t, std::optional<messages::Location>) {}

    void Transport::RequestErrorReceived(ConnectionHandle, uint64_t, const RequestResponse&) {}

    void Transport::TrackStatusReceived(ConnectionHandle, uint64_t, const FullTrackName&) {}

    void Transport::ResolveTrackStatus(ConnectionHandle connection_handle,
                                       uint64_t request_id,
                                       const RequestResponse& subscribe_response)
    {
        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_DEBUG(logger_, "ResolveTrackStatus conn_id: {} not found, ignoring", connection_handle);
            return;
        }

        switch (subscribe_response.reason_code) {
            case RequestResponse::ReasonCode::kOk: {
                SendRequestOk(conn_it->second, request_id, subscribe_response.largest_location);
                break;
            }
            case RequestResponse::ReasonCode::kDoesNotExist:
                SendRequestError(conn_it->second,
                                 request_id,
                                 ErrorCode::kDoesNotExist,
                                 0ms, // TODO: Figure out retry interval
                                 subscribe_response.error_reason.has_value() ? *subscribe_response.error_reason
                                                                             : "Track does not exist");
                break;
            case RequestResponse::ReasonCode::kUnauthorized:
                SendRequestError(conn_it->second,
                                 request_id,
                                 ErrorCode::kUnauthorized,
                                 0ms, // TODO: Figure out retry interval
                                 subscribe_response.error_reason.has_value() ? *subscribe_response.error_reason
                                                                             : "Unauthorized");
                break;
            default:
                SendRequestError(conn_it->second, request_id, ErrorCode::kInternalError, 0ms, "Internal error");
                break;
        }
    }

    void Transport::SendCtrlMsg(const ConnectionContext& conn_ctx,
                                DataContextId data_ctx_id,
                                std::shared_ptr<const std::vector<uint8_t>> data)
    {
        if (!conn_ctx.ctrl_data_ctx_id.has_value()) {
            CloseConnection(conn_ctx.connection_handle,
                            messages::TerminationReason::kProtocolViolation,
                            "Control bidir data context not created");
            return;
        }

        auto result = quic_transport_->Enqueue(conn_ctx.connection_handle,
                                               data_ctx_id,
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

    void Transport::SendClientSetup()
    try {
        /* Available parameters:
         * - PATH (0x01): Specifies the path and query portion of the MoQ URI. Used only in native QUIC; prohibited in
         *   WebTransport.
         * - AUTHORITY (0x05): Specifies the authority (host) component of the MoQ URI. Used only in native QUIC;
         *   prohibited in WebTransport.
         * - MAX_REQUEST_ID (0x02): Communicates the initial number of requests the server is allowed to send to the
         *   client.
         * - MAX_AUTH_TOKEN_CACHE_SIZE (0x04): The maximum size in bytes of registered Authorization tokens the client
         *   is willing to store.
         * - AUTHORIZATION TOKEN (0x03): Provides one or more tokens to authorize the establishment of the session.
         * - MOQT IMPLEMENTATION (0x07): A free-form string identifying the name and version of the client's
         *   implementation.
         */
        auto setup_parameters = SetupParameters{}
                                  .Add(SetupParameterType::kEndpointId, client_config_.endpoint_id)
                                  .Add(SetupParameterType::kMaxRequestId, 0x1000);

        // Set PATH and AUTHORITY for native QUIC connections.
        // Start() has already validated parse of connect URI succeeds.
        const auto [host, port, protocol, path] = *ParseConnectUri(client_config_.connect_uri);
        if (protocol != TransportProtocol::kWebTransport) {
            const auto authority = port > 0 ? host + ":" + std::to_string(port) : host;
            setup_parameters.Add(SetupParameterType::kAuthority, authority);
            if (!path.empty()) {
                setup_parameters.Add(SetupParameterType::kPath, path);
            }
        }

        auto& conn_ctx = connections_.begin()->second;

        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), ControlMessageType::kClientSetup, setup_parameters);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending ClientSetup (error={})", e.what());
        throw e;
    }

    void Transport::SendServerSetup(ConnectionContext& conn_ctx)
    try {
        auto setup_parameters = SetupParameters{}.Add(SetupParameterType::kEndpointId, server_config_.endpoint_id);

        SPDLOG_LOGGER_DEBUG(logger_, "Sending SERVER_SETUP to conn_id: {}", conn_ctx.connection_handle);

        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), ControlMessageType::kServerSetup, setup_parameters);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending ServerSetup (error={})", e.what());
        throw e;
    }

    void Transport::SendRequestOk(ConnectionContext& conn_ctx,
                                  messages::RequestID request_id,
                                  std::optional<Location> largest_location)
    try {
        auto params = Parameters{}.AddOptional(ParameterType::kLargestObject, largest_location);

        SPDLOG_LOGGER_DEBUG(
          logger_, "Sending REQUEST_OK to conn_id: {} request_id: {}", conn_ctx.connection_handle, request_id);

        SendCtrlMsg(
          conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), ControlMessageType::kRequestOk, UintVar(request_id), params);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending REQUEST_OK (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendRequestUpdate(const ConnectionContext& conn_ctx,
                                      messages::RequestID request_id,
                                      messages::RequestID existing_request_id,
                                      quicr::TrackHash th,
                                      std::optional<messages::GroupId> end_group_id,
                                      std::uint8_t priority,
                                      bool forward)
    try {
        auto params = Parameters{}
                        .Add(ParameterType::kSubscriberPriority, priority)
                        .Add(ParameterType::kForward, forward)
                        .AddOptional(ParameterType::kNewGroupRequest, end_group_id);

        SPDLOG_LOGGER_DEBUG(
          logger_,
          "Sending REQUEST_UPDATE to conn_id: {} request_id: {} existing_id: {} track namespace hash: {} name "
          "hash: {} forward: {} ngr: {}",
          conn_ctx.connection_handle,
          request_id,
          existing_request_id,
          th.track_namespace_hash,
          th.track_name_hash,
          forward,
          end_group_id.has_value());

        SendCtrlMsg(conn_ctx,
                    conn_ctx.ctrl_data_ctx_id.value(),
                    ControlMessageType::kRequestUpdate,
                    UintVar(request_id),
                    UintVar(existing_request_id),
                    params);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending REQUEST_UPDATE (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendRequestError(ConnectionContext& conn_ctx,
                                     uint64_t request_id,
                                     ErrorCode error,
                                     std::chrono::milliseconds retry_interval,
                                     const std::string& reason)
    try {
        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending REQUEST_ERROR to conn_id: {} request_id: {} error code: {} reason: {}",
                            conn_ctx.connection_handle,
                            request_id,
                            static_cast<int>(error),
                            reason);

        SendCtrlMsg(conn_ctx,
                    conn_ctx.ctrl_data_ctx_id.value(),
                    ControlMessageType::kRequestError,
                    UintVar(request_id),
                    error,
                    UintVar(retry_interval.count()),
                    AsOwnedBytes(reason));
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending REQUEST_ERROR (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendPublishNamespace(ConnectionContext& conn_ctx,
                                         RequestID request_id,
                                         const TrackNamespace& track_namespace)
    try {
        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending PublishNamespace to conn_id: {} request_id: {} namespace_hash: {}",
                            conn_ctx.connection_handle,
                            request_id,
                            TrackHash({ track_namespace, {} }).track_namespace_hash);

        SendCtrlMsg(conn_ctx,
                    conn_ctx.ctrl_data_ctx_id.value(),
                    ControlMessageType::kPublishNamespace,
                    UintVar(request_id),
                    track_namespace,
                    Parameters{});
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending PublishNamespace (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendPublishNamespaceDone(ConnectionContext& conn_ctx, messages::RequestID request_id)
    try {
        SPDLOG_LOGGER_DEBUG(logger_, "Sending PUBLISH_NAMESPACE_DONE to conn_id: {}", conn_ctx.connection_handle);

        SendCtrlMsg(
          conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), ControlMessageType::kPublishNamespaceDone, UintVar(request_id));
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending PUBLISH_NAMESPACE_DONE (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendNamespace(ConnectionContext& conn_ctx,
                                  DataContextId data_ctx_id,
                                  const TrackNamespace& track_namespace_suffix)
    try {
        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending NAMESPACE to conn_id: {} suffix_hash: {}",
                            conn_ctx.connection_handle,
                            TrackHash({ track_namespace_suffix, {} }).track_namespace_hash);

        SendCtrlMsg(conn_ctx, data_ctx_id, ControlMessageType::kNamespace, track_namespace_suffix);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending NAMESPACE (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendTrackStatus(ConnectionContext& conn_ctx,
                                    messages::RequestID request_id,
                                    const FullTrackName& tfn)
    try {
        SPDLOG_LOGGER_DEBUG(
          logger_, "Sending TRACK_STATUS to conn_id: {} request_id: {}", conn_ctx.connection_handle, request_id);

        SendCtrlMsg(conn_ctx,
                    conn_ctx.ctrl_data_ctx_id.value(),
                    ControlMessageType::kTrackStatus,
                    UintVar(request_id),
                    tfn.name_space,
                    tfn.name);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending Trac (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendSubscribe(ConnectionContext& conn_ctx,
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
        auto params = Parameters{}
                        .Add(ParameterType::kSubscriberPriority, priority)
                        .AddOptional(ParameterType::kGroupOrder, group_order)
                        .Add(ParameterType::kForward, 1)
                        .AddOptional(ParameterType::kDeliveryTimeout, delivery_timeout);

        if (auto filter_type = GetFilterParameterType(filter); filter_type != ParameterType::kInvalid) {
            params.Add(filter_type, filter);
        }

        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending SUBSCRIBE to conn_id: {} request_id: {} track namespace hash: {} name hash: {}",
                            conn_ctx.connection_handle,
                            request_id,
                            th.track_namespace_hash,
                            th.track_name_hash);

        SendCtrlMsg(conn_ctx,
                    conn_ctx.ctrl_data_ctx_id.value(),
                    ControlMessageType::kSubscribe,
                    UintVar(request_id),
                    tfn.name_space,
                    tfn.name,
                    params);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending Subscribe (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendPublish(ConnectionContext& conn_ctx,
                                messages::RequestID request_id,
                                const FullTrackName& tfn,
                                uint64_t track_alias,
                                messages::GroupOrder group_order,
                                std::optional<Location> largest_location,
                                bool forward,
                                bool support_new_group)
    try {
        /* Available parameters:
         * - AUTHORIZATION TOKEN (0x03): Conveys authorization for the publisher to initiate the track.
         * - EXPIRES (0x08): Time in milliseconds after which the publisher will terminate the subscription.
         * - LARGEST OBJECT (0x09): The largest Location in the Track observed by the sender.
         * - FORWARD (0x10): Specifies the initial Forwarding State.
         */
        auto params = Parameters{}
                        .Add(ParameterType::kForward, forward)
                        .Add(ParameterType::kExpires, 0)
                        .AddOptional(ParameterType::kLargestObject, largest_location);

        auto extensions = TrackExtensions{}
                            .Add(ExtensionType::kDeliveryTimeout, 0)
                            .Add(ExtensionType::kMaxCacheDuration, 0)
                            .Add(ExtensionType::kDefaultPublisherGroupOrder, group_order)
                            .Add(ExtensionType::kDefaultPublisherPriority, 1)
                            .Add(ExtensionType::kDynamicGroups, support_new_group);

        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending PUBLISH to conn_id: {} request_id: {} track alias: {}",
                            conn_ctx.connection_handle,
                            request_id,
                            track_alias);

        SendCtrlMsg(conn_ctx,
                    conn_ctx.ctrl_data_ctx_id.value(),
                    ControlMessageType::kPublish,
                    UintVar(request_id),
                    tfn.name_space,
                    tfn.name,
                    UintVar(track_alias),
                    params,
                    extensions);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending Publish (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendPublishOk(ConnectionContext& conn_ctx,
                                  messages::RequestID request_id,
                                  bool forward,
                                  std::uint8_t priority,
                                  std::optional<messages::GroupOrder> group_order,
                                  const messages::Filter& filter)
    try {
        /* Available parameters:
         * - DELIVERY TIMEOUT (0x02): Duration the relay should attempt forwarding objects.
         * - SUBSCRIBER PRIORITY (0x20): Priority of the subscription relative to others.
         * - GROUP ORDER (0x22): Preference for group delivery order.
         * - SUBSCRIPTION FILTER (0x21): Specifies which objects the publisher should send.
         * - EXPIRES (0x08): Time in milliseconds after which the subscription will be terminated.
         * - FORWARD (0x10): Specifies the Forwarding State.
         * - NEW GROUP REQUEST (0x32): Requests the publisher to start a new group.
         */
        auto params = Parameters{}
                        .Add(ParameterType::kSubscriberPriority, priority)
                        .AddOptional(ParameterType::kGroupOrder, group_order)
                        .Add(ParameterType::kForward, forward);

        if (const auto filter_type = GetFilterParameterType(filter); filter_type != ParameterType::kInvalid) {
            params.Add(filter_type, filter);
        }

        SPDLOG_LOGGER_DEBUG(
          logger_, "Sending PUBLISH_OK to conn_id: {} request_id: {} ", conn_ctx.connection_handle, request_id);

        SendCtrlMsg(
          conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), ControlMessageType::kPublishOk, UintVar(request_id), params);

    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending Publish Ok (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendSubscribeOk(ConnectionContext& conn_ctx,
                                    uint64_t request_id,
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

        SPDLOG_LOGGER_DEBUG(
          logger_, "Sending SUBSCRIBE OK to conn_id: {} request_id: {}", conn_ctx.connection_handle, request_id);

        SendCtrlMsg(conn_ctx,
                    conn_ctx.ctrl_data_ctx_id.value(),
                    ControlMessageType::kSubscribeOk,
                    UintVar(request_id),
                    UintVar(track_alias),
                    params,
                    extensions);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending SubscribeOk (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendPublishDone(ConnectionContext& conn_ctx,
                                    uint64_t request_id,
                                    messages::PublishDoneStatusCode status,
                                    const std::string& reason)
    try {
        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending PUBLISH_DONE to conn_id: {} request_id: {} status: {}",
                            conn_ctx.connection_handle,
                            request_id,
                            static_cast<uint64_t>(status));

        SendCtrlMsg(conn_ctx,
                    conn_ctx.ctrl_data_ctx_id.value(),
                    ControlMessageType::kPublishDone,
                    UintVar(request_id),
                    status,
                    UintVar(0),
                    AsOwnedBytes(reason));
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending PUBLISH_DONE (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendUnsubscribe(ConnectionContext& conn_ctx, uint64_t request_id)
    try {
        SPDLOG_LOGGER_DEBUG(
          logger_, "Sending UNSUBSCRIBE to conn_id: {} request_id: {}", conn_ctx.connection_handle, request_id);

        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), ControlMessageType::kUnsubscribe, UintVar(request_id));
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending Unsubscribe (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendSubscribeNamespace(ConnectionHandle conn_handle,
                                           std::shared_ptr<SubscribeNamespaceHandler> handler)
    {
        // Namespace or tracks?
        std::string log_name;
        ControlMessageType type;
        switch (handler->GetMode()) {
            case SubscribeNamespaceHandler::Mode::kNamespaces:
                log_name = "SUBSCRIBE_NAMESPACE";
                type = ControlMessageType::kSubscribeNamespace;
                break;
            case SubscribeNamespaceHandler::Mode::kTracks:
                log_name = "SUBSCRIBE_TRACKS";
                type = ControlMessageType::kSubscribeTracks;
                break;
        }

        try {
            std::lock_guard<std::mutex> _(state_mutex_);
            auto conn_it = connections_.find(conn_handle);
            if (conn_it == connections_.end()) {
                SPDLOG_LOGGER_ERROR(logger_, "Subscribe track conn_id: {} does not exist.", conn_handle);
                return;
            }

            const auto& prefix = handler->GetPrefix();
            const auto rid = conn_it->second.GetNextRequestId();

            /* Available parameters:
             * - AUTHORIZATION TOKEN (0x03)
             * - FORWARD (0x10)
             */
            const auto& filter = handler->GetFilter();
            Parameters params;
            if (const auto filter_type = GetFilterParameterType(filter); filter_type != ParameterType::kInvalid) {
                params.Add(filter_type, filter);
            }

            handler->SetTransport(GetSharedPtr());

            [[maybe_unused]] auto th = TrackHash({ prefix, {} });

            if (auto [_, is_new] = conn_it->second.request_handlers.try_emplace(rid, handler); !is_new) {
                SPDLOG_LOGGER_WARN(logger_, "Namespace already subscribed to (alias={})", th.track_fullname_hash);
                return;
            }

            SPDLOG_LOGGER_DEBUG(logger_,
                                "Sending {} to conn_id: {} request_id: {} prefix_hash: {}",
                                log_name,
                                conn_it->second.connection_handle,
                                rid,
                                th.track_namespace_hash);

            handler->data_ctx_id_ = quic_transport_->CreateDataContext(conn_handle, true, 0, true);
            quic_transport_->CreateStream(conn_handle, handler->data_ctx_id_, 0);

            SendCtrlMsg(conn_it->second, handler->data_ctx_id_, type, UintVar(rid), prefix, params);
        } catch (const std::exception& e) {
            SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending {} (error={})", log_name, e.what());
            // TODO: add error handling in libquicr in calling function
        }
    }

    void Transport::SendUnsubscribeNamespace(ConnectionHandle conn_handle,
                                             const std::shared_ptr<SubscribeNamespaceHandler>& handler)
    try {
        std::lock_guard<std::mutex> _(state_mutex_);
        auto conn_it = connections_.find(conn_handle);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Subscribe track conn_id: {} does not exist.", conn_handle);
            return;
        }

        conn_it->second.request_handlers.erase(handler->GetRequestId().value());

        const auto& prefix = handler->GetPrefix();

        [[maybe_unused]] auto th = TrackHash({ prefix, {} });

        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending UNSUBSCRIBE_NAMESPACE to conn_id: {} prefix_hash: {}",
                            conn_handle,
                            th.track_namespace_hash);

        SendCtrlMsg(conn_it->second, handler->data_ctx_id_, ControlMessageType::kNamespaceDone, prefix);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending UNSUBSCRIBE_NAMESPACE (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SubscribeNamespace(ConnectionHandle connection_handle,
                                       std::shared_ptr<SubscribeNamespaceHandler> handler)
    {
        SendSubscribeNamespace(connection_handle, std::move(handler));
    }

    void Transport::UnsubscribeNamespace(ConnectionHandle connection_handle,
                                         const std::shared_ptr<SubscribeNamespaceHandler>& handler)
    {
        SendUnsubscribeNamespace(connection_handle, handler);
    }

    void Transport::SendFetch(ConnectionContext& conn_ctx,
                              uint64_t request_id,
                              const FullTrackName& tfn,
                              std::uint8_t priority,
                              std::optional<messages::GroupOrder> group_order,
                              const messages::Location& start_location,
                              const messages::FetchEndLocation& end_location)
    try {
        messages::Location wire_end_location = { .group = end_location.group,
                                                 .object =
                                                   end_location.object.has_value() ? *end_location.object + 1 : 0 };

        /* Available parameters:
         * - AUTHORIZATION TOKEN (0x03): Conveys authorization for the fetch request.
         * - SUBSCRIBER PRIORITY (0x20): Priority of the fetch response relative to other data.
         * - GROUP ORDER (0x22): Preference for the order of groups in the fetch response.
         */
        auto params = Parameters{}
                        .Add(ParameterType::kSubscriberPriority, priority)
                        .AddOptional(ParameterType::kGroupOrder, group_order);

        SendCtrlMsg(conn_ctx,
                    conn_ctx.ctrl_data_ctx_id.value(),
                    ControlMessageType::kFetch,
                    UintVar(request_id),
                    messages::FetchType::kStandalone,
                    tfn.name_space,
                    tfn.name,
                    start_location,
                    wire_end_location,
                    params);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending Fetch (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendJoiningFetch(ConnectionContext& conn_ctx,
                                     uint64_t request_id,
                                     std::uint8_t priority,
                                     std::optional<messages::GroupOrder> group_order,
                                     uint64_t joining_request_id,
                                     messages::GroupId joining_start,
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

        SendCtrlMsg(conn_ctx,
                    conn_ctx.ctrl_data_ctx_id.value(),
                    ControlMessageType::kFetch,
                    UintVar(request_id),
                    absolute ? FetchType::kAbsoluteJoiningFetch : FetchType::kRelativeJoiningFetch,
                    joining_request_id,
                    joining_start,
                    params);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending JoiningFetch (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendFetchCancel(ConnectionContext& conn_ctx, uint64_t request_id)
    try {
        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), ControlMessageType::kFetchCancel, UintVar(request_id));
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending FetchCancel (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendFetchOk(ConnectionContext& conn_ctx,
                                uint64_t request_id,
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

        SendCtrlMsg(conn_ctx,
                    conn_ctx.ctrl_data_ctx_id.value(),
                    ControlMessageType::kFetchOk,
                    UintVar(request_id),
                    end_of_track,
                    largest_location,
                    params,
                    extensions);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending FetchOk (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SubscribeTrack(TransportConnId conn_id, std::shared_ptr<SubscribeTrackHandler> track_handler)
    {
        const auto& tfn = track_handler->GetFullTrackName();
        track_handler->connection_handle_ = conn_id;

        // Track hash is the track alias for now.
        auto th = TrackHash(tfn);

        auto proposed_track_alias = track_handler->GetTrackAlias();
        if (not proposed_track_alias.has_value()) {
            track_handler->SetTrackAlias(th.track_fullname_hash);
        } else {
            th.track_fullname_hash = proposed_track_alias.value();
        }

        SPDLOG_LOGGER_INFO(logger_, "Subscribe track conn_id: {} track_alias: {}", conn_id, th.track_fullname_hash);

        std::lock_guard<std::mutex> _(state_mutex_);
        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Subscribe track conn_id: {} does not exist.", conn_id);
            return;
        }

        if (!track_handler->IsPublisherInitiated()) {
            // increment and get the next request id if not initiated by publisher, which request Id is reused
            track_handler->SetRequestId(conn_it->second.GetNextRequestId());

        } else {
            if (!track_handler->GetReceivedTrackAlias().has_value()) {
                throw std::runtime_error("Missing received track alias for publisher initiated subscribe");
            }

            if (!track_handler->GetRequestId().has_value()) {
                throw std::runtime_error("Missing request id for publisher initiated subscribe");
            }

            conn_it->second.sub_by_recv_track_alias[*track_handler->GetReceivedTrackAlias()] = track_handler;
        }

        auto priority = track_handler->GetPriority();
        auto group_order = track_handler->GetGroupOrder();
        const auto& filter = track_handler->GetFilter();
        auto delivery_timeout = track_handler->GetDeliveryTimeout();

        track_handler->SetTransport(GetSharedPtr());

        // Set the track handler for tracking by request ID
        conn_it->second.request_handlers[*track_handler->GetRequestId()] = track_handler;

        if (!track_handler->IsPublisherInitiated()) {
            SendSubscribe(conn_it->second,
                          *track_handler->GetRequestId(),
                          tfn,
                          th,
                          priority,
                          group_order,
                          filter,
                          delivery_timeout);

            // Handle joining fetch, if requested.
            auto joining_fetch = track_handler->GetJoiningFetch();
            if (track_handler->GetJoiningFetch()) {
                // Make a joining fetch handler.
                const auto joining_fetch_handler = std::make_shared<JoiningFetchHandler>(track_handler);
                const auto& info = *joining_fetch;
                const auto fetch_rid = conn_it->second.GetNextRequestId();
                SPDLOG_LOGGER_INFO(logger_,
                                   "Subscribe with joining fetch conn_id: {} track_alias: {} subscribe id: {} "
                                   "joining subscribe id: {}",
                                   conn_id,
                                   th.track_fullname_hash,
                                   fetch_rid,
                                   *track_handler->GetRequestId());
                conn_it->second.request_handlers[fetch_rid] = std::move(joining_fetch_handler);
                SendJoiningFetch(conn_it->second,
                                 fetch_rid,
                                 info.priority,
                                 info.group_order,
                                 *track_handler->GetRequestId(),
                                 info.joining_start,
                                 info.absolute);
            }
        }
    }

    void Transport::UnsubscribeTrack(quicr::TransportConnId conn_id,
                                     const std::shared_ptr<SubscribeTrackHandler>& track_handler)
    {
        auto& conn_ctx = connections_[conn_id];
        RemoveSubscribeTrack(conn_ctx, *track_handler);
    }

    void Transport::UpdateTrackSubscription(TransportConnId conn_id,
                                            std::shared_ptr<SubscribeTrackHandler> track_handler)
    {
        const auto& tfn = track_handler->GetFullTrackName();
        auto th = TrackHash(tfn);

        SPDLOG_LOGGER_INFO(logger_, "Subscribe track conn_id: {} hash: {}", conn_id, th.track_fullname_hash);

        std::lock_guard<std::mutex> _(state_mutex_);
        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Subscribe track conn_id: {} does not exist.", conn_id);
            return;
        }

        if (not track_handler->GetRequestId().has_value()) {
            return;
        }

        auto priority = track_handler->GetPriority();
        SendRequestUpdate(conn_it->second,
                          conn_it->second.GetNextRequestId(),
                          track_handler->GetRequestId().value(),
                          th,
                          track_handler->pending_new_group_request_id_,
                          priority,
                          true);
    }

    void Transport::RemoveSubscribeTrack(ConnectionContext& conn_ctx,
                                         SubscribeTrackHandler& handler,
                                         bool remove_handler)
    {
        auto handler_status = handler.GetStatus();

        switch (handler_status) {
            case SubscribeTrackHandler::Status::kDoneByFin:
                [[fallthrough]];
            case SubscribeTrackHandler::Status::kDoneByReset:
                [[fallthrough]];
            case SubscribeTrackHandler::Status::kOk:
                try {
                    if (not handler.IsPublisherInitiated() && not conn_ctx.closed) {
                        SendUnsubscribe(conn_ctx, handler.GetRequestId().value());
                    }
                } catch (const std::exception& e) {
                    SPDLOG_LOGGER_ERROR(logger_, "Failed to send unsubscribe: {}", e.what());
                }

                handler.SetStatus(SubscribeTrackHandler::Status::kNotSubscribed);
                break;

            default:
                break;
        }

        if (remove_handler) {
            std::lock_guard<std::mutex> _(state_mutex_);

            if (handler.GetRequestId().has_value()) {
                conn_ctx.request_handlers.erase(*handler.GetRequestId());
            }

            if (handler.GetReceivedTrackAlias().has_value()) {
                conn_ctx.sub_by_recv_track_alias.erase(handler.GetReceivedTrackAlias().value());
            }
        }
    }

    void Transport::UnpublishTrack(TransportConnId conn_id, const std::shared_ptr<PublishTrackHandler>& track_handler)
    {
        // Generate track alias
        auto tfn = track_handler->GetFullTrackName();
        auto th = TrackHash(tfn);

        SPDLOG_LOGGER_INFO(logger_, "Unpublish track conn_id: {} hash: {}", conn_id, th.track_fullname_hash);

        std::unique_lock<std::mutex> lock(state_mutex_);

        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Unpublish track conn_id: {} does not exist.", conn_id);
            return;
        }

        conn_it->second.pub_tracks_by_track_alias.erase(th.track_fullname_hash);

        if (!track_handler->GetRequestId().has_value()) {
            return;
        }

        conn_it->second.request_handlers.erase(track_handler->GetRequestId().value());

        /*
         * This is a round about way to send subscribe done because of the announce flow. This
         * will go away if we stop using the announce flow. For now, it works for both announce
         * and publish flows.
         */
        auto pub_ns_it = conn_it->second.pub_tracks_by_name.find(th.track_namespace_hash);
        if (pub_ns_it != conn_it->second.pub_tracks_by_name.end()) {
            auto pub_n_it = pub_ns_it->second.find(th.track_name_hash);
            if (pub_n_it != pub_ns_it->second.end()) {
                // Send subscribe done if track has subscriber and is sending
                if (pub_n_it->second->GetStatus() == PublishTrackHandler::Status::kOk &&
                    pub_n_it->second->GetRequestId().has_value()) {
                    SPDLOG_LOGGER_INFO(
                      logger_,
                      "Unpublish track namespace hash: {} track_name_hash: {} track_alias: {}, sending "
                      "publish_done",
                      th.track_namespace_hash,
                      th.track_name_hash,
                      th.track_fullname_hash);
                    SendPublishDone(conn_it->second,
                                    *pub_n_it->second->GetRequestId(),
                                    PublishDoneStatusCode::kSubscribtionEnded,
                                    "Unpublish track");
                } else {
                    SPDLOG_LOGGER_INFO(logger_,
                                       "Unpublish track namespace hash: {} track_name_hash: {} track_alias: {}",
                                       th.track_namespace_hash,
                                       th.track_name_hash,
                                       th.track_fullname_hash);
                }

                pub_n_it->second->publish_data_ctx_id_ = 0;

                lock.unlock();

                // We continue to use the kNotAnnounced state when removing. Might make sense to use kDestroyed instead
                pub_n_it->second->SetStatus(PublishTrackHandler::Status::kNotAnnounced);

                lock.lock();

                pub_ns_it->second.erase(pub_n_it);
            }

            quic_transport_->DeleteDataContext(conn_id, track_handler->publish_data_ctx_id_);
        }
    }

    void Transport::PublishTrack(TransportConnId conn_id, std::shared_ptr<PublishTrackHandler> track_handler)
    {
        const auto tfn = track_handler->GetFullTrackName();
        auto th = TrackHash(tfn);
        SPDLOG_LOGGER_INFO(logger_, "Publish track conn_id: {} hash: {}", conn_id, th.track_fullname_hash);

        std::unique_lock<std::mutex> lock(state_mutex_);

        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Publish track conn_id: {} does not exist.", conn_id);
            return;
        }

        track_handler->SetRequestId(conn_it->second.GetNextRequestId());

        if (!track_handler->GetTrackAlias().has_value()) {
            track_handler->SetTrackAlias(th.track_fullname_hash);
        }

        // Add state to received request ID since a subscribe will not be received for this request
        conn_it->second.recv_req_id[*track_handler->GetRequestId()] = { track_handler->GetFullTrackName(), th };

        track_handler->SetStatus(PublishTrackHandler::Status::kPendingPublishOk);
        SendPublish(conn_it->second,
                    *track_handler->GetRequestId(),
                    tfn,
                    track_handler->GetTrackAlias().value(),
                    GroupOrder::kAscending,
                    std::make_optional(
                      Location{ track_handler->largest_location_.group, track_handler->largest_location_.object }),
                    true,
                    track_handler->support_new_group_request_);

        track_handler->connection_handle_ = conn_id;
        SPDLOG_LOGGER_INFO(logger_,
                           "Publish track creating new data context connId {}, track namespace hash: {}, name hash: {}",
                           conn_id,
                           th.track_namespace_hash,
                           th.track_name_hash);
        track_handler->publish_data_ctx_id_ =
          quic_transport_->CreateDataContext(conn_id,
                                             track_handler->default_track_mode_ == TrackMode::kDatagram ? false : true,
                                             track_handler->default_priority_,
                                             false,
                                             track_handler->GetRequestId());

        // Set this transport as the one for the publisher to use.
        track_handler->SetTransport(GetSharedPtr());

        // Hold ref to track handler
        conn_it->second.request_handlers[*track_handler->GetRequestId()] = track_handler;
        conn_it->second.pub_tracks_by_name[th.track_namespace_hash][th.track_name_hash] = track_handler;
        conn_it->second.pub_tracks_by_track_alias[th.track_fullname_hash][conn_id] = track_handler;
        conn_it->second.pub_tracks_by_data_ctx_id[track_handler->publish_data_ctx_id_] = std::move(track_handler);
    }

    void Transport::PublishNamespace(ConnectionHandle conn_id,
                                     std::shared_ptr<PublishNamespaceHandler> ns_handler,
                                     bool passive)
    {
        auto prefix_hash = hash(ns_handler->GetPrefix());
        SPDLOG_LOGGER_INFO(logger_, "Publish namespace conn_id: {0} hash: {1}", conn_id, prefix_hash);

        std::unique_lock<std::mutex> lock(state_mutex_);

        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Publish track conn_id: {0} does not exist.", conn_id);
            return;
        }

        if (!passive) {
            ns_handler->SetRequestId(conn_it->second.GetNextRequestId());

            SPDLOG_LOGGER_INFO(logger_, "Publishing to namespace hash: {0} sending ANNOUNCE message", prefix_hash);

            lock.unlock();

            ns_handler->SetStatus(PublishNamespaceHandler::Status::kPendingResponse);

            lock.lock();

            SendPublishNamespace(conn_it->second, *ns_handler->GetRequestId(), ns_handler->GetPrefix());
            conn_it->second.request_handlers[*ns_handler->GetRequestId()] = ns_handler;

        } else {
            ns_handler->SetStatus(PublishNamespaceHandler::Status::kOk);
        }

        ns_handler->SetConnectionId(conn_id);
        ns_handler->SetTransport(GetSharedPtr());
    }

    void Transport::PublishNamespaceDone(ConnectionHandle conn_id,
                                         const std::shared_ptr<PublishNamespaceHandler>& track_handler)
    {
        const auto& prefix = track_handler->GetPrefix();
        const auto prefix_hash = hash(prefix);

        SPDLOG_LOGGER_INFO(logger_, "PublishNamespaceDone (conn_id={}, prefix_hash={})", conn_id, prefix_hash);

        std::lock_guard<std::mutex> lock(state_mutex_);

        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_,
                                "PublishNamespaceDone failed, namespace does not exist (conn_id={}, prefix_hash={})",
                                conn_id,
                                prefix_hash);
            return;
        }

        SendPublishNamespaceDone(conn_it->second, track_handler->GetRequestId().value());
        conn_it->second.request_handlers.erase(track_handler->GetRequestId().value());
    }

    void Transport::ResolvePublish(const ConnectionHandle connection_handle,
                                   const uint64_t request_id,
                                   const PublishAttributes& attributes,
                                   const PublishResponse& publish_response,
                                   std::shared_ptr<SubscribeTrackHandler> handler)
    {
        const auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        auto error_code = ErrorCode::kInternalError;
        auto reason = std::string("Internal error");

        switch (publish_response.reason_code) {
            case PublishResponse::ReasonCode::kOk: {
                // Update the handler to correctly work with publisher initiated subscribe
                if (handler) {
                    if (attributes.is_publisher_initiated) {
                        handler->SetPublishInitiated();
                    }

                    handler->SetConnectionId(connection_handle);
                    handler->SetRequestId(request_id);
                    handler->SetReceivedTrackAlias(attributes.track_alias);
                    handler->SetPriority(attributes.priority);
                    handler->SetDeliveryTimeout(attributes.delivery_timeout);
                    handler->SetPublisherDefaultGroupOrder(attributes.publisher_default_group_order);
                    handler->SupportNewGroupRequest(attributes.dynamic_groups);

                    SubscribeTrack(connection_handle, std::move(handler));
                }

                SendPublishOk(conn_it->second,
                              request_id,
                              attributes.forward,
                              attributes.priority,
                              attributes.group_order,
                              attributes.filter);

                return;
            }

            case PublishResponse::ReasonCode::kRejected:
                error_code = ErrorCode::kUninterested;
                reason = "Rejected; not interested";
                break;

            case PublishResponse::ReasonCode::kNotAuthorized:
                error_code = ErrorCode::kUnauthorized;
                reason = "Not authorized";
                break;

            default:
                break;
        }

        SendRequestError(conn_it->second, request_id, error_code, 0ms, reason);
    }

    void Transport::StandaloneFetchReceived(
      [[maybe_unused]] ConnectionHandle connection_handle,
      [[maybe_unused]] uint64_t request_id,
      [[maybe_unused]] const FullTrackName& track_full_name,
      [[maybe_unused]] const quicr::messages::StandaloneFetchAttributes& attributes)
    {
    }

    void Transport::JoiningFetchReceived([[maybe_unused]] ConnectionHandle connection_handle,
                                         [[maybe_unused]] uint64_t request_id,
                                         [[maybe_unused]] const FullTrackName& track_full_name,
                                         [[maybe_unused]] const quicr::messages::JoiningFetchAttributes& attributes)
    {
    }

    void Transport::FetchTrack(ConnectionHandle connection_handle, std::shared_ptr<FetchTrackHandler> track_handler)
    {
        const auto& tfn = track_handler->GetFullTrackName();
        auto th = TrackHash(tfn);

        track_handler->SetTrackAlias(th.track_fullname_hash);

        SPDLOG_LOGGER_INFO(logger_, "Fetch track conn_id: {} hash: {}", connection_handle, th.track_fullname_hash);

        std::lock_guard<std::mutex> _(state_mutex_);
        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Fetch track conn_id: {} does not exist.", connection_handle);
            return;
        }

        track_handler->SetRequestId(conn_it->second.GetNextRequestId());

        SPDLOG_LOGGER_DEBUG(logger_, "subscribe id (from fetch) to add to memory: {}", *track_handler->GetRequestId());

        auto priority = track_handler->GetPriority();
        auto group_order = track_handler->GetGroupOrder();
        auto start_location = track_handler->GetStartLocation();
        auto end_location = track_handler->GetEndLocation();

        track_handler->SetStatus(FetchTrackHandler::Status::kPendingResponse);

        const auto request_id = *track_handler->GetRequestId();
        conn_it->second.request_handlers[*track_handler->GetRequestId()] = track_handler;

        SendFetch(conn_it->second, request_id, tfn, priority, group_order, start_location, end_location);
    }

    void Transport::CancelFetchTrack(ConnectionHandle connection_handle,
                                     std::shared_ptr<FetchTrackHandler> track_handler)
    {
        std::lock_guard<std::mutex> _(state_mutex_);
        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Fetch track conn_id: {} does not exist.", connection_handle);
            return;
        }

        const auto sub_id = track_handler->GetRequestId();
        if (!sub_id.has_value()) {
            return;
        }

        conn_it->second.request_handlers.erase(track_handler->GetRequestId().value());

        track_handler->SetRequestId(std::nullopt);

        if (track_handler->GetStatus() == FetchTrackHandler::Status::kDoneByFin ||
            track_handler->GetStatus() == FetchTrackHandler::Status::kDoneByReset) {
            return;
        }

        SendFetchCancel(conn_it->second, sub_id.value());

        track_handler->SetStatus(FetchTrackHandler::Status::kNotConnected);
    }

    std::shared_ptr<PublishTrackHandler> Transport::GetPubTrackHandler(ConnectionContext& conn_ctx, TrackHash& th)
    {
        auto pub_ns_it = conn_ctx.pub_tracks_by_name.find(th.track_namespace_hash);
        if (pub_ns_it == conn_ctx.pub_tracks_by_name.end()) {
            return nullptr;
        }

        auto pub_n_it = pub_ns_it->second.find(th.track_name_hash);
        if (pub_n_it == pub_ns_it->second.end()) {
            return nullptr;
        }

        return pub_n_it->second;
    }

    void Transport::RemoveAllTracksForConnectionClose(ConnectionContext& conn_ctx)
    {
        // clean up subscriber handlers on disconnect
        for (auto& [req_id, req] : conn_ctx.request_handlers) {
            if (auto h = req.Get<SubscribeTrackHandler>()) {

                RemoveSubscribeTrack(conn_ctx, *h, false);
                if (req.handler->GetConnectionId() == conn_ctx.connection_handle) {
                    if (auto h = req.Get<SubscribeTrackHandler>()) {
                        h->SetStatus(SubscribeTrackHandler::Status::kNotConnected);
                    }
                }
            } else if (auto h = req.Get<PublishTrackHandler>()) {
                h->SetStatus(PublishTrackHandler::Status::kNotConnected);
                h->SetRequestId(std::nullopt);
            }
        }

        conn_ctx.pub_tracks_by_data_ctx_id.clear();
        conn_ctx.pub_tracks_by_name.clear();
        conn_ctx.recv_req_id.clear();
        conn_ctx.request_handlers.clear();
        conn_ctx.sub_by_recv_track_alias.clear();
    }

    // ---------------------------------------------------------------------------------------
    // Transport handler callbacks
    // ---------------------------------------------------------------------------------------

    void Transport::OnConnectionStatus(const TransportConnId& conn_id, const TransportStatus status)
    {
        SPDLOG_LOGGER_DEBUG(logger_, "Connection status conn_id: {} status: {}", conn_id, static_cast<int>(status));
        ConnectionStatus conn_status = ConnectionStatus::kConnected;
        bool remove_connection = false;
        auto& conn_ctx = connections_[conn_id];

        switch (status) {
            case TransportStatus::kReady: {
                if (client_mode_) {
                    SPDLOG_LOGGER_INFO(logger_,
                                       "Connection established, creating bi-dir stream and sending CLIENT_SETUP");

                    conn_ctx.ctrl_data_ctx_id = quic_transport_->CreateDataContext(conn_id, true, 0, true);
                    conn_ctx.ctrl_stream_id =
                      quic_transport_->CreateStream(conn_id, conn_ctx.ctrl_data_ctx_id.value(), 0);

                    SendClientSetup();

                    if (client_mode_) {
                        status_ = Status::kPendingServerSetup;
                    } else {
                        status_ = Status::kReady;
                    }

                    conn_status = ConnectionStatus::kConnected;
                }
                break;
            }

            case TransportStatus::kConnecting:
                if (client_mode_) {
                    status_ = Status::kConnecting;
                }

                conn_status = ConnectionStatus::kConnected;
                break;
            case TransportStatus::kRemoteRequestClose:
                conn_status = ConnectionStatus::kClosedByRemote;
                conn_ctx.closed = true;
                remove_connection = true;
                break;

            case TransportStatus::kIdleTimeout:
                conn_status = ConnectionStatus::kIdleTimeout;
                conn_ctx.closed = true;
                remove_connection = true;
                break;

            case TransportStatus::kDisconnected: {
                conn_status = ConnectionStatus::kNotConnected;
                conn_ctx.closed = true;
                remove_connection = true;
                break;
            }

            case TransportStatus::kShuttingDown:
                conn_status = ConnectionStatus::kNotConnected;
                break;

            case TransportStatus::kShutdown:
                conn_status = ConnectionStatus::kNotConnected;
                remove_connection = true;
                status_ = Status::kNotReady;
                break;
        }

        if (remove_connection) {
            // Clean up publish and subscribe tracks
            auto conn_it = connections_.find(conn_id);
            if (conn_it != connections_.end()) {
                if (client_mode_) {
                    status_ = Status::kNotConnected;
                }

                RemoveAllTracksForConnectionClose(conn_it->second);
                ConnectionStatusChanged(conn_id, conn_status);

                std::lock_guard<std::mutex> _(state_mutex_);
                connections_.erase(conn_it);
            }
        }

        StatusChanged(status_);
    }

    void Transport::OnNewConnection(const TransportConnId& conn_id, const TransportRemote& remote)
    {
        auto [conn_ctx, is_new] = connections_.try_emplace(conn_id, ConnectionContext{});
        conn_ctx->second.next_request_id = 1; // Server is odd, starting at 1

        conn_ctx->second.connection_handle = conn_id;
        NewConnectionAccepted(conn_id, { remote.host_or_ip, remote.port });
    }

    void Transport::OnRecvStream(const TransportConnId& conn_id,
                                 uint64_t stream_id,
                                 std::optional<DataContextId> data_ctx_id,
                                 const bool is_bidir)
    try {
        auto rx_ctx = quic_transport_->GetStreamRxContext(conn_id, stream_id);
        auto& conn_ctx = connections_[conn_id];

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

            auto& data = *data_opt.value();

            // CONTROL STREAM
            if (is_bidir) {
                auto& ctrl_msg_buffer = conn_ctx.ctrl_msg_buffer[stream_id];
                ctrl_msg_buffer.data.insert(ctrl_msg_buffer.data.end(), data.begin(), data.end());

                rx_ctx->data_queue.PopFront();
                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Transport:ControlMessageReceived conn_id: {} stream_id: {} data size: {}",
                                    conn_id,
                                    stream_id,
                                    ctrl_msg_buffer.data.size());

                // Set the default/primary control stream and data context id
                if (not conn_ctx.ctrl_data_ctx_id) {
                    if (not data_ctx_id) {
                        CloseConnection(conn_id,
                                        messages::TerminationReason::kInternalError,
                                        "Received bidir is missing data context");
                        return;
                    }

                    conn_ctx.ctrl_data_ctx_id = data_ctx_id;

                    if (!conn_ctx.ctrl_stream_id.has_value()) { // First bidir is the primary control stream
                        conn_ctx.ctrl_stream_id = stream_id;
                    }
                }

                while (ctrl_msg_buffer.data.size() > 0) {
                    if (not ctrl_msg_buffer.msg_type.has_value()) {
                        // Decode message type
                        auto uv_sz = UintVar::Size(ctrl_msg_buffer.data.front());
                        if (ctrl_msg_buffer.data.size() < uv_sz) {
                            i = kReadLoopMaxPerStream - 4;
                            break; // Not enough bytes to process control message. Try again once more.
                        }

                        auto msg_type = uint64_t(
                          quicr::UintVar({ ctrl_msg_buffer.data.begin(), ctrl_msg_buffer.data.begin() + uv_sz }));

                        ctrl_msg_buffer.data.erase(ctrl_msg_buffer.data.begin(), ctrl_msg_buffer.data.begin() + uv_sz);

                        ctrl_msg_buffer.msg_type = static_cast<ControlMessageType>(msg_type);
                    }

                    uint16_t payload_len = 0;

                    // Decode control payload length in bytes
                    if (ctrl_msg_buffer.data.size() < sizeof(payload_len)) {
                        i = kReadLoopMaxPerStream - 4;
                        break; // Not enough bytes to process control message. Try again once more.
                    }

                    std::memcpy(&payload_len, ctrl_msg_buffer.data.data(), sizeof(payload_len));
                    payload_len = SwapBytes(payload_len);

                    if (ctrl_msg_buffer.data.size() < payload_len + sizeof(payload_len)) {
                        i = kReadLoopMaxPerStream - 4;
                        break; // Not enough bytes to process control message. Try again once more.
                    }

                    if (ProcessCtrlMessage(conn_ctx,
                                           data_ctx_id.value(),
                                           ctrl_msg_buffer.msg_type.value(),
                                           { ctrl_msg_buffer.data.begin() + sizeof(payload_len),
                                             ctrl_msg_buffer.data.begin() + sizeof(payload_len) + payload_len })) {

                        // Reset the control message buffer and message type to start a new message.
                        ctrl_msg_buffer.msg_type = std::nullopt;
                        ctrl_msg_buffer.data.erase(ctrl_msg_buffer.data.begin(),
                                                   ctrl_msg_buffer.data.begin() + sizeof(payload_len) + payload_len);
                    } else {
                        conn_ctx.metrics.invalid_ctrl_stream_msg++;
                        ctrl_msg_buffer.msg_type = std::nullopt;
                        ctrl_msg_buffer.data.erase(ctrl_msg_buffer.data.begin(),
                                                   ctrl_msg_buffer.data.begin() + sizeof(payload_len) + payload_len);
                    }
                }
                continue;
            } // end of is_bidir

            // DATA OBJECT
            if (rx_ctx->is_new) {
                /*
                 * Process data subgroup header - assume that the start of stream will always have enough bytes
                 * for track alias
                 */
                auto type_sz = UintVar::Size(data.front());
                if (data.size() < type_sz) {
                    SPDLOG_LOGGER_WARN(
                      logger_,
                      "New stream {} does not have enough bytes to process start of stream header len: {} < {}",
                      stream_id,
                      data.size(),
                      type_sz);
                    i = kReadLoopMaxPerStream;
                    continue; // Not enough bytes to process control message. Try again once more.
                }

                SPDLOG_LOGGER_DEBUG(
                  logger_, "New stream conn_id: {} stream_id: {} data size: {}", conn_id, stream_id, data.size());

                auto msg_type = uint64_t(quicr::UintVar({ data.begin(), data.begin() + type_sz }));
                auto cursor_it = std::next(data.begin(), type_sz);

                SPDLOG_LOGGER_TRACE(logger_, "Received stream message type: 0x{:02x} ({})", msg_type, msg_type);

                bool parsed_header = false;
                switch (GetStreamMessageType(msg_type)) {
                    case StreamMessageType::kSubgroupHeader: {
                        const auto properties = StreamHeaderProperties(msg_type);
                        parsed_header = OnRecvSubgroup(properties, cursor_it, *rx_ctx, stream_id, conn_ctx, *data_opt);
                        break;
                    }
                    case StreamMessageType::kFetchHeader: {
                        parsed_header = OnRecvFetch(cursor_it, *rx_ctx, stream_id, conn_ctx, *data_opt);
                        break;
                    }
                    default:
                        SPDLOG_LOGGER_WARN(
                          logger_, "Received start of stream with invalid header type {}, dropping", msg_type);
                        conn_ctx.metrics.rx_stream_invalid_type++;

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

                        SPDLOG_LOGGER_INFO(
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

                rx_ctx->data_queue.PopFront();

            } else if (rx_ctx->caller_any.has_value()) {
                rx_ctx->data_queue.PopFront();

                // fast processing for existing stream using weak pointer to subscribe handler
                auto sub_handler_weak = std::any_cast<std::weak_ptr<SubscribeTrackHandler>>(rx_ctx->caller_any);
                if (auto sub_handler = sub_handler_weak.lock()) {
                    try {
                        sub_handler->StreamDataRecv(false, stream_id, data_opt.value());
                    } catch (const ProtocolViolationException& e) {
                        SPDLOG_LOGGER_ERROR(logger_, "Protocol violation on stream data recv: {}", e.reason);
                        CloseConnection(conn_id, TerminationReason::kProtocolViolation, e.reason);
                    } catch (std::exception& e) {
                        SPDLOG_LOGGER_ERROR(logger_, "Caught exception on stream data recv: {}", e.what());
                        CloseConnection(conn_id, TerminationReason::kInternalError, "Internal error");
                    }
                } else {
                    SPDLOG_LOGGER_ERROR(
                      logger_,
                      "Received data on existing stream_id: {} with no handler anymore, resetting stream",
                      stream_id);

                    if (data_ctx_id.has_value()) {
                        quic_transport_->CloseStream(conn_id, data_ctx_id.value(), stream_id, true);
                    }
                }
            }
        } // end of for loop rx data queue
    } catch (const TransportException& e) {
        SPDLOG_LOGGER_INFO(logger_, "OnRecvStream: connection or stream no longer exists (error={})", e.what());
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception on receiving stream. (error={})", e.what());
        throw;

        // TODO(tievens): Add metrics to track if this happens
    }

    void Transport::OnStreamClosed(const ConnectionHandle& connection_handle,
                                   [[maybe_unused]] std::uint64_t stream_id,
                                   std::shared_ptr<StreamRxContext> rx_ctx,
                                   std::optional<uint64_t> request_id,
                                   StreamClosedFlag flag)
    {
        SPDLOG_LOGGER_DEBUG(logger_, "Stream {} closed", stream_id);

        if (rx_ctx == nullptr) { // If not RX, it's for TX/publisher
            if (request_id.has_value()) {
                const auto& conn_ctx = connections_[connection_handle];
                const auto& handler_it = conn_ctx.request_handlers.find(*request_id);
                if (handler_it != conn_ctx.request_handlers.end()) {
                    if (auto pub_handler = handler_it->second.Get<PublishTrackHandler>()) {
                        SPDLOG_LOGGER_DEBUG(
                          logger_, "Stream {} closed, calling publish handler stream close", stream_id);
                        pub_handler->StreamClosed(stream_id, flag == StreamClosedFlag::kReset);
                    }
                }
            }
            return;
        }

        try {

            if ((stream_id & 2) == 0) { // bidir
                auto& conn_ctx = connections_[connection_handle];

                switch (flag) {
                    case StreamClosedFlag::kFin:
                        if (conn_ctx.ctrl_stream_id.has_value() && conn_ctx.ctrl_stream_id == stream_id) {
                            CloseConnection(
                              connection_handle, TerminationReason::kProtocolViolation, "Primary control stream FIN");
                        } else {
                            conn_ctx.ctrl_msg_buffer.erase(stream_id);
                        }

                        break;
                    case StreamClosedFlag::kReset:
                        if (conn_ctx.ctrl_stream_id.has_value() && conn_ctx.ctrl_stream_id == stream_id) {
                            CloseConnection(
                              connection_handle, TerminationReason::kProtocolViolation, "Primary control stream RESET");
                        } else {
                            conn_ctx.ctrl_msg_buffer.erase(stream_id);
                        }
                        break;
                }

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
                    SPDLOG_LOGGER_ERROR(logger_, "Protocol violation on stream data recv: {}", e.reason);
                    CloseConnection(connection_handle, TerminationReason::kProtocolViolation, e.reason);
                } catch (const std::exception& e) {
                    SPDLOG_LOGGER_ERROR(logger_, "Caught exception on stream data recv: {}", e.what());
                    CloseConnection(connection_handle, TerminationReason::kInternalError, "Internal error");
                }
            }
        } catch (const std::bad_any_cast&) {
            SPDLOG_LOGGER_WARN(logger_, "Received stream closed for unknown handler");
        }
    }

    bool Transport::OnRecvSubgroup(StreamHeaderProperties properties,
                                   std::vector<uint8_t>::const_iterator cursor_it,
                                   StreamRxContext& rx_ctx,
                                   std::uint64_t stream_id,
                                   ConnectionContext& conn_ctx,
                                   std::shared_ptr<const std::vector<uint8_t>> data) const
    {
        uint64_t track_alias = 0;
        std::optional<uint8_t> priority;

        try {
            // First header in subgroup starts with track alias
            auto ta_sz = UintVar::Size(*cursor_it);
            track_alias = uint64_t(quicr::UintVar({ cursor_it, cursor_it + ta_sz }));
            cursor_it += ta_sz;

            auto group_id_sz = UintVar::Size(*cursor_it);
            cursor_it += group_id_sz;

            if (properties.subgroup_id_mode == SubgroupIdType::kExplicit) {
                auto subgroup_id_sz = UintVar::Size(*cursor_it);
                cursor_it += subgroup_id_sz;
            }

            if (!properties.default_priority) {
                priority = *cursor_it;
            }

        } catch (std::invalid_argument&) {
            SPDLOG_LOGGER_WARN(logger_, "Received start of stream without enough bytes to process uintvar");
            return false;
        }

        auto sub_it = conn_ctx.sub_by_recv_track_alias.find(track_alias);
        if ((sub_it == conn_ctx.sub_by_recv_track_alias.end() || sub_it->second == nullptr)) {
            conn_ctx.metrics.rx_stream_unknown_track_alias++;
            SPDLOG_LOGGER_WARN(
              logger_,
              "Received stream_header_subgroup to unknown subscribe track track_alias: {} stream: {}, ignored",
              track_alias,
              stream_id);

            return false;
        }

        rx_ctx.is_new = false;

        rx_ctx.caller_any = std::make_any<std::weak_ptr<SubscribeTrackHandler>>(sub_it->second);
        if (priority.has_value()) {
            sub_it->second->SetPriority(*priority);
        }

        sub_it->second->StreamDataRecv(true, stream_id, std::move(data));
        return true;
    }

    bool Transport::OnRecvFetch(std::vector<uint8_t>::const_iterator cursor_it,
                                StreamRxContext& rx_ctx,
                                std::uint64_t stream_id,
                                ConnectionContext& conn_ctx,
                                std::shared_ptr<const std::vector<uint8_t>> data) const
    {
        uint64_t request_id = 0;

        try {
            // Extract Subscribe ID.
            const std::size_t sub_sz = UintVar::Size(*cursor_it);
            request_id = static_cast<std::uint64_t>(UintVar({ cursor_it, cursor_it + sub_sz }));

        } catch (std::invalid_argument&) {
            SPDLOG_LOGGER_WARN(logger_, "Received start of stream without enough bytes to process uintvar");
            return false;
        }

        rx_ctx.is_new = false;

        const auto fetch_it = conn_ctx.request_handlers.find(request_id);
        if (fetch_it == conn_ctx.request_handlers.end()) {
            // TODO: Metrics.
            SPDLOG_LOGGER_WARN(logger_,
                               "Received fetch_header to unknown fetch track request_id: {} stream: {}, ignored",
                               request_id,
                               stream_id);

            // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging
            return false;
        }

        if (auto h = fetch_it->second.Get<SubscribeTrackHandler>()) {
            h->StreamDataRecv(true, stream_id, std::move(data));
            rx_ctx.caller_any = std::make_any<std::weak_ptr<SubscribeTrackHandler>>(h);
            return true;
        }

        return false;
    }

    void Transport::OnRecvDgram(const TransportConnId& conn_id, std::optional<DataContextId> data_ctx_id)
    {
        for (int i = 0; i < kReadLoopMaxPerStream; i++) {
            auto data = quic_transport_->Dequeue(conn_id, data_ctx_id);
            if (data && !data->empty() && data->size() > 3) {
                auto msg_type = data->front();

                // Message type needs to be either datagram header types or status types.
                if (!DatagramHeaderProperties::IsValid(msg_type)) {
                    SPDLOG_LOGGER_DEBUG(
                      logger_, "Received datagram that is not a supported datagram type, dropping: {}", msg_type);
                    auto& conn_ctx = connections_[conn_id];
                    conn_ctx.metrics.rx_dgram_invalid_type++;
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

                auto& conn_ctx = connections_[conn_id];
                auto sub_it = conn_ctx.sub_by_recv_track_alias.find(track_alias);
                if (sub_it == conn_ctx.sub_by_recv_track_alias.end()) {
                    conn_ctx.metrics.rx_dgram_unknown_track_alias++;

                    SPDLOG_LOGGER_DEBUG(
                      logger_, "Received datagram to unknown subscribe track track alias: {}, ignored", track_alias);

                    // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                    continue;
                }

                SPDLOG_LOGGER_TRACE(logger_,
                                    "Received object datagram conn_id: {} data_ctx_id: {} subscriber_id: {} "
                                    "track_alias: {} group_id: {4} object_id: {5} data size: {6}",
                                    conn_id,
                                    (data_ctx_id ? *data_ctx_id : 0),
                                    sub_id,
                                    track_alias,
                                    data.value()->size());

                auto handler = static_cast<SubscribeTrackHandler*>(sub_it->second.get());

                handler->DgramDataRecv(data);
            } else if (data) {
                auto& conn_ctx = connections_[conn_id];
                conn_ctx.metrics.rx_dgram_decode_failed++;

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Failed to decode datagram conn_id: {} data_ctx_id: {} size: {}",
                                    conn_id,
                                    (data_ctx_id ? *data_ctx_id : 0),
                                    data->size());
            }
        }
    }

    void Transport::OnConnectionMetricsSampled(const MetricsTimeStamp sample_time,
                                               const TransportConnId conn_id,
                                               const QuicConnectionMetrics& quic_connection_metrics)
    {
        // TODO: doesn't require lock right now, but might need to add lock
        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            // Connection no longer exists, skip metrics sampling
            return;
        }

        auto& conn = conn_it->second;

        conn.metrics.last_sample_time = sample_time.time_since_epoch() / std::chrono::microseconds(1);
        conn.metrics.quic = quic_connection_metrics;

        if (client_mode_) {
            MetricsSampled(conn.metrics);
        } else {
            MetricsSampled(conn_id, conn.metrics);
        }
    }

    void Transport::OnDataMetricsStampled(const MetricsTimeStamp sample_time,
                                          const TransportConnId conn_id,
                                          const DataContextId data_ctx_id,
                                          const QuicDataContextMetrics& quic_data_context_metrics)
    {
        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            // Connection no longer exists, skip metrics sampling
            return;
        }

        const auto& conn = conn_it->second;
        const auto& pub_th_it = conn.pub_tracks_by_data_ctx_id.find(data_ctx_id);

        if (pub_th_it != conn.pub_tracks_by_data_ctx_id.end()) {
            auto& pub_h = pub_th_it->second;
            pub_h->publish_track_metrics_.last_sample_time =
              sample_time.time_since_epoch() / std::chrono::microseconds(1);

            pub_h->publish_track_metrics_.quic.tx_buffer_drops = quic_data_context_metrics.tx_buffer_drops;
            pub_h->publish_track_metrics_.quic.tx_callback_ms = quic_data_context_metrics.tx_callback_ms;
            pub_h->publish_track_metrics_.quic.tx_delayed_callback = quic_data_context_metrics.tx_delayed_callback;
            pub_h->publish_track_metrics_.quic.tx_object_duration_us = quic_data_context_metrics.tx_object_duration_us;
            pub_h->publish_track_metrics_.quic.tx_queue_discards = quic_data_context_metrics.tx_queue_discards;
            pub_h->publish_track_metrics_.quic.tx_queue_expired = quic_data_context_metrics.tx_queue_expired;
            pub_h->publish_track_metrics_.quic.tx_queue_size = quic_data_context_metrics.tx_queue_size;
            pub_h->publish_track_metrics_.quic.tx_reset_wait = quic_data_context_metrics.tx_reset_wait;

            pub_h->MetricsSampled(pub_h->publish_track_metrics_);
        }

        for (const auto& [_, req] : conn.request_handlers) {
            if (auto h = req.Get<SubscribeTrackHandler>(); h) {
                h->MetricsSampled(h->subscribe_track_metrics_);
            }
        }
    }

    void Transport::OnNewDataContext(const ConnectionHandle&, const DataContextId&) {}

    void Transport::CloseConnection(TransportConnId conn_id,
                                    messages::TerminationReason reason,
                                    const std::string& reason_str)
    {
        std::ostringstream log_msg;
        log_msg << "Closing conn_id: " << conn_id;
        switch (reason) {
            case messages::TerminationReason::kNoError:
                log_msg << " no error";
                break;
            case messages::TerminationReason::kInternalError:
                log_msg << " internal error: " << reason_str;
                break;
            case messages::TerminationReason::kUnauthorized:
                log_msg << " unauthorized: " << reason_str;
                break;
            case messages::TerminationReason::kProtocolViolation:
                log_msg << " protocol violation: " << reason_str;
                break;
            case messages::TerminationReason::kDuplicateTrackAlias:
                log_msg << " duplicate track alias: " << reason_str;
                break;
            case messages::TerminationReason::kKeyValueFormattingError:
                log_msg << " key_value formatting mismatch: " << reason_str;
                break;
            case messages::TerminationReason::kGoawayTimeout:
                log_msg << " goaway timeout: " << reason_str;
                break;
            default:
                log_msg << " termination reason (" << reason_str << ")";
                break;
        }

        SPDLOG_LOGGER_INFO(logger_, log_msg.str());

        quic_transport_->Close(conn_id, static_cast<uint64_t>(reason));

        if (client_mode_) {
            SPDLOG_LOGGER_INFO(logger_, "Client connection closed, stopping client");
            stop_ = true;
        }
    }

    std::shared_ptr<Transport> Transport::GetSharedPtr()
    {
        if (!weak_from_this().lock()) {
            throw std::runtime_error("Transport is not shared_ptr");
        }

        return shared_from_this();
    }

    Transport::ConnectionContext& Transport::GetConnectionContext(ConnectionHandle conn)
    {
        return connections_.at(conn);
    }

    void Transport::SetWebTransportMode(ConnectionHandle conn_id, bool is_webtransport)
    {
        std::lock_guard<std::mutex> _(state_mutex_);
        auto conn_it = connections_.find(conn_id);
        if (conn_it != connections_.end()) {
            conn_it->second.is_webtransport = is_webtransport;
        }
    }

    std::uint64_t Transport::CreateStream(ConnectionHandle conn_id, std::uint64_t data_ctx_id, uint8_t priority)
    {
        return quic_transport_->CreateStream(conn_id, data_ctx_id, priority);
    }

    TransportError Transport::Enqueue(const TransportConnId& conn_id,
                                      const DataContextId& data_ctx_id,
                                      std::uint64_t stream_id,
                                      std::shared_ptr<const std::vector<uint8_t>> bytes,
                                      const uint8_t priority,
                                      const uint32_t ttl_ms,
                                      const uint32_t delay_ms,
                                      const ITransport::EnqueueFlags flags)
    {
        return quic_transport_->Enqueue(
          conn_id, data_ctx_id, stream_id, std::move(bytes), priority, ttl_ms, delay_ms, flags);
    }

    // -- Lifecycle --

    Transport::Status Transport::Start()
    {
        return StartTransport().status;
    }

    void Transport::Stop()
    {
        stop_ = true;
        StopTransport();
    }

    // -- Resolve Methods --

    void Transport::ResolveSubscribe(ConnectionHandle connection_handle,
                                     uint64_t request_id,
                                     uint64_t track_alias,
                                     const RequestResponse& subscribe_response)
    {
        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        if (client_mode_) {
            switch (subscribe_response.reason_code) {
                case RequestResponse::ReasonCode::kOk:
                    SendSubscribeOk(conn_it->second,
                                    request_id,
                                    track_alias,
                                    kSubscribeExpires,
                                    subscribe_response.largest_location,
                                    subscribe_response.publisher_default_group_order);
                    break;
                default:
                    SendRequestError(
                      conn_it->second, request_id, messages::ErrorCode::kInternalError, 0ms, "Internal error");
                    break;
            }
            return;
        }

        switch (subscribe_response.reason_code) {
            case RequestResponse::ReasonCode::kOk: {
                // Save the latest state for joining fetch.
                auto req_it = conn_it->second.recv_req_id.find(request_id);
                if (req_it == conn_it->second.recv_req_id.end()) {
                    SPDLOG_LOGGER_WARN(logger_,
                                       "Resolve subscribe has no request_id: {} conn_id: {} track_alias: {}",
                                       request_id,
                                       connection_handle,
                                       track_alias);
                    break;
                }

                req_it->second.largest_location = subscribe_response.largest_location;

                if (!subscribe_response.is_publisher_initiated) {
                    // Send the ok.
                    SendSubscribeOk(conn_it->second,
                                    request_id,
                                    track_alias,
                                    kSubscribeExpires,
                                    subscribe_response.largest_location,
                                    subscribe_response.publisher_default_group_order);
                }
                break;
            }
            default:
                if (!subscribe_response.is_publisher_initiated) {
                    SendRequestError(
                      conn_it->second, request_id, messages::ErrorCode::kInternalError, 0ms, "Internal error");
                }
                break;
        }
    }

    void Transport::ResolveSubscribeNamespace(ConnectionHandle connection_handle,
                                              DataContextId data_ctx_id,
                                              uint64_t request_id,
                                              const TrackNamespace& prefix,
                                              const SubscribeNamespaceResponse& response)
    {
        const auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        auto th = TrackHash({ prefix, {} });

        if (response.reason_code != SubscribeNamespaceResponse::ReasonCode::kOk) {
            SendRequestError(conn_it->second, request_id, messages::ErrorCode::kInternalError, 0ms, "Internal error");
            return;
        }

        SendRequestOk(conn_it->second, request_id);

        // Fan out NAMESPACE for matching namespaces.
        for (const auto& name_space : response.namespaces) {
            const auto match = prefix.IsPrefixOf(name_space);
            if (match == std::partial_ordering::unordered || match == std::partial_ordering::greater) {
                SPDLOG_LOGGER_WARN(logger_, "Dropping non prefix match");
                continue;
            }

            const auto prefix_size = prefix.GetEntries().size();
            const auto namespace_size = name_space.GetEntries().size();
            auto suffix =
              namespace_size == prefix_size ? TrackNamespace{} : name_space.GetSuffix(namespace_size - prefix_size);
            SendNamespace(conn_it->second, data_ctx_id, suffix);
        }
    }

    void Transport::ResolveSubscribeTracks(ConnectionHandle connection_handle,
                                           DataContextId,
                                           uint64_t request_id,
                                           const TrackNamespace& prefix,
                                           const SubscribeNamespaceResponse& response)
    {
        const auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        if (response.reason_code != SubscribeNamespaceResponse::ReasonCode::kOk) {
            SendRequestError(conn_it->second, request_id, messages::ErrorCode::kInternalError, 0ms, "Internal error");
            return;
        }

        SendRequestOk(conn_it->second, request_id);

        // Fan out PUBLISH_NAMESPACE for matching namespaces.
        for (const auto& name_space : response.namespaces) {
            const auto match = prefix.IsPrefixOf(name_space);
            if (match == std::partial_ordering::unordered || match == std::partial_ordering::less) {
                SPDLOG_LOGGER_WARN(logger_, "Dropping non prefix match");
                continue;
            }

            auto request_id = conn_it->second.GetNextRequestId();
            SendPublishNamespace(conn_it->second, request_id, name_space);
        }
    }

    void Transport::ResolveFetch(ConnectionHandle connection_handle,
                                 uint64_t request_id,
                                 std::uint8_t priority,
                                 std::optional<messages::GroupOrder> group_order,
                                 const FetchResponse& response)
    {
        auto error_code = messages::ErrorCode::kInternalError;

        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        switch (response.reason_code) {
            case FetchResponse::ReasonCode::kOk:
                SendFetchOk(conn_it->second,
                            request_id,
                            response.publisher_default_group_order,
                            priority,
                            response.largest_location.value());
                return;

            case FetchResponse::ReasonCode::kInvalidRange:
                error_code = messages::ErrorCode::kInvalidRange;
                break;

            default:
                break;
        }

        SendRequestError(conn_it->second,
                         request_id,
                         error_code,
                         0ms,
                         response.error_reason.has_value() ? response.error_reason.value() : "Internal error");
    }

    void Transport::ResolvePublishNamespace(ConnectionHandle connection_handle,
                                            uint64_t request_id,
                                            const TrackNamespace& track_namespace,
                                            const std::vector<ConnectionHandle>& subscribers,
                                            const PublishNamespaceResponse& response)
    {
        auto th = TrackHash({ track_namespace, {} });
        auto fanout_subscribe_namespace_requestors = [&] {
            for (const auto& sub_conn_handle : subscribers) {
                auto it = connections_.find(sub_conn_handle);
                if (it == connections_.end()) {
                    continue;
                }

                auto next_request_id = it->second.GetNextRequestId();
                SendPublishNamespace(it->second, next_request_id, track_namespace);
                // it->second.tracks_by_request_id[request_id] = next_request_id;
            }
        };

        if (!connection_handle) {
            fanout_subscribe_namespace_requestors();
            return;
        }

        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        switch (response.reason_code) {
            case PublishNamespaceResponse::ReasonCode::kOk: {
                SendRequestOk(conn_it->second, request_id);

                fanout_subscribe_namespace_requestors();
                break;
            }
            default: {
                // TODO: Send announce error
            }
        }
    }

    void Transport::ResolvePublishNamespaceDone(ConnectionHandle connection_handle,
                                                messages::RequestID request_id,
                                                const std::vector<ConnectionHandle>& subscribers)
    {
        for (const auto& sub_conn_handle : subscribers) {
            auto it = connections_.find(sub_conn_handle);
            if (it == connections_.end()) {
                continue;
            }

            auto req_it = it->second.request_handlers.find(request_id);
            if (req_it != it->second.request_handlers.end()) {
                SendPublishNamespaceDone(it->second, request_id);
                it->second.request_handlers.erase(req_it);
            }
        }
    }

    void Transport::ResolveRequestUpdate(ConnectionHandle connection_handle,
                                         uint64_t request_id,
                                         uint64_t existing_request_id,
                                         const messages::Parameters& params)
    {
        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        auto track_it = conn_it->second.request_handlers.find(existing_request_id);
        if (track_it == conn_it->second.request_handlers.end()) {
            if (client_mode_) {
                SendRequestError(conn_it->second,
                                 request_id,
                                 messages::ErrorCode::kDoesNotExist,
                                 0ms,
                                 "Found no track for existing request ID");
            }
            return;
        }

        SPDLOG_LOGGER_DEBUG(
          logger_, "Request Updated resolve req_id: {} existing_id: {}", request_id, existing_request_id);

        track_it->second.handler->RequestUpdate(request_id, params);

        SendRequestOk(conn_it->second, request_id);
    }

    // -- Client Callbacks --

    void Transport::ServerSetupReceived(const ServerSetupAttributes& server_setup_attributes) {}

    void Transport::PublishNamespaceStatusChanged(messages::RequestID request_id, const PublishNamespaceStatus status)
    {
    }

    void Transport::PublishNamespaceReceived(const TrackNamespace& track_namespace,
                                             const PublishNamespaceAttributes& publish_namespace_attributes)
    {
    }

    void Transport::NamespaceReceived(const TrackNamespace&) {}

    void Transport::PublishNamespaceDoneReceived(messages::RequestID request_id) {}

    void Transport::UnpublishedSubscribeReceived(const FullTrackName&, const messages::SubscribeAttributes&) {}

    void Transport::MetricsSampled(const ConnectionMetrics&) {}

    // -- Server Callbacks --

    void Transport::NewConnectionAccepted(ConnectionHandle connection_handle, const ConnectionRemoteInfo& remote)
    {
        SPDLOG_LOGGER_DEBUG(
          logger_, "New connection conn_id: {} remote ip: {} port: {}", connection_handle, remote.ip, remote.port);
    }

    void Transport::ConnectionStatusChanged(ConnectionHandle connection_handle, ConnectionStatus status) {}

    void Transport::MetricsSampled(ConnectionHandle, const ConnectionMetrics&) {}

    void Transport::ClientSetupReceived(ConnectionHandle, const ClientSetupAttributes&) {}

    void Transport::PublishNamespaceReceived(ConnectionHandle connection_handle,
                                             const TrackNamespace& track_namespace,
                                             const PublishNamespaceAttributes& publish_announce_attributes)
    {
    }

    std::vector<ConnectionHandle> Transport::PublishNamespaceDoneReceived(ConnectionHandle connection_handle,
                                                                          messages::RequestID request_id)
    {
        return std::vector<ConnectionHandle>();
    }

    void Transport::UnsubscribeNamespaceReceived(ConnectionHandle, const TrackNamespace&) {}

    void Transport::SubscribeNamespaceReceived(ConnectionHandle,
                                               DataContextId,
                                               const TrackNamespace&,
                                               const messages::SubscribeNamespaceAttributes&)
    {
    }

    void Transport::SubscribeTracksReceived(ConnectionHandle,
                                            DataContextId,
                                            const TrackNamespace&,
                                            const messages::SubscribeNamespaceAttributes&)
    {
    }

    void Transport::SubscribeReceived(ConnectionHandle connection_handle,
                                      uint64_t request_id,
                                      const FullTrackName& track_full_name,
                                      const messages::SubscribeAttributes& subscribe_attributes)
    {
    }

    void Transport::UnsubscribeReceived(ConnectionHandle, uint64_t) {}

    void Transport::PublishDoneReceived(ConnectionHandle, uint64_t) {}

    void Transport::NewGroupRequested(const FullTrackName&, messages::GroupId) {}

    // -- Shared Callbacks --

    void Transport::PublishReceived(ConnectionHandle connection_handle,
                                    uint64_t request_id,
                                    const messages::PublishAttributes& publish_attributes,
                                    std::weak_ptr<SubscribeNamespaceHandler> sub_ns_handler)
    {
        if (!client_mode_) {
            return;
        }

        auto handler = SubscribeTrackHandler::Create(publish_attributes.track_full_name, publish_attributes.priority);

        ResolvePublish(connection_handle,
                       request_id,
                       publish_attributes,
                       { .reason_code = PublishResponse::ReasonCode::kNotSupported },
                       handler);
    }

    void Transport::FetchCancelReceived(ConnectionHandle connection_handle, uint64_t request_id) {}

    void Transport::RequestUpdateReceived(ConnectionHandle connection_handle,
                                          uint64_t request_id,
                                          uint64_t existing_request_id,
                                          const messages::Parameters& params)
    {
        if (client_mode_) {
            ResolveRequestUpdate(connection_handle, request_id, existing_request_id, params);
        }
    }

    // -- Server Relay Methods --

    void Transport::BindPublisherTrack(ConnectionHandle connection_handle,
                                       ConnectionHandle src_id,
                                       uint64_t request_id,
                                       const std::shared_ptr<PublishTrackHandler>& track_handler,
                                       bool ephemeral)
    {
        // Generate track alias
        const auto& tfn = track_handler->GetFullTrackName();
        auto th = TrackHash(tfn);

        std::unique_lock<std::mutex> lock(state_mutex_);
        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Subscribe track conn_id: {} does not exist.", connection_handle);
            return;
        }

        if (!track_handler->GetTrackAlias().has_value()) {
            track_handler->SetTrackAlias(th.track_fullname_hash);
        }

        track_handler->SetRequestId(request_id);
        conn_it->second.request_handlers[request_id] = track_handler;

        track_handler->connection_handle_ = connection_handle;

        track_handler->publish_data_ctx_id_ =
          quic_transport_->CreateDataContext(connection_handle,
                                             track_handler->default_track_mode_ == TrackMode::kDatagram ? false : true,
                                             track_handler->default_priority_,
                                             false);

        // Set this transport as the one for the publisher to use.
        track_handler->SetTransport(GetSharedPtr());

        if (!ephemeral) {
            // Hold onto track handler
            conn_it->second.pub_tracks_by_name[th.track_namespace_hash][th.track_name_hash] = track_handler;
            conn_it->second.pub_tracks_by_track_alias[th.track_fullname_hash][src_id] = track_handler;
            conn_it->second.pub_tracks_by_data_ctx_id[track_handler->publish_data_ctx_id_] = track_handler;
        }

        lock.unlock();
        track_handler->SetStatus(PublishTrackHandler::Status::kOk);
    }

    void Transport::UnbindPublisherTrack(ConnectionHandle connection_handle,
                                         ConnectionHandle src_id,
                                         const std::shared_ptr<PublishTrackHandler>& track_handler,
                                         bool send_publish_done)
    {
        std::lock_guard lock(state_mutex_);

        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }
        auto th = TrackHash(track_handler->GetFullTrackName());
        SPDLOG_LOGGER_DEBUG(
          logger_,
          "Server publish track conn_id: {} full_name_hash: {} namespace_hash: {} name_hash: {} unbind",
          connection_handle,
          th.track_fullname_hash,
          th.track_namespace_hash,
          th.track_name_hash);

        conn_it->second.request_handlers.erase(*track_handler->GetRequestId());
        conn_it->second.pub_tracks_by_name[th.track_namespace_hash].erase(th.track_name_hash);

        conn_it->second.pub_tracks_by_track_alias[th.track_fullname_hash].erase(src_id);
        if (conn_it->second.pub_tracks_by_track_alias[th.track_fullname_hash].empty()) {
            conn_it->second.pub_tracks_by_track_alias.erase(th.track_fullname_hash);
        }

        if (conn_it->second.pub_tracks_by_name.count(th.track_namespace_hash) == 0) {
            SPDLOG_LOGGER_DEBUG(logger_,
                                "Server publish track conn_id: {} full_name_hash: {} namespace_hash: {} unbind",
                                connection_handle,
                                th.track_fullname_hash,
                                th.track_namespace_hash);

            conn_it->second.pub_tracks_by_name.erase(th.track_namespace_hash);
        }

        conn_it->second.pub_tracks_by_data_ctx_id.erase(track_handler->publish_data_ctx_id_);

        quic_transport_->DeleteDataContext(connection_handle, track_handler->publish_data_ctx_id_);

        if (send_publish_done) {
            SendPublishDone(conn_it->second,
                            track_handler->GetRequestId().value(),
                            messages::PublishDoneStatusCode::kSubscribtionEnded,
                            "No publishers");
        }
    }

    void Transport::BindFetchTrack(TransportConnId conn_id, std::shared_ptr<PublishFetchHandler> track_handler)
    {
        const std::uint64_t request_id = *track_handler->GetRequestId();
        SPDLOG_LOGGER_INFO(logger_, "Publish fetch track conn_id: {} subscribe: {}", conn_id, request_id);

        std::lock_guard lock(state_mutex_);

        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Publish fetch track conn_id: {} does not exist.", conn_id);
            return;
        }

        track_handler->SetStatus(PublishFetchHandler::Status::kOk);
        track_handler->connection_handle_ = conn_id;
        track_handler->publish_data_ctx_id_ =
          quic_transport_->CreateDataContext(conn_id, true, track_handler->GetDefaultPriority(), false);

        track_handler->SetTransport(GetSharedPtr());

        // Hold ref to track handler
        conn_it->second.pub_fetch_tracks_by_request_id[request_id] = track_handler;
    }

    void Transport::UnbindFetchTrack(ConnectionHandle connection_handle,
                                     const std::shared_ptr<PublishFetchHandler>& track_handler)
    {
        std::lock_guard lock(state_mutex_);

        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }
        auto request_id = *track_handler->GetRequestId();
        SPDLOG_LOGGER_DEBUG(
          logger_, "Server publish fetch track conn_id: {} subscribe id: {} unbind", connection_handle, request_id);

        conn_it->second.pub_fetch_tracks_by_request_id.erase(request_id);
        quic_transport_->DeleteDataContext(connection_handle, track_handler->publish_data_ctx_id_, true);
    }

    // -- Private --

    bool Transport::ProcessCtrlMessage(ConnectionContext& conn_ctx,
                                       uint64_t data_ctx_id,
                                       messages::ControlMessageType msg_type,
                                       BytesSpan msg_bytes)
    try {
        switch (msg_type) {
            case messages::ControlMessageType::kSubscribe: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto track_namespace = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                const auto track_name = messages::Message::ParseField<Bytes>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                auto tfn = FullTrackName{ track_namespace, track_name };
                auto th = TrackHash(tfn);
                conn_ctx.recv_req_id[request_id] = { .track_full_name = tfn, .track_hash = th };

                if (client_mode_) {
                    auto ptd = GetPubTrackHandler(conn_ctx, th);
                    if (ptd == nullptr) {
                        SPDLOG_LOGGER_WARN(logger_,
                                           "Received subscribe unknown publish track conn_id: {} namespace hash: {} "
                                           "name hash: {} request_id: {}",
                                           conn_ctx.connection_handle,
                                           th.track_namespace_hash,
                                           th.track_name_hash,
                                           request_id);

                        SendRequestError(
                          conn_ctx, request_id, messages::ErrorCode::kDoesNotExist, 0ms, "Published track not found");
                        return true;
                    }

                    ResolveSubscribe(conn_ctx.connection_handle,
                                     request_id,
                                     ptd->GetTrackAlias().value(),
                                     { RequestResponse::ReasonCode::kOk });

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
                if (parameters.Contains(messages::ParameterType::kLocationFilter)) {
                    filter = parameters.GetFilter(messages::FilterType::kLocationFilter);
                } else if (parameters.Contains(messages::ParameterType::kTrackFilter)) {
                    filter = parameters.GetFilter(messages::FilterType::kTrackFilter);
                }

                SubscribeReceived(conn_ctx.connection_handle,
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
                                  });

                if (new_group_request_id.has_value()) {
                    NewGroupRequested(tfn, *new_group_request_id);
                }

                return true;
            }
            case messages::ControlMessageType::kSubscribeOk: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto track_alias = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);
                const auto track_extensions = messages::Message::ParseField<messages::TrackExtensions>(msg_bytes);

                auto sub_it = conn_ctx.request_handlers.find(request_id);
                if (sub_it == conn_ctx.request_handlers.end()) {
                    SPDLOG_LOGGER_WARN(
                      logger_,
                      "Received subscribe ok to unknown subscribe track conn_id: {} request_id: {}, ignored",
                      conn_ctx.connection_handle,
                      request_id);
                    return true;
                }

                if (auto sub_handler = sub_it->second.Get<SubscribeTrackHandler>()) {
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
                    conn_ctx.sub_by_recv_track_alias[track_alias] = sub_handler;
                }

                return true;
            }
            case messages::ControlMessageType::kRequestOk: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                if (client_mode_) {
                    auto track_it = conn_ctx.request_handlers.find(request_id);
                    if (track_it == conn_ctx.request_handlers.end()) {
                        SPDLOG_LOGGER_WARN(logger_,
                                           "Received REQUEST_OK to unknown track conn_id: {} request_id: {}, ignored",
                                           conn_ctx.connection_handle,
                                           request_id);
                        return true;
                    }

                    track_it->second.handler->RequestOk(request_id, parameters);
                    RequestOkReceived(conn_ctx.connection_handle, request_id);
                    return true;
                }

                auto largest_location =
                  parameters.GetOptional<messages::Location>(messages::ParameterType::kLargestObject);
                RequestOkReceived(conn_ctx.connection_handle, request_id, largest_location);
                return true;
            }
            case messages::ControlMessageType::kRequestError: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto error_code = messages::Message::ParseField<messages::ErrorCode>(msg_bytes);
                [[maybe_unused]] const auto retry_interval = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto error_reason = messages::Message::ParseField<Bytes>(msg_bytes);

                RequestResponse response{};
                response.reason_code = RequestResponse::FromErrorCode(error_code);
                response.error_reason = std::string(error_reason.begin(), error_reason.end());

                if (client_mode_) {
                    auto track_it = conn_ctx.request_handlers.find(request_id);
                    if (track_it == conn_ctx.request_handlers.end()) {
                        SPDLOG_LOGGER_WARN(
                          logger_,
                          "Received REQUEST_ERROR to unknown track conn_id: {} request_id: {}, ignored",
                          conn_ctx.connection_handle,
                          request_id);
                        return true;
                    }

                    track_it->second.handler->RequestError(error_code, response.error_reason.value());
                    RequestErrorReceived(conn_ctx.connection_handle, request_id, response);
                    return true;
                }

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received track status error request_id: {} error code: {} reason: {}",
                                    request_id,
                                    static_cast<std::uint64_t>(error_code),
                                    response.error_reason.value());
                RequestErrorReceived(conn_ctx.connection_handle, request_id, response);
                return true;
            }
            case messages::ControlMessageType::kTrackStatus: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto track_namespace = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                const auto track_name = messages::Message::ParseField<Bytes>(msg_bytes);

                auto tfn = FullTrackName{ track_namespace, track_name };

                auto th = TrackHash(tfn);
                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received track status request_id: {} for full name hash: {}",
                                    request_id,
                                    th.track_fullname_hash);

                TrackStatusReceived(conn_ctx.connection_handle, request_id, tfn);
                return true;
            }
            case messages::ControlMessageType::kPublishNamespace: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto track_namespace = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                [[maybe_unused]] const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                if (client_mode_) {
                    PublishNamespaceReceived(track_namespace, { .request_id = request_id });
                } else {
                    PublishNamespaceReceived(conn_ctx.connection_handle, track_namespace, { request_id });
                }
                return true;
            }
            case messages::ControlMessageType::kSubscribeTracks: {
                if (client_mode_) {
                    SPDLOG_LOGGER_ERROR(
                      logger_, "Unsupported MOQT message type: {}, bad stream", static_cast<uint64_t>(msg_type));
                    return false;
                }

                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto track_namespace_prefix = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                messages::Filter filter;
                if (parameters.Contains(messages::ParameterType::kLocationFilter)) {
                    filter = parameters.GetFilter(messages::FilterType::kLocationFilter);
                } else if (parameters.Contains(messages::ParameterType::kTrackFilter)) {
                    filter = parameters.GetFilter(messages::FilterType::kTrackFilter);
                }

                SubscribeTracksReceived(
                  conn_ctx.connection_handle,
                  data_ctx_id,
                  track_namespace_prefix,
                  { .request_id = request_id, .filter_type = messages::FilterType::kTrackFilter, .filter = filter });
                return true;
            }
            case messages::ControlMessageType::kSubscribeNamespace: {
                if (client_mode_) {
                    SPDLOG_LOGGER_ERROR(
                      logger_, "Unsupported MOQT message type: {}, bad stream", static_cast<uint64_t>(msg_type));
                    return false;
                }

                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto track_namespace_prefix = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                [[maybe_unused]] const auto subscribe_options =
                  messages::Message::ParseField<messages::SubscribeOptions>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                messages::Filter filter;
                if (parameters.Contains(messages::ParameterType::kLocationFilter)) {
                    filter = parameters.GetFilter(messages::FilterType::kLocationFilter);
                } else if (parameters.Contains(messages::ParameterType::kTrackFilter)) {
                    filter = parameters.GetFilter(messages::FilterType::kTrackFilter);
                }

                SubscribeNamespaceReceived(
                  conn_ctx.connection_handle,
                  data_ctx_id,
                  track_namespace_prefix,
                  { .request_id = request_id, .filter_type = messages::FilterType::kTrackFilter, .filter = filter });
                return true;
            }
            case messages::ControlMessageType::kNamespace: {
                if (!client_mode_) {
                    SPDLOG_LOGGER_ERROR(
                      logger_, "Unsupported MOQT message type: {}, bad stream", static_cast<uint64_t>(msg_type));
                    return false;
                }

                const auto track_namespace_suffix = messages::Message::ParseField<TrackNamespace>(msg_bytes);

                for (auto& [_, track] : conn_ctx.request_handlers) {
                    if (auto h = track.Get<SubscribeNamespaceHandler>();
                        h && h->GetMode() == SubscribeNamespaceHandler::Mode::kNamespaces &&
                        h->data_ctx_id_ == data_ctx_id) {
                        std::vector<Bytes> entries;
                        entries.reserve(h->GetPrefix().GetEntries().size() +
                                        track_namespace_suffix.GetEntries().size());

                        for (const auto& entry : h->GetPrefix().GetEntries()) {
                            entries.emplace_back(entry.begin(), entry.end());
                        }

                        for (const auto& entry : track_namespace_suffix.GetEntries()) {
                            entries.emplace_back(entry.begin(), entry.end());
                        }

                        NamespaceReceived(TrackNamespace{ entries });
                        return true;
                    }
                }

                SPDLOG_LOGGER_WARN(logger_,
                                   "Received NAMESPACE to unknown subscribe namespace conn_id: {} data_ctx_id: {}",
                                   conn_ctx.connection_handle,
                                   data_ctx_id);
                return true;
            }
            case messages::ControlMessageType::kNamespaceDone: {
                if (client_mode_) {
                    SPDLOG_LOGGER_ERROR(
                      logger_, "Unsupported MOQT message type: {}, bad stream", static_cast<uint64_t>(msg_type));
                    return false;
                }

                const auto track_namespace_suffix = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                UnsubscribeNamespaceReceived(conn_ctx.connection_handle, track_namespace_suffix);
                return true;
            }
            case messages::ControlMessageType::kPublishNamespaceDone: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);

                if (client_mode_) {
                    PublishNamespaceDoneReceived(request_id);
                    return true;
                }

                SPDLOG_LOGGER_INFO(logger_, "Received publish namespace done for request_id: {}", request_id);

                auto sub_namespace_conns = PublishNamespaceDoneReceived(conn_ctx.connection_handle, request_id);

                std::lock_guard<std::mutex> _(state_mutex_);
                for (auto conn_id : sub_namespace_conns) {
                    auto conn_it = connections_.find(conn_id);
                    if (conn_it == connections_.end()) {
                        continue;
                    }

                    SendPublishNamespaceDone(conn_it->second, request_id);
                }

                return true;
            }
            case messages::ControlMessageType::kUnsubscribe: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);

                if (client_mode_) {
                    const auto th_it = conn_ctx.recv_req_id.find(request_id);
                    if (th_it == conn_ctx.recv_req_id.end()) {
                        SPDLOG_LOGGER_WARN(
                          logger_,
                          "Received unsubscribe to unknown request_id conn_id: {} request_id: {}, ignored",
                          conn_ctx.connection_handle,
                          request_id);
                        return true;
                    }

                    const auto th = TrackHash(th_it->second.track_full_name);
                    const auto pub_track_ns_it = conn_ctx.pub_tracks_by_name.find(th.track_namespace_hash);
                    if (pub_track_ns_it != conn_ctx.pub_tracks_by_name.end()) {
                        const auto pub_track_n_it = pub_track_ns_it->second.find(th.track_name_hash);
                        if (pub_track_n_it != pub_track_ns_it->second.end()) {
                            pub_track_n_it->second->SetStatus(PublishTrackHandler::Status::kNoSubscribers);
                        }
                    }
                    return true;
                }

                auto& th = conn_ctx.recv_req_id[request_id].track_hash;
                if (auto pdt = GetPubTrackHandler(conn_ctx, th)) {
                    pdt->SetStatus(PublishTrackHandler::Status::kNoSubscribers);
                }

                UnsubscribeReceived(conn_ctx.connection_handle, request_id);
                conn_ctx.recv_req_id.erase(request_id);
                return true;
            }
            case messages::ControlMessageType::kPublishDone: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                [[maybe_unused]] const auto status_code =
                  messages::Message::ParseField<messages::PublishDoneStatusCode>(msg_bytes);
                [[maybe_unused]] const auto stream_count = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                [[maybe_unused]] const auto error_reason = messages::Message::ParseField<Bytes>(msg_bytes);

                auto sub_it = conn_ctx.request_handlers.find(request_id);
                if (sub_it == conn_ctx.request_handlers.end()) {
                    SPDLOG_LOGGER_WARN(logger_,
                                       "Received publish done to unknown request_id conn_id: {} request_id: {}",
                                       conn_ctx.connection_handle,
                                       request_id);
                    return true;
                }

                if (client_mode_) {
                    if (auto h = sub_it->second.Get<SubscribeTrackHandler>()) {
                        h->SetStatus(SubscribeTrackHandler::Status::kNotSubscribed);
                    }
                    return true;
                }

                auto tfn = sub_it->second.handler->GetFullTrackName();
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_INFO(logger_,
                                   "Received publish done conn_id: {} request_id: {} track namespace hash: {} "
                                   "name hash: {} track alias: {}",
                                   conn_ctx.connection_handle,
                                   request_id,
                                   th.track_namespace_hash,
                                   th.track_name_hash,
                                   th.track_fullname_hash);

                if (auto h = sub_it->second.Get<SubscribeTrackHandler>()) {
                    h->SetStatus(SubscribeTrackHandler::Status::kNotSubscribed);
                    PublishDoneReceived(conn_ctx.connection_handle, request_id);
                }

                conn_ctx.recv_req_id.erase(request_id);
                return true;
            }
            case messages::ControlMessageType::kRequestsBlocked: {
                const auto maximum_request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                SPDLOG_LOGGER_WARN(logger_, "Subscribe was blocked, maximum_request_id: {}", maximum_request_id);
                return true;
            }
            case messages::ControlMessageType::kPublishNamespaceCancel: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto error_code = messages::Message::ParseField<messages::ErrorCode>(msg_bytes);
                const auto error_reason = messages::Message::ParseField<Bytes>(msg_bytes);

                if (client_mode_) {
                    PublishNamespaceStatusChanged(request_id, PublishNamespaceStatus::kNotPublished);
                    return true;
                }

                std::string reason(error_reason.begin(), error_reason.end());
                SPDLOG_LOGGER_INFO(logger_,
                                   "Received publish namespace cancel for request_id: {} (error_code={}, reason={})",
                                   request_id,
                                   static_cast<int>(error_code),
                                   reason);
                return true;
            }
            case messages::ControlMessageType::kGoaway: {
                const auto new_session_uri = messages::Message::ParseField<Bytes>(msg_bytes);
                std::string new_sess_uri(new_session_uri.begin(), new_session_uri.end());
                SPDLOG_LOGGER_INFO(logger_, "Received goaway new session uri: {}", new_sess_uri);
                return true;
            }
            case messages::ControlMessageType::kServerSetup: {
                if (!client_mode_) {
                    SPDLOG_LOGGER_ERROR(
                      logger_, "Unsupported MOQT message type: {}, bad stream", static_cast<uint64_t>(msg_type));
                    return false;
                }

                const auto setup_parameters = messages::Message::ParseField<messages::SetupParameters>(msg_bytes);

                std::string endpoint_id = "Unknown Endpoint ID";
                for (const auto& param : setup_parameters) {
                    if (param.type == messages::SetupParameterType::kEndpointId) {
                        endpoint_id = std::string(param.value.begin(), param.value.end());
                        break;
                    }
                }

                ServerSetupReceived({ 0, endpoint_id });
                SetStatus(Status::kReady);

                SPDLOG_LOGGER_INFO(
                  logger_, "Server setup received conn_id: {} from: {}", conn_ctx.connection_handle, endpoint_id);

                conn_ctx.setup_complete = true;
                return true;
            }
            case messages::ControlMessageType::kClientSetup: {
                if (client_mode_) {
                    SPDLOG_LOGGER_ERROR(
                      logger_, "Unsupported MOQT message type: {}, bad stream", static_cast<uint64_t>(msg_type));
                    return false;
                }

                const auto setup_parameters = messages::Message::ParseField<messages::SetupParameters>(msg_bytes);

                std::string endpoint_id = "Unknown Endpoint ID";
                for (const auto& param : setup_parameters) {
                    if (param.type == messages::SetupParameterType::kEndpointId) {
                        endpoint_id = std::string(param.value.begin(), param.value.end());
                    }
                }

                ClientSetupReceived(conn_ctx.connection_handle, { endpoint_id });

                SPDLOG_LOGGER_INFO(
                  logger_, "Client setup received conn_id: {} from: {}", conn_ctx.connection_handle, endpoint_id);

                SendServerSetup(conn_ctx);
                conn_ctx.setup_complete = true;
                return true;
            }
            case messages::ControlMessageType::kFetchOk: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                [[maybe_unused]] const auto end_of_track = messages::Message::ParseField<std::uint8_t>(msg_bytes);
                const auto end_location = messages::Message::ParseField<messages::Location>(msg_bytes);
                [[maybe_unused]] const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);
                const auto track_extensions = messages::Message::ParseField<messages::TrackExtensions>(msg_bytes);

                auto fetch_it = conn_ctx.request_handlers.find(request_id);
                if (fetch_it == conn_ctx.request_handlers.end()) {
                    SPDLOG_LOGGER_WARN(logger_,
                                       "Received fetch ok for unknown fetch track conn_id: {} request_id: {}, ignored",
                                       conn_ctx.connection_handle,
                                       request_id);
                    return true;
                }

                if (auto h = fetch_it->second.Get<FetchTrackHandler>()) {
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

                        messages::StandaloneFetchAttributes attrs = {
                            .priority = priority,
                            .group_order = group_order,
                            .publisher_default_group_order = messages::GroupOrder::kAscending,
                            .start_location = start,
                            .end_location = end_location,
                        };
                        StandaloneFetchReceived(conn_ctx.connection_handle, request_id, tfn, attrs);
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

                        const auto subscribe_state = conn_ctx.recv_req_id.find(joining_request_id);
                        if (subscribe_state == conn_ctx.recv_req_id.end()) {
                            SendRequestError(conn_ctx,
                                             request_id,
                                             messages::ErrorCode::kDoesNotExist,
                                             0ms,
                                             "Corresponding subscribe does not exist");
                            return true;
                        }

                        FullTrackName tfn = subscribe_state->second.track_full_name;

                        auto priority = parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority);
                        auto group_order =
                          parameters.GetOptional<messages::GroupOrder>(messages::ParameterType::kGroupOrder);

                        messages::JoiningFetchAttributes attrs = {
                            .priority = priority,
                            .group_order = group_order,
                            .publisher_default_group_order = messages::GroupOrder::kAscending,
                            .joining_request_id = joining_request_id,
                            .relative = relative_joining,
                            .joining_start = joining_start,
                        };

                        JoiningFetchReceived(conn_ctx.connection_handle, request_id, tfn, attrs);
                        return true;
                    }
                    default: {
                        SendRequestError(
                          conn_ctx, request_id, messages::ErrorCode::kNotSupported, 0ms, "Unknown fetch type");
                        return true;
                    }
                }
            }
            case messages::ControlMessageType::kFetchCancel: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);

                if (!client_mode_ && conn_ctx.recv_req_id.find(request_id) == conn_ctx.recv_req_id.end()) {
                    SPDLOG_LOGGER_WARN(logger_, "Received Fetch Cancel for unknown subscribe ID: {}", request_id);
                }

                const auto fetch_it = conn_ctx.request_handlers.find(request_id);
                if (fetch_it == conn_ctx.request_handlers.end()) {
                    FetchCancelReceived(conn_ctx.connection_handle, request_id);
                    return true;
                }

                if (auto h = fetch_it->second.Get<FetchTrackHandler>()) {
                    h->SetStatus(FetchTrackHandler::Status::kCancelled);
                }

                FetchCancelReceived(conn_ctx.connection_handle, request_id);

                conn_ctx.request_handlers.erase(fetch_it);
                conn_ctx.recv_req_id.erase(request_id);

                return true;
            }
            case messages::ControlMessageType::kPublish: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto track_namespace = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                const auto track_name = messages::Message::ParseField<Bytes>(msg_bytes);
                const auto track_alias = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);
                const auto track_extensions = messages::Message::ParseField<messages::TrackExtensions>(msg_bytes);

                auto tfn = FullTrackName{ track_namespace, track_name };
                auto th = TrackHash(tfn);
                conn_ctx.recv_req_id[request_id] = { .track_full_name = tfn, .track_hash = th };

                auto delivery_timeout = parameters.Get<std::uint64_t>(messages::ParameterType::kDeliveryTimeout);
                auto expires = parameters.Get<std::uint64_t>(messages::ParameterType::kExpires);
                auto forward = parameters.Get<bool>(messages::ParameterType::kForward);
                auto new_group_request_id =
                  parameters.GetOptional<std::uint64_t>(messages::ParameterType::kNewGroupRequest);
                const auto publisher_default_group_order =
                  track_extensions
                    .GetOptional<messages::GroupOrder>(messages::ExtensionType::kDefaultPublisherGroupOrder)
                    .value_or(messages::GroupOrder::kAscending);

                messages::PublishAttributes attrs;
                attrs.track_full_name = tfn;
                attrs.track_alias = track_alias;
                attrs.forward = forward;
                attrs.group_order = std::nullopt;
                attrs.publisher_default_group_order = publisher_default_group_order;
                attrs.delivery_timeout = std::chrono::milliseconds(delivery_timeout);
                attrs.expires = std::chrono::milliseconds(expires);
                attrs.is_publisher_initiated = true;
                attrs.new_group_request_id = new_group_request_id;

                if (client_mode_) {
                    auto dynamic_groups = track_extensions.GetOptional<bool>(messages::ExtensionType::kDynamicGroups);
                    attrs.dynamic_groups = dynamic_groups.value_or(false);

                    std::weak_ptr<SubscribeNamespaceHandler> sub_ns_handler;
                    for (auto& [_, track] : conn_ctx.request_handlers) {
                        if (auto h = track.Get<SubscribeNamespaceHandler>()) {
                            if (h->GetPrefix().HasSamePrefix(track_namespace)) {
                                sub_ns_handler = h;
                                break;
                            }
                        }
                    }

                    PublishReceived(conn_ctx.connection_handle, request_id, attrs, sub_ns_handler);
                } else {
                    attrs.priority = 0;
                    attrs.dynamic_groups = true; // TODO: read from extensions/properties

                    PublishReceived(conn_ctx.connection_handle, request_id, attrs, {});
                }

                return true;
            }
            case messages::ControlMessageType::kPublishOk: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                auto pub_it = conn_ctx.request_handlers.find(request_id);
                if (pub_it == conn_ctx.request_handlers.end()) {
                    SPDLOG_LOGGER_WARN(
                      logger_,
                      "Received publish ok to unknown publish track conn_id: {} request_id: {}, ignored",
                      conn_ctx.connection_handle,
                      request_id);
                    return true;
                }

                if (client_mode_) {
                    auto forward = parameters.Get<bool>(messages::ParameterType::kForward);
                    if (auto h = pub_it->second.Get<PublishTrackHandler>()) {
                        h->SetStatus(forward ? PublishTrackHandler::Status::kOk : PublishTrackHandler::Status::kPaused);
                    }
                    return true;
                }

                if (auto pub_h = pub_it->second.Get<PublishTrackHandler>()) {
                    auto th = TrackHash(pub_h->GetFullTrackName());
                    conn_ctx.recv_req_id[request_id] = { .track_full_name = pub_h->GetFullTrackName(),
                                                         .track_hash = th };

                    auto priority = parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority);
                    auto delivery_timeout =
                      parameters.GetOptional<std::uint64_t>(messages::ParameterType::kDeliveryTimeout);
                    auto forward = parameters.GetOptional<bool>(messages::ParameterType::kForward);
                    auto new_group_request_id =
                      parameters.GetOptional<std::uint64_t>(messages::ParameterType::kNewGroupRequest);

                    if (pub_h->GetDefaultPriority() < priority) {
                        pub_h->SetDefaultPriority(priority);
                    }

                    if (delivery_timeout.has_value() && *delivery_timeout) {
                        pub_h->SetDefaultTTL(*delivery_timeout);
                    }

                    if (forward.has_value()) {
                        pub_h->SetStatus(*forward ? PublishTrackHandler::Status::kOk
                                                  : PublishTrackHandler::Status::kPaused);
                    }

                    if (new_group_request_id.has_value()) {
                        pub_h->SetStatus(PublishTrackHandler::Status::kNewGroupRequested);
                    }
                }

                return true;
            }
            case messages::ControlMessageType::kRequestUpdate: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto existing_request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                if (client_mode_) {
                    auto track_it = conn_ctx.request_handlers.find(existing_request_id);
                    if (track_it == conn_ctx.request_handlers.end()) {
                        SPDLOG_LOGGER_WARN(logger_,
                                           "Received REQUEST_UPDATE to unknown track conn_id: {} request_id: {}, "
                                           "existing_request_id: {} ignored",
                                           conn_ctx.connection_handle,
                                           request_id,
                                           existing_request_id);
                        return true;
                    }

                    track_it->second.handler->RequestUpdate(request_id, parameters);
                    RequestUpdateReceived(conn_ctx.connection_handle, request_id, existing_request_id, parameters);
                    return true;
                }

                auto sub_ctx_it = conn_ctx.recv_req_id.find(existing_request_id);
                if (sub_ctx_it == conn_ctx.recv_req_id.end()) {
                    SPDLOG_LOGGER_WARN(logger_,
                                       "Received subscribe_update for unknown subscription conn_id: {} request_id: {}",
                                       conn_ctx.connection_handle,
                                       request_id);

                    SendRequestError(
                      conn_ctx, request_id, messages::ErrorCode::kDoesNotExist, 0ms, "Subscription not found");
                    return true;
                }

                [[maybe_unused]] auto delivery_timeout =
                  parameters.Get<std::uint64_t>(messages::ParameterType::kDeliveryTimeout);
                [[maybe_unused]] auto priority = parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority);
                auto forward = parameters.Get<bool>(messages::ParameterType::kForward);
                auto new_group_request_id =
                  parameters.GetOptional<std::uint64_t>(messages::ParameterType::kNewGroupRequest);

                if (new_group_request_id.has_value()) {
                    NewGroupRequested(sub_ctx_it->second.track_full_name, new_group_request_id.value());
                }

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received subscribe_update to recv request_id: {} forward: {} ngr: {}",
                                    request_id,
                                    forward,
                                    new_group_request_id.has_value());

                for (const auto& pub :
                     conn_ctx.pub_tracks_by_track_alias[sub_ctx_it->second.track_hash.track_fullname_hash]) {
                    if (not forward) {
                        pub.second->SetStatus(PublishTrackHandler::Status::kPaused);
                    } else {
                        pub.second->SetStatus(PublishTrackHandler::Status::kNewGroupRequested);
                    }
                }
                return true;
            }
            default: {
                SPDLOG_LOGGER_ERROR(
                  logger_, "Unsupported MOQT message type: {}, bad stream", static_cast<uint64_t>(msg_type));
                return false;
            }
        }

        return false;

    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_,
                            "Caught exception trying to process control message. (type={}, error={})",
                            static_cast<int>(msg_type),
                            e.what());
        CloseConnection(conn_ctx.connection_handle, messages::TerminationReason::kProtocolViolation, e.what());
        return false;
    } catch (...) {
        SPDLOG_LOGGER_ERROR(logger_, "Unable to parse control message");
        CloseConnection(conn_ctx.connection_handle,
                        messages::TerminationReason::kProtocolViolation,
                        "Control message cannot be parsed");
        return false;
    }

} // namespace quicr
