// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/detail/transport.h"

#include "quicr/detail/ctrl_messages.h"
#include "quicr/detail/messages.h"

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

    Transport::Transport(const ClientConfig& cfg, std::shared_ptr<TickService> tick_service)
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

    Transport::Transport(const ServerConfig& cfg, std::shared_ptr<TickService> tick_service)
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

    Transport::Status Transport::Start()
    {
        if (!weak_from_this().lock()) {
            throw std::runtime_error("Transport is not shared_ptr");
        }

        if (client_mode_) {
            TransportRemote relay;
            auto parse_result = ParseConnectUri(client_config_.connect_uri);
            if (!parse_result) {
                return Status::kInvalidParams;
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
                return status_;
            }

            SetConnectionHandle(conn_id);

            status_ = Status::kConnecting;
            StatusChanged(status_);

            SPDLOG_LOGGER_INFO(logger_, "Connecting session conn_id: {}...", conn_id);
            auto [conn_ctx, _] = connections_.try_emplace(conn_id, ConnectionContext{});
            conn_ctx->second.connection_handle = conn_id;

            return status_;
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

        return status_;
    }

    Transport::Status Transport::Stop()
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

    void Transport::RequestUpdateReceived(ConnectionHandle, uint64_t, uint64_t, const Parameters&) {}

    void Transport::ResolveRequestUpdate(ConnectionHandle, uint64_t, uint64_t, const Parameters&) {}

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

    void Transport::SendCtrlMsg(const ConnectionContext& conn_ctx, DataContextId data_ctx_id, BytesSpan data)
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
                                               std::make_shared<const std::vector<uint8_t>>(data.begin(), data.end()),
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

        Bytes buffer;
        buffer << ClientSetup(setup_parameters);

        auto& conn_ctx = connections_.begin()->second;

        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending ClientSetup (error={})", e.what());
        throw e;
    }

    void Transport::SendServerSetup(ConnectionContext& conn_ctx)
    try {
        auto setup_parameters = SetupParameters{}.Add(SetupParameterType::kEndpointId, server_config_.endpoint_id);

        Bytes buffer;
        buffer << ServerSetup(setup_parameters);

        SPDLOG_LOGGER_DEBUG(logger_, "Sending SERVER_SETUP to conn_id: {}", conn_ctx.connection_handle);

        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending ServerSetup (error={})", e.what());
        throw e;
    }

    void Transport::SendRequestOk(ConnectionContext& conn_ctx,
                                  messages::RequestID request_id,
                                  std::optional<Location> largest_location)
    try {
        auto params = Parameters{}.AddOptional(ParameterType::kLargestObject, largest_location);

        Bytes buffer;
        buffer << messages::RequestOk(request_id, params);

        SPDLOG_LOGGER_DEBUG(
          logger_, "Sending REQUEST_OK to conn_id: {} request_id: {}", conn_ctx.connection_handle, request_id);

        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), buffer);
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

        Bytes buffer;
        buffer << RequestUpdate(request_id, existing_request_id, params);

        SPDLOG_LOGGER_DEBUG(
          logger_,
          "Sending REQUEST_UPDATE to conn_id: {} request_id: {} existing_id: {} track namespace hash: {} name "
          "hash: {} forward: {}",
          conn_ctx.connection_handle,
          request_id,
          existing_request_id,
          th.track_namespace_hash,
          th.track_name_hash,
          forward);

        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), buffer);
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
        Bytes buffer;
        buffer << RequestError(request_id, error, retry_interval.count(), AsOwnedBytes(reason));

        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending REQUEST_ERROR to conn_id: {} request_id: {} error code: {} reason: {}",
                            conn_ctx.connection_handle,
                            request_id,
                            static_cast<int>(error),
                            reason);

        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending REQUEST_ERROR (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendPublishNamespace(ConnectionContext& conn_ctx,
                                         RequestID request_id,
                                         const TrackNamespace& track_namespace)
    try {
        auto publish_namespace = messages::PublishNamespace(request_id, track_namespace, {});

        Bytes buffer;
        buffer << publish_namespace;

        auto th = TrackHash({ track_namespace, {} });
        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending PublishNamespace to conn_id: {} request_id: {} namespace_hash: {}",
                            conn_ctx.connection_handle,
                            request_id,
                            th.track_namespace_hash);

        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending PublishNamespace (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendPublishNamespaceDone(ConnectionContext& conn_ctx, messages::RequestID request_id)
    try {
        auto publish_namespace_done = messages::PublishNamespaceDone(request_id);

        Bytes buffer;
        buffer << publish_namespace_done;

        SPDLOG_LOGGER_DEBUG(logger_, "Sending PUBLISH_NAMESPACE_DONE to conn_id: {}", conn_ctx.connection_handle);

        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending PUBLISH_NAMESPACE_DONE (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendTrackStatus(ConnectionContext& conn_ctx,
                                    messages::RequestID request_id,
                                    const FullTrackName& tfn)
    try {
        auto trackstatus = TrackStatus(request_id, tfn.name_space, tfn.name);

        Bytes buffer;
        buffer << trackstatus;

        SPDLOG_LOGGER_DEBUG(
          logger_, "Sending TRACK_STATUS to conn_id: {} request_id: {}", conn_ctx.connection_handle, request_id);

        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending Trac (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendSubscribe(ConnectionContext& conn_ctx,
                                  uint64_t request_id,
                                  const FullTrackName& tfn,
                                  TrackHash th, // TODO: This is only for a debug message, should be removed
                                  std::uint8_t priority,
                                  GroupOrder group_order,
                                  FilterType filter_type,
                                  std::optional<std::chrono::milliseconds> delivery_timeout)
    try {
        // TODO: Add support for these filter types.
        if (filter_type == FilterType::kAbsoluteStart || filter_type == FilterType::kAbsoluteRange) {
            throw std::runtime_error("Absolute filtering not yet supported for Subscribe");
        }

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
                        .Add(ParameterType::kGroupOrder, group_order)
                        .Add(ParameterType::kSubscriptionFilter, filter_type)
                        .Add(ParameterType::kForward, 1)
                        .AddOptional(ParameterType::kDeliveryTimeout, delivery_timeout);

        Bytes buffer;
        buffer << Subscribe(request_id, tfn.name_space, tfn.name, params);

        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending SUBSCRIBE to conn_id: {} request_id: {} track namespace hash: {} name hash: {}",
                            conn_ctx.connection_handle,
                            request_id,
                            th.track_namespace_hash,
                            th.track_name_hash);

        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), buffer);
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

        Bytes buffer;
        buffer << Publish(request_id, tfn.name_space, tfn.name, track_alias, params, extensions);

        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending PUBLISH to conn_id: {} request_id: {} track alias: {}",
                            conn_ctx.connection_handle,
                            request_id,
                            track_alias);

        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending Publish (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendPublishOk(ConnectionContext& conn_ctx,
                                  messages::RequestID request_id,
                                  bool forward,
                                  std::uint8_t priority,
                                  messages::GroupOrder group_order,
                                  messages::FilterType filter_type)
    try {
        // TODO: Add support for these filter types.
        if (filter_type == FilterType::kAbsoluteStart || filter_type == FilterType::kAbsoluteRange) {
            throw std::runtime_error("Absolute filtering not yet supported for Subscribe");
        }

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
                        .Add(ParameterType::kGroupOrder, group_order)
                        .Add(ParameterType::kSubscriptionFilter, filter_type)
                        .Add(ParameterType::kForward, forward);

        Bytes buffer;
        buffer << PublishOk(request_id, params);

        SPDLOG_LOGGER_DEBUG(
          logger_, "Sending PUBLISH_OK to conn_id: {} request_id: {} ", conn_ctx.connection_handle, request_id);

        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), buffer);

    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending Publish Ok (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendSubscribeOk(ConnectionContext& conn_ctx,
                                    uint64_t request_id,
                                    uint64_t track_alias,
                                    uint64_t expires,
                                    const std::optional<Location>& largest_location)
    try {
        auto params = Parameters{}
                        .Add(ParameterType::kExpires, expires)
                        .AddOptional(ParameterType::kLargestObject, largest_location);

        auto extensions = TrackExtensions{}
                            .Add(ExtensionType::kDeliveryTimeout, 0)
                            .Add(ExtensionType::kMaxCacheDuration, 0)
                            .Add(ExtensionType::kDefaultPublisherGroupOrder, GroupOrder::kAscending)
                            .Add(ExtensionType::kDefaultPublisherPriority, 1)
                            .Add(ExtensionType::kDynamicGroups, true);

        Bytes buffer;
        buffer << SubscribeOk(request_id, track_alias, params, extensions);

        SPDLOG_LOGGER_DEBUG(
          logger_, "Sending SUBSCRIBE OK to conn_id: {} request_id: {}", conn_ctx.connection_handle, request_id);

        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending SubscribeOk (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendPublishDone(ConnectionContext& conn_ctx,
                                    uint64_t request_id,
                                    messages::PublishDoneStatusCode status,
                                    const std::string& reason)
    try {
        auto publish_done = messages::PublishDone(request_id, status, 0, quicr::Bytes(reason.begin(), reason.end()));

        Bytes buffer;
        buffer << publish_done;

        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending PUBLISH_DONE to conn_id: {} request_id: {} status: {}",
                            conn_ctx.connection_handle,
                            request_id,
                            static_cast<uint64_t>(status));

        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending PUBLISH_DONE (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendUnsubscribe(ConnectionContext& conn_ctx, uint64_t request_id)
    try {
        auto unsubscribe = messages::Unsubscribe(request_id);

        Bytes buffer;
        buffer << unsubscribe;

        SPDLOG_LOGGER_DEBUG(
          logger_, "Sending UNSUBSCRIBE to conn_id: {} request_id: {}", conn_ctx.connection_handle, request_id);

        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending Unsubscribe (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendSubscribeNamespace(ConnectionHandle conn_handle,
                                           std::shared_ptr<SubscribeNamespaceHandler> handler)
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
        auto params = Parameters{};

        Bytes buffer;
        buffer << messages::SubscribeNamespace(rid, prefix, SubscribeOptions::kBoth, params);

        handler->SetTransport(GetSharedPtr());

        auto th = TrackHash({ prefix, {} });

        if (auto [_, is_new] = conn_it->second.request_handlers.try_emplace(rid, handler); !is_new) {
            SPDLOG_LOGGER_WARN(logger_, "Namespace already subscribed to (alias={})", th.track_fullname_hash);
            return;
        }

        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending SUBSCRIBE_NAMESPACE to conn_id: {} request_id: {} prefix_hash: {}",
                            conn_it->second.connection_handle,
                            rid,
                            th.track_namespace_hash);

        handler->data_ctx_id_ = quic_transport_->CreateDataContext(conn_handle, true, 0, true);
        quic_transport_->CreateStream(conn_handle, handler->data_ctx_id_, 0);

        SendCtrlMsg(conn_it->second, handler->data_ctx_id_, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending SUBSCRIBE_NAMESPACE (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
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
        Bytes buffer;
        buffer << NamespaceDone(prefix);

        auto th = TrackHash({ prefix, {} });

        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending UNSUBSCRIBE_NAMESPACE to conn_id: {} prefix_hash: {}",
                            conn_handle,
                            th.track_namespace_hash);

        SendCtrlMsg(conn_it->second, handler->data_ctx_id_, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending UNSUBSCRIBE_NAMESPACE (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendFetch(ConnectionContext& conn_ctx,
                              uint64_t request_id,
                              const FullTrackName& tfn,
                              std::uint8_t priority,
                              messages::GroupOrder group_order,
                              const messages::Location& start_location,
                              const messages::FetchEndLocation& end_location)
    try {
        messages::Location wire_end_location = { .group = end_location.group,
                                                 .object =
                                                   end_location.object.has_value() ? *end_location.object + 1 : 0 };
        const auto group_0 = std::make_optional<messages::Fetch::Group_0>() = {
            tfn.name_space, tfn.name, start_location, wire_end_location
        };

        /* Available parameters:
         * - AUTHORIZATION TOKEN (0x03): Conveys authorization for the fetch request.
         * - SUBSCRIBER PRIORITY (0x20): Priority of the fetch response relative to other data.
         * - GROUP ORDER (0x22): Preference for the order of groups in the fetch response.
         */
        auto params =
          Parameters{}.Add(ParameterType::kSubscriberPriority, priority).Add(ParameterType::kGroupOrder, group_order);

        Bytes buffer;
        buffer << Fetch(request_id, messages::FetchType::kStandalone, group_0, std::nullopt, params);

        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending Fetch (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendJoiningFetch(ConnectionContext& conn_ctx,
                                     uint64_t request_id,
                                     std::uint8_t priority,
                                     messages::GroupOrder group_order,
                                     uint64_t joining_request_id,
                                     messages::GroupId joining_start,
                                     bool absolute)
    try {
        auto group_1 = std::make_optional<messages::Fetch::Group_1>() = { joining_request_id, joining_start };

        /* Available parameters:
         * - AUTHORIZATION TOKEN (0x03): Conveys authorization for the fetch request.
         * - SUBSCRIBER PRIORITY (0x20): Priority of the fetch response relative to other data.
         * - GROUP ORDER (0x22): Preference for the order of groups in the fetch response.
         */
        auto params =
          Parameters{}.Add(ParameterType::kSubscriberPriority, priority).Add(ParameterType::kGroupOrder, group_order);

        Bytes buffer;
        buffer << Fetch(request_id,
                        absolute ? FetchType::kAbsoluteJoiningFetch : FetchType::kRelativeJoiningFetch,
                        std::nullopt,
                        group_1,
                        params);

        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending JoiningFetch (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendFetchCancel(ConnectionContext& conn_ctx, uint64_t request_id)
    try {
        auto fetch_cancel = messages::FetchCancel(request_id);

        Bytes buffer;
        buffer << fetch_cancel;

        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending FetchCancel (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SendFetchOk(ConnectionContext& conn_ctx,
                                uint64_t request_id,
                                GroupOrder group_order,
                                bool end_of_track,
                                Location largest_location)
    try {
        /* Available parameters: None */
        auto params = Parameters{};

        auto extensions = TrackExtensions{}
                            .Add(ExtensionType::kDeliveryTimeout, 0)
                            .Add(ExtensionType::kMaxCacheDuration, 0)
                            .Add(ExtensionType::kDefaultPublisherGroupOrder, group_order)
                            .Add(ExtensionType::kDefaultPublisherPriority, 1)
                            .Add(ExtensionType::kDynamicGroups, true);

        Bytes buffer;
        buffer << FetchOk(request_id, end_of_track, largest_location, params, {});

        SendCtrlMsg(conn_ctx, conn_ctx.ctrl_data_ctx_id.value(), buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending FetchOk (error={})", e.what());
        // TODO: add error handling in libquicr in calling function
    }

    void Transport::SubscribeTrack(TransportConnId conn_id, std::shared_ptr<SubscribeTrackHandler> track_handler)
    {
        const auto& tfn = track_handler->GetFullTrackName();
        track_handler->connection_handle_ = conn_id;

        // Track hash is the track alias for now.
        // TODO(tievens): Evaluate; change hash to be more than 62 bits to avoid collisions
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
        auto filter_type = track_handler->GetFilterType();
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
                          filter_type,
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
                        // SendUnsubscribe(conn_ctx, handler.GetRequestId().value());
                        SendPublishDone(conn_ctx,
                                        handler.GetRequestId().value(),
                                        PublishDoneStatusCode::kSubscribtionEnded,
                                        "No publishers left");
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

        conn_it->second.request_handlers.erase(track_handler->GetRequestId().value());
        conn_it->second.pub_tracks_by_track_alias.erase(th.track_fullname_hash);

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
                                             false);

        // Set this transport as the one for the publisher to use.
        track_handler->SetTransport(GetSharedPtr());

        // Hold ref to track handler
        conn_it->second.request_handlers[*track_handler->GetRequestId()] = track_handler;
        conn_it->second.pub_tracks_by_name[th.track_namespace_hash][th.track_name_hash] = track_handler;
        conn_it->second.pub_tracks_by_track_alias[th.track_fullname_hash][conn_id] = track_handler;
        conn_it->second.pub_tracks_by_data_ctx_id[track_handler->publish_data_ctx_id_] = std::move(track_handler);
    }

    void Transport::PublishNamespace(ConnectionHandle conn_id, std::shared_ptr<PublishNamespaceHandler> track_handler)
    {
        auto prefix_hash = hash(track_handler->GetPrefix());
        SPDLOG_LOGGER_INFO(logger_, "Publish namespace conn_id: {0} hash: {1}", conn_id, prefix_hash);

        std::unique_lock<std::mutex> lock(state_mutex_);

        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Publish track conn_id: {0} does not exist.", conn_id);
            return;
        }

        track_handler->SetRequestId(conn_it->second.GetNextRequestId());

        SPDLOG_LOGGER_INFO(logger_, "Publishing to namespace hash: {0} sending ANNOUNCE message", prefix_hash);

        lock.unlock();

        track_handler->SetStatus(PublishNamespaceHandler::Status::kPendingResponse);

        lock.lock();

        SendPublishNamespace(conn_it->second, *track_handler->GetRequestId(), track_handler->GetPrefix());

        conn_it->second.request_handlers[*track_handler->GetRequestId()] = track_handler;

        track_handler->connection_handle_ = conn_id;
        track_handler->SetTransport(GetSharedPtr());
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
                                   const PublishResponse& publish_response)
    {
        const auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        switch (publish_response.reason_code) {
            case PublishResponse::ReasonCode::kOk: {
                for (auto& [_, track] : conn_it->second.request_handlers) {
                    if (auto h = track.Get<SubscribeNamespaceHandler>(); h) {
                        if (h->IsTrackAcceptable(attributes.track_full_name)) {
                            h->AcceptNewTrack(connection_handle, request_id, attributes);
                        }
                    }
                }

                SendPublishOk(conn_it->second,
                              request_id,
                              attributes.forward,
                              attributes.priority,
                              attributes.group_order,
                              attributes.filter_type);

                // Fan out PUBLISH, if requested.
                for (const auto& handle : publish_response.namespace_subscribers) {
                    const auto& conn_it = connections_.find(handle);
                    if (conn_it == connections_.end()) {
                        SPDLOG_LOGGER_WARN(logger_, "Bad connection handle on SUBSCRIBE_NAMESPACE fan out");
                        continue;
                    }
                    const auto outgoing_request = conn_it->second.GetNextRequestId();
                    conn_it->second.pub_by_request_id[outgoing_request] = attributes.track_full_name;
                    SendPublish(conn_it->second,
                                outgoing_request,
                                attributes.track_full_name,
                                attributes.track_alias,
                                attributes.group_order,
                                publish_response.largest_location,
                                attributes.forward,
                                attributes.dynamic_groups);
                }
                break;
            }
            default:
                SendRequestError(conn_it->second, request_id, ErrorCode::kInternalError, 0ms, "Internal error");
                break;
        }
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
            if (auto h = req.Get<SubscribeTrackHandler>(); h) {

                RemoveSubscribeTrack(conn_ctx, *h, false);
                if (req.handler->GetConnectionId() == conn_ctx.connection_handle) {
                    static_cast<SubscribeTrackHandler*>(req.handler.get())
                      ->SetStatus(SubscribeTrackHandler::Status::kNotConnected);
                }
            } else if (auto h = req.Get<PublishTrackHandler>(); h) {
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

                    if (ProcessCtrlMessage(
                          conn_ctx,
                          data_ctx_id.value(),
                          ctrl_msg_buffer.msg_type.value(),
                          { ctrl_msg_buffer.data.begin() + sizeof(payload_len), ctrl_msg_buffer.data.end() })) {

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
                        rx_ctx->unknown_expiry_tick_ms = tick_service_->Milliseconds();
                        rx_ctx->unknown_expiry_tick_ms += age_ms;

                        SPDLOG_LOGGER_INFO(logger_,
                                           "Setting stream_id: {} unknown expiry to {}ms (current time is {}ms)",
                                           stream_id,
                                           rx_ctx->unknown_expiry_tick_ms,
                                           tick_service_->Milliseconds());
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

                    quic_transport_->CloseStream(conn_id, data_ctx_id.value(), stream_id, true);
                }
            }
        } // end of for loop rx data queue
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception on receiving stream. (error={})", e.what());
        throw;

        // TODO(tievens): Add metrics to track if this happens
    }

    void Transport::OnStreamClosed(const ConnectionHandle& connection_handle,
                                   [[maybe_unused]] std::uint64_t stream_id,
                                   std::shared_ptr<StreamRxContext> rx_ctx,
                                   StreamClosedFlag flag)
    {
        if (!rx_ctx->caller_any.has_value()) {
            SPDLOG_LOGGER_DEBUG(logger_, "Received stream closed with null handler");
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
            if (const auto handler_ptr = handler_weak.lock(); handler_ptr) {
                try {
                    if (auto handler = dynamic_cast<SubscribeTrackHandler*>(handler_ptr.get()); handler) {
                        switch (flag) {
                            case StreamClosedFlag::kFin:
                                handler->SetStatus(FetchTrackHandler::Status::kDoneByFin);
                                handler->StreamClosed(stream_id, false);
                                break;
                            case StreamClosedFlag::kReset:
                                handler->SetStatus(FetchTrackHandler::Status::kDoneByReset);
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
        if (sub_it == conn_ctx.sub_by_recv_track_alias.end() || sub_it->second == nullptr) {
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

        if (auto h = fetch_it->second.Get<SubscribeTrackHandler>(); h) {
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
} // namespace moq
