// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/detail/transport.h"
#include "quicr/detail/messages.h"

#include <format>
#include <quicr/detail/joining_fetch_handler.h>
#include <sstream>

namespace quicr {
    using namespace quicr::messages;

    TransportException::TransportException(TransportError error, std::source_location location)
      : std::runtime_error(std::format("Error in transport (error={}, line={}, file={})",
                                       static_cast<int>(error),
                                       location.line(),
                                       location.file_name()))
      , Error(error)
    {
    }

    static std::optional<std::tuple<std::string, uint16_t>> ParseConnectUri(const std::string& connect_uri)
    {
        // moq://domain:port/<dont-care>
        const std::string proto = "moq://";
        auto it = std::search(connect_uri.begin(), connect_uri.end(), proto.begin(), proto.end());

        if (it == connect_uri.end()) {
            return std::nullopt;
        }

        // move to end for moq://
        std::advance(it, proto.length());

        std::string address_str;
        std::string port_str;
        uint16_t port = 0;

        do {
            auto colon = std::find(it, connect_uri.end(), ':');
            if (address_str.empty() && colon == connect_uri.end()) {
                break;
            }

            if (address_str.empty()) {
                // parse resource id
                address_str.reserve(distance(it, colon));
                address_str.assign(it, colon);
                std::advance(it, address_str.length());
                it++;
                continue;
            }

            auto slash = std::find(it, connect_uri.end(), '/');

            if (port_str.empty()) {
                // parse client/sender id
                port_str.reserve(distance(it, slash));
                port_str.assign(it, slash);
                std::advance(it, port_str.length());
                port = stoi(port_str, nullptr);
                it++;
                break;
            }

        } while (it != connect_uri.end());

        if (address_str.empty() || port_str.empty()) {
            return std::nullopt;
        }

        return std::make_tuple(address_str, port);
    }

    Transport::Transport(const ClientConfig& cfg, std::shared_ptr<TickService> tick_service)
      : client_mode_(true)
      , logger_(spdlog::stderr_color_mt("MTC"))
      , server_config_({})
      , client_config_(cfg)
      , tick_service_(std::move(tick_service))
      , quic_transport_({})
    {
        SPDLOG_LOGGER_TRACE(logger_, "Created Moq instance in client mode connecting to {0}", cfg.connect_uri);
        Init();
    }

    Transport::Transport(const ServerConfig& cfg, std::shared_ptr<TickService> tick_service)
      : client_mode_(false)
      , logger_(spdlog::stderr_color_mt("MTS"))
      , server_config_(cfg)
      , client_config_({})
      , tick_service_(std::move(tick_service))
      , quic_transport_({})
    {
        SPDLOG_LOGGER_INFO(
          logger_, "Created Moq instance in server mode listening on {0}:{1}", cfg.server_bind_ip, cfg.server_port);
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
        if (client_mode_) {
            TransportRemote relay;
            auto parse_result = ParseConnectUri(client_config_.connect_uri);
            if (!parse_result) {
                return Status::kInvalidParams;
            }
            auto [address, port] = parse_result.value();
            relay.host_or_ip = address;
            relay.port = port; // TODO: Add URI parser
            relay.proto = TransportProtocol::kQuic;

            quic_transport_ =
              ITransport::MakeClientTransport(relay, client_config_.transport_config, *this, tick_service_, logger_);

            auto conn_id = quic_transport_->Start();

            SetConnectionHandle(conn_id);

            status_ = Status::kConnecting;
            StatusChanged(status_);

            SPDLOG_LOGGER_INFO(logger_, "Connecting session conn_id: {0}...", conn_id);
            auto [conn_ctx, _] = connections_.try_emplace(conn_id, ConnectionContext{});
            conn_ctx->second.connection_handle = conn_id;

            return status_;
        }

        TransportRemote server;
        server.host_or_ip = server_config_.server_bind_ip;
        server.port = server_config_.server_port;
        server.proto = TransportProtocol::kQuic;

        quic_transport_ =
          ITransport::MakeServerTransport(server, server_config_.transport_config, *this, tick_service_, logger_);
        quic_transport_->Start();

        status_ = Status::kReady;
        return status_;
    }

    Transport::Status Transport::Stop()
    {
        return Status();
    }

    void Transport::SendCtrlMsg(const ConnectionContext& conn_ctx, BytesSpan data)
    {
        if (not conn_ctx.ctrl_data_ctx_id) {
            CloseConnection(conn_ctx.connection_handle,
                            messages::TerminationReason::kProtocolViolation,
                            "Control bidir stream not created");
            return;
        }

        auto result = quic_transport_->Enqueue(conn_ctx.connection_handle,
                                               *conn_ctx.ctrl_data_ctx_id,
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
        auto supported_versions = { kMoqtVersion };
        messages::SetupParameters setup_parameters;
        setup_parameters.push_back(
          { .type = messages::SetupParameterType::kEndpointId,
            .value = { client_config_.endpoint_id.begin(), client_config_.endpoint_id.end() } });

        auto client_setup = messages::ClientSetup(supported_versions, setup_parameters);

        Bytes buffer;
        // SAH - FIXME - preallocate "buffer" to encode the data...
        buffer << client_setup;

        auto& conn_ctx = connections_.begin()->second;

        SendCtrlMsg(conn_ctx, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending ClientSetup (error={})", e.what());
        throw;
    }

    void Transport::SendServerSetup(ConnectionContext& conn_ctx)
    try {
        auto selected_version = conn_ctx.client_version;

        messages::SetupParameters setup_parameters;
        setup_parameters.push_back(
          { .type = messages::SetupParameterType::kEndpointId,
            .value = { server_config_.endpoint_id.begin(), server_config_.endpoint_id.end() } });

        auto server_setup = messages::ServerSetup(selected_version, setup_parameters);

        Bytes buffer;
        buffer << server_setup;

        SPDLOG_LOGGER_DEBUG(logger_, "Sending SERVER_SETUP to conn_id: {0}", conn_ctx.connection_handle);

        SendCtrlMsg(conn_ctx, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending ServerSetup (error={})", e.what());
        throw;
    }

    void Transport::SendAnnounce(ConnectionContext& conn_ctx,
                                 RequestID request_id,
                                 const TrackNamespace& track_namespace)
    try {
        auto announce = messages::Announce(request_id, track_namespace, {});

        Bytes buffer;
        buffer << announce;

        auto th = TrackHash({ track_namespace, {} });
        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending ANNOUNCE to conn_id: {} request_id: {} namespace_hash: {}",
                            conn_ctx.connection_handle,
                            request_id,
                            th.track_namespace_hash);

        SendCtrlMsg(conn_ctx, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending Announce (error={})", e.what());
        throw;
    }

    void Transport::SendAnnounceOk(ConnectionContext& conn_ctx, RequestID request_id)
    try {
        auto announce_ok = messages::AnnounceOk(request_id);

        Bytes buffer;
        buffer << announce_ok;

        SPDLOG_LOGGER_DEBUG(
          logger_, "Sending ANNOUNCE OK to conn_id: {} request_id: {}", conn_ctx.connection_handle, request_id);

        SendCtrlMsg(conn_ctx, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending AnnounceOk (error={})", e.what());
        throw;
    }

    void Transport::SendUnannounce(ConnectionContext& conn_ctx, const TrackNamespace& track_namespace)
    try {
        auto unannounce = messages::Unannounce(track_namespace);

        Bytes buffer;
        buffer << unannounce;

        SPDLOG_LOGGER_DEBUG(logger_, "Sending UNANNOUNCE to conn_id: {}", conn_ctx.connection_handle);

        SendCtrlMsg(conn_ctx, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending Unannounce (error={})", e.what());
        throw;
    }

    void Transport::SendSubscribe(ConnectionContext& conn_ctx,
                                  uint64_t request_id,
                                  const FullTrackName& tfn,
                                  TrackHash th,
                                  SubscriberPriority priority,
                                  GroupOrder group_order,
                                  FilterType filter_type,
                                  std::chrono::milliseconds delivery_timeout)
    try {
        std::uint64_t delivery_timeout_ms = delivery_timeout.count();

        auto subscribe =
          Subscribe(request_id,
                    tfn.name_space,
                    tfn.name,
                    priority,
                    group_order,
                    1,
                    filter_type,
                    nullptr,
                    std::nullopt,
                    nullptr,
                    std::nullopt,
                    {
                      Parameter{
                        ParameterType::kDeliveryTimeout,
                        Bytes{ reinterpret_cast<uint8_t*>(&delivery_timeout_ms),
                               reinterpret_cast<uint8_t*>(&delivery_timeout_ms) + sizeof(std::uint64_t) },
                      },
                    });

        Bytes buffer;
        buffer << subscribe;

        SPDLOG_LOGGER_DEBUG(
          logger_,
          "Sending SUBSCRIBE to conn_id: {0} request_id: {1} track namespace hash: {2} name hash: {3}",
          conn_ctx.connection_handle,
          request_id,
          th.track_namespace_hash,
          th.track_name_hash);

        SendCtrlMsg(conn_ctx, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending Subscribe (error={})", e.what());
        throw;
    }

    void Transport::SendPublish(ConnectionContext& conn_ctx,
                                messages::RequestID request_id,
                                const FullTrackName& tfn,
                                TrackHash th,
                                messages::SubscriberPriority priority,
                                messages::GroupOrder group_order,
                                bool content_exists,
                                messages::Location largest_location,
                                bool forward)
    try {

        auto publish = Publish(request_id,
                               tfn.name_space,
                               tfn.name,
                               th.track_fullname_hash,
                               group_order,
                               1,
                               nullptr,
                               std::nullopt,
                               forward,
                               {});

        Bytes buffer;
        buffer << publish;

        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending PUBLISH to conn_id: {0} request_id: {1} track namespace hash: {2} name hash: {3}",
                            conn_ctx.connection_handle,
                            request_id,
                            th.track_namespace_hash,
                            th.track_name_hash);

        SendCtrlMsg(conn_ctx, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending Publish (error={})", e.what());
        throw;
    }

    void Transport::SendPublishOk(ConnectionContext& conn_ctx,
                                  messages::RequestID request_id,
                                  bool forward,
                                  messages::SubscriberPriority priority,
                                  messages::GroupOrder group_order,
                                  messages::FilterType filter_type)
    try {
        auto publish_ok = PublishOk(
          request_id, forward, priority, group_order, filter_type, nullptr, std::nullopt, nullptr, std::nullopt, {});

        Bytes buffer;
        buffer << publish_ok;

        SPDLOG_LOGGER_DEBUG(
          logger_, "Sending PUBLISH_OK to conn_id: {0} request_id: {1} ", conn_ctx.connection_handle, request_id);

        SendCtrlMsg(conn_ctx, buffer);

    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending Publish Ok (error={})", e.what());
        throw;
    }

    void Transport::SendPublishError(ConnectionContext& conn_ctx,
                                     messages::RequestID request_id,
                                     messages::SubscribeErrorCode error,
                                     const std::string& reason)
    try {
        const auto publish_err = PublishError(request_id, static_cast<int>(error), Bytes(reason.begin(), reason.end()));

        Bytes buffer;
        buffer << publish_err;

        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending PUBLISH_ERROR to conn_id: {0} request_id: {1} error code: {2} reason: {3}",
                            conn_ctx.connection_handle,
                            request_id,
                            static_cast<int>(error),
                            reason);

        SendCtrlMsg(conn_ctx, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending Publish Error (error={})", e.what());
        throw;
    }

    void Transport::SendSubscribeUpdate(quicr::Transport::ConnectionContext& conn_ctx,
                                        uint64_t request_id,
                                        quicr::TrackHash th,
                                        Location start_location,
                                        messages::GroupId end_group_id,
                                        messages::SubscriberPriority priority,
                                        bool forward)
    try {
        auto subscribe_update =
          messages::SubscribeUpdate(request_id, start_location, end_group_id, priority, forward, {});

        Bytes buffer;
        buffer << subscribe_update;

        SPDLOG_LOGGER_DEBUG(
          logger_,
          "Sending SUBSCRIBE_UPDATE to conn_id: {0} request_id: {1} track namespace hash: {2} name hash: {3}",
          conn_ctx.connection_handle,
          request_id,
          th.track_namespace_hash,
          th.track_name_hash);

        SendCtrlMsg(conn_ctx, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending SubscribeUpdate (error={})", e.what());
        throw;
    }

    void Transport::SendSubscribeOk(ConnectionContext& conn_ctx,
                                    uint64_t request_id,
                                    uint64_t track_alias,
                                    uint64_t expires,
                                    bool content_exists,
                                    Location largest_location)
    try {
        const auto group_0 = std::make_optional<SubscribeOk::Group_0>() = { largest_location };
        const auto subscribe_ok =
          SubscribeOk(request_id, track_alias, expires, GroupOrder::kAscending, content_exists, nullptr, group_0, {});

        Bytes buffer;
        buffer << subscribe_ok;

        SPDLOG_LOGGER_DEBUG(
          logger_, "Sending SUBSCRIBE OK to conn_id: {0} request_id: {1}", conn_ctx.connection_handle, request_id);

        SendCtrlMsg(conn_ctx, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending SubscribeOk (error={})", e.what());
        throw;
    }

    void Transport::SendSubscribeDone(ConnectionContext& conn_ctx, uint64_t request_id, const std::string& reason)
    try {
        auto subscribe_done = messages::SubscribeDone(request_id,
                                                      messages::SubscribeDoneStatusCode::kSubscribtionEnded,
                                                      0,
                                                      quicr::Bytes(reason.begin(), reason.end()));

        Bytes buffer;
        buffer << subscribe_done;

        SPDLOG_LOGGER_DEBUG(
          logger_, "Sending SUBSCRIBE DONE to conn_id: {0} request_id: {1}", conn_ctx.connection_handle, request_id);

        SendCtrlMsg(conn_ctx, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending SubscribeDone (error={})", e.what());
        throw;
    }

    void Transport::SendUnsubscribe(ConnectionContext& conn_ctx, uint64_t request_id)
    try {
        auto unsubscribe = messages::Unsubscribe(request_id);

        Bytes buffer;
        buffer << unsubscribe;

        SPDLOG_LOGGER_DEBUG(
          logger_, "Sending UNSUBSCRIBE to conn_id: {0} request_id: {1}", conn_ctx.connection_handle, request_id);

        SendCtrlMsg(conn_ctx, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending Unsubscribe (error={})", e.what());
        throw;
    }

    void Transport::SendSubscribeAnnounces(ConnectionHandle conn_handle, const TrackNamespace& prefix_namespace)
    try {
        std::lock_guard<std::mutex> _(state_mutex_);
        auto conn_it = connections_.find(conn_handle);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Subscribe track conn_id: {0} does not exist.", conn_handle);
            return;
        }

        auto rid = conn_it->second.next_request_id++ << 1;
        if (client_mode_) {
            rid++;
        }

        conn_it->second.sub_announces_by_request_id[rid] = prefix_namespace;
        auto msg = messages::SubscribeAnnounces(rid, prefix_namespace, {});

        Bytes buffer;
        buffer << msg;

        auto th = TrackHash({ prefix_namespace, {} });

        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending Subscribe announces to conn_id: {} request_id: {} prefix_hash: {}",
                            conn_it->second.connection_handle,
                            rid,
                            th.track_namespace_hash);

        SendCtrlMsg(conn_it->second, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending SubscribeAnnounces (error={})", e.what());
        throw;
    }

    void Transport::SendSubscribeAnnouncesOk(ConnectionContext& conn_ctx, RequestID request_id)
    try {
        auto msg = messages::SubscribeAnnouncesOk(request_id);

        Bytes buffer;
        buffer << msg;

        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending Subscribe announces ok to conn_id: {} request_id: {}",
                            conn_ctx.connection_handle,
                            request_id);

        SendCtrlMsg(conn_ctx, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending SubscribeAnnouncesOk (error={})", e.what());
        throw;
    }

    void Transport::SendSubscribeAnnouncesError(ConnectionContext& conn_ctx,
                                                RequestID request_id,
                                                messages::SubscribeAnnouncesErrorCode err_code,
                                                const messages::ReasonPhrase& reason)
    try {

        auto msg = messages::SubscribeAnnouncesError(request_id, err_code, quicr::Bytes(reason.begin(), reason.end()));

        Bytes buffer;
        buffer << msg;

        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending Subscribe announces error to conn_id: {} request_id: {}",
                            conn_ctx.connection_handle,
                            request_id);

        SendCtrlMsg(conn_ctx, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending SubscribeAnnouncesError (error={})", e.what());
        throw;
    }

    void Transport::SendUnsubscribeAnnounces(ConnectionHandle conn_handle, const TrackNamespace& prefix_namespace)
    try {
        std::lock_guard<std::mutex> _(state_mutex_);
        auto conn_it = connections_.find(conn_handle);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Subscribe track conn_id: {0} does not exist.", conn_handle);
            return;
        }

        for (auto it = conn_it->second.sub_announces_by_request_id.begin();
             it != conn_it->second.sub_announces_by_request_id.end();
             ++it) {
            if (it->second == prefix_namespace) {
                conn_it->second.sub_announces_by_request_id.erase(it);
                break;
            }
        }

        auto msg = messages::UnsubscribeAnnounces(prefix_namespace);

        Bytes buffer;
        buffer << msg;

        auto th = TrackHash({ prefix_namespace, {} });

        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending Unsubscribe announces to conn_id: {} prefix_hash: {}",
                            conn_handle,
                            th.track_namespace_hash);

        SendCtrlMsg(conn_it->second, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending UnsubscribeAnnounces (error={})", e.what());
        throw;
    }

    void Transport::SendSubscribeError(ConnectionContext& conn_ctx,
                                       uint64_t request_id,
                                       SubscribeErrorCode error,
                                       const std::string& reason)
    try {
        const auto subscribe_err = SubscribeError(request_id, error, Bytes(reason.begin(), reason.end()));

        Bytes buffer;
        buffer << subscribe_err;

        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending SUBSCRIBE ERROR to conn_id: {0} request_id: {1} error code: {2} reason: {3}",
                            conn_ctx.connection_handle,
                            request_id,
                            static_cast<int>(error),
                            reason);

        SendCtrlMsg(conn_ctx, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending SubscribeError (error={})", e.what());
        throw;
    }

    void Transport::SendFetch(ConnectionContext& conn_ctx,
                              uint64_t request_id,
                              const FullTrackName& tfn,
                              messages::SubscriberPriority priority,
                              messages::GroupOrder group_order,
                              messages::GroupId start_group,
                              messages::GroupId start_object,
                              messages::GroupId end_group,
                              messages::GroupId end_object)
    try {
        const auto group_0 = std::make_optional<messages::Fetch::Group_0>() = {
            tfn.name_space, tfn.name, { start_group, start_object }, { end_group, end_object }
        };

        auto fetch = messages::Fetch(request_id,
                                     priority,
                                     group_order,
                                     messages::FetchType::kStandalone,
                                     nullptr,
                                     group_0,
                                     nullptr,
                                     std::nullopt,
                                     {});

        Bytes buffer;
        buffer << fetch;

        SendCtrlMsg(conn_ctx, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending Fetch (error={})", e.what());
        throw;
    }

    void Transport::SendJoiningFetch(ConnectionContext& conn_ctx,
                                     uint64_t request_id,
                                     messages::SubscriberPriority priority,
                                     messages::GroupOrder group_order,
                                     uint64_t joining_request_id,
                                     messages::GroupId preceding_group_offset,
                                     const messages::Parameters parameters)
    try {
        auto group_1 = std::make_optional<messages::Fetch::Group_1>() = { joining_request_id, preceding_group_offset };

        auto fetch = messages::Fetch(request_id,
                                     priority,
                                     group_order,
                                     messages::FetchType::kJoiningFetch,
                                     nullptr,
                                     std::nullopt,
                                     nullptr,
                                     group_1,
                                     parameters);

        Bytes buffer;
        buffer << fetch;

        SendCtrlMsg(conn_ctx, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending JoiningFetch (error={})", e.what());
        throw;
    }

    void Transport::SendFetchCancel(ConnectionContext& conn_ctx, uint64_t request_id)
    try {
        auto fetch_cancel = messages::FetchCancel(request_id);

        Bytes buffer;
        buffer << fetch_cancel;

        SendCtrlMsg(conn_ctx, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending FetchCancel (error={})", e.what());
        throw;
    }

    void Transport::SendFetchOk(ConnectionContext& conn_ctx,
                                uint64_t request_id,
                                GroupOrder group_order,
                                bool end_of_track,
                                Location largest_location)
    try {
        auto fetch_ok = messages::FetchOk(request_id, group_order, end_of_track, largest_location, {});

        Bytes buffer;
        buffer << fetch_ok;

        SendCtrlMsg(conn_ctx, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending FetchOk (error={})", e.what());
        throw;
    }

    void Transport::SendFetchError(ConnectionContext& conn_ctx,
                                   uint64_t request_id,
                                   FetchErrorCode error,
                                   const std::string& reason)
    try {
        auto fetch_err = messages::FetchError(request_id, error, quicr::Bytes(reason.begin(), reason.end()));

        Bytes buffer;
        buffer << fetch_err;

        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending FETCH ERROR to conn_id: {0} request_id: {1} error code: {2} reason: {3}",
                            conn_ctx.connection_handle,
                            request_id,
                            static_cast<int>(error),
                            reason);

        SendCtrlMsg(conn_ctx, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending FetchError (error={})", e.what());
        throw;
    }

    void Transport::SendNewGroupRequest(ConnectionHandle conn_id, uint64_t request_id, uint64_t track_alias)
    try {
        auto new_group_request = messages::NewGroupRequest(request_id, track_alias);
        Bytes buffer;
        buffer << new_group_request;

        std::lock_guard<std::mutex> _(state_mutex_);
        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Subscribe track conn_id: {0} does not exist.", conn_id);
            return;
        }

        SendCtrlMsg(conn_it->second, buffer);
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception sending NewGroupRequest (error={})", e.what());
        throw;
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

        SPDLOG_LOGGER_INFO(logger_, "Subscribe track conn_id: {0} track_alias: {1}", conn_id, th.track_fullname_hash);

        std::lock_guard<std::mutex> _(state_mutex_);
        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Subscribe track conn_id: {0} does not exist.", conn_id);
            return;
        }

        auto sid = conn_it->second.next_request_id++ << 1;

        if (!track_handler->IsPublisherInitiated()) {
            if (client_mode_) {
                sid++;
            }

            track_handler->SetRequestId(sid);
        } else {
            sid = *track_handler->GetRequestId();
        }

        auto priority = track_handler->GetPriority();
        auto group_order = track_handler->GetGroupOrder();
        auto filter_type = track_handler->GetFilterType();
        auto delivery_timeout = track_handler->GetDeliveryTimeout();

        track_handler->new_group_request_callback_ = [=, this](auto req_id, auto track_alias) {
            SendNewGroupRequest(conn_id, req_id, track_alias);
        };

        track_handler->set_forwarding_func_ = [=, this](bool forward) {
            SendSubscribeUpdate(conn_it->second, sid, th, {}, 0, track_handler->GetPriority(), forward);
        };

        // Set the track handler for tracking by subscribe ID and track alias
        conn_it->second.sub_by_track_alias[*track_handler->GetTrackAlias()] = track_handler;
        conn_it->second.tracks_by_request_id[sid] = track_handler;

        if (!track_handler->IsPublisherInitiated()) {
            SendSubscribe(conn_it->second, sid, tfn, th, priority, group_order, filter_type, delivery_timeout);

            // Handle joining fetch, if requested.
            auto joining_fetch = track_handler->GetJoiningFetch();
            if (track_handler->GetJoiningFetch()) {
                // Make a joining fetch handler.
                const auto joining_fetch_handler = std::make_shared<JoiningFetchHandler>(track_handler);
                const auto& info = *joining_fetch;
                const auto fetch_sid = conn_it->second.next_request_id++;
                SPDLOG_LOGGER_INFO(logger_,
                                   "Subscribe with joining fetch conn_id: {0} track_alias: {1} subscribe id: {2} "
                                   "joining subscribe id: {3}",
                                   conn_id,
                                   th.track_fullname_hash,
                                   fetch_sid,
                                   sid);
                conn_it->second.tracks_by_request_id[fetch_sid] = std::move(joining_fetch_handler);
                SendJoiningFetch(conn_it->second,
                                 fetch_sid,
                                 info.priority,
                                 info.group_order,
                                 sid,
                                 info.preceding_group_offset,
                                 info.parameters);
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

        SPDLOG_LOGGER_INFO(logger_, "Subscribe track conn_id: {0} hash: {1}", conn_id, th.track_fullname_hash);

        std::lock_guard<std::mutex> _(state_mutex_);
        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Subscribe track conn_id: {0} does not exist.", conn_id);
            return;
        }

        if (not track_handler->GetRequestId().has_value()) {
            return;
        }

        SPDLOG_LOGGER_DEBUG(
          logger_, "subscribe id (from subscribe) to add to memory: {0}", track_handler->GetRequestId().value());

        auto priority = track_handler->GetPriority();
        SendSubscribeUpdate(
          conn_it->second, track_handler->GetRequestId().value(), th, { 0x0, 0x0 }, 0x0, priority, true);
    }

    void Transport::RemoveSubscribeTrack(ConnectionContext& conn_ctx,
                                         SubscribeTrackHandler& handler,
                                         bool remove_handler)
    {
        handler.SetStatus(SubscribeTrackHandler::Status::kNotSubscribed);

        auto request_id = handler.GetRequestId();

        handler.SetRequestId(std::nullopt);

        if (request_id.has_value()) {
            SendUnsubscribe(conn_ctx, *request_id);

            SPDLOG_LOGGER_DEBUG(logger_, "Removed subscribe track subscribe id: {0}", *request_id);

            if (remove_handler) {
                handler.SetStatus(SubscribeTrackHandler::Status::kNotConnected); // Set after remove subscribe track

                std::lock_guard<std::mutex> _(state_mutex_);
                conn_ctx.tracks_by_request_id.erase(*request_id);
                conn_ctx.sub_by_track_alias.erase(*handler.GetTrackAlias());
            }
        }
    }

    void Transport::UnpublishTrack(TransportConnId conn_id, const std::shared_ptr<PublishTrackHandler>& track_handler)
    {
        // Generate track alias
        auto tfn = track_handler->GetFullTrackName();
        auto th = TrackHash(tfn);

        SPDLOG_LOGGER_INFO(logger_, "Unpublish track conn_id: {0} hash: {1}", conn_id, th.track_fullname_hash);

        std::unique_lock<std::mutex> lock(state_mutex_);

        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Unpublish track conn_id: {0} does not exist.", conn_id);
            return;
        }

        conn_it->second.pub_tracks_by_request_id.erase(*track_handler->GetRequestId());
        conn_it->second.pub_tracks_ns_by_request_id.erase(*track_handler->GetRequestId());
        conn_it->second.pub_tracks_by_track_alias.erase(th.track_fullname_hash);

        if (not track_handler->UsingAnnounce()) {
            conn_it->second.recv_req_id.erase(th.track_fullname_hash);
        }

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
                      "Unpublish track namespace hash: {0} track_name_hash: {1} track_alias: {2}, sending "
                      "subscribe_done",
                      th.track_namespace_hash,
                      th.track_name_hash,
                      th.track_fullname_hash);
                    SendSubscribeDone(conn_it->second, *pub_n_it->second->GetRequestId(), "Unpublish track");
                } else {
                    SPDLOG_LOGGER_INFO(logger_,
                                       "Unpublish track namespace hash: {0} track_name_hash: {1} track_alias: {2}",
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

            if (pub_ns_it->second.size() == 0 && track_handler->UsingAnnounce()) {
                SPDLOG_LOGGER_INFO(
                  logger_, "Unpublish namespace hash: {0}, has no tracks, sending unannounce", th.track_namespace_hash);

                SendUnannounce(conn_it->second, tfn.name_space);
                conn_it->second.pub_tracks_by_name.erase(pub_ns_it);
            }
        }
    }

    void Transport::PublishTrack(TransportConnId conn_id, std::shared_ptr<PublishTrackHandler> track_handler)
    {
        const auto tfn = track_handler->GetFullTrackName();
        auto th = TrackHash(tfn);
        SPDLOG_LOGGER_INFO(logger_, "Publish track conn_id: {0} hash: {1}", conn_id, th.track_fullname_hash);

        std::unique_lock<std::mutex> lock(state_mutex_);

        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Publish track conn_id: {0} does not exist.", conn_id);
            return;
        }

        auto sid = conn_it->second.next_request_id++ << 1;
        if (client_mode_) {
            sid++;
        }

        track_handler->SetRequestId(sid);

        conn_it->second.pub_tracks_ns_by_request_id[sid] = th.track_namespace_hash;

        if (track_handler->UsingAnnounce()) {
            // Check if this published track is a new namespace or existing.
            auto pub_ns_it = conn_it->second.pub_tracks_by_name.find(th.track_namespace_hash);
            if (pub_ns_it == conn_it->second.pub_tracks_by_name.end()) {
                SPDLOG_LOGGER_INFO(logger_,
                                   "Publish track has new namespace hash: {0} sending ANNOUNCE message",
                                   th.track_namespace_hash);

                lock.unlock();

                track_handler->SetStatus(PublishTrackHandler::Status::kPendingAnnounceResponse);

                lock.lock();

                SendAnnounce(conn_it->second, sid, tfn.name_space);

            } else {
                auto pub_n_it = pub_ns_it->second.find(th.track_name_hash);
                if (pub_n_it == pub_ns_it->second.end()) {
                    track_handler->SetStatus(pub_ns_it->second.begin()->second->GetStatus());
                    SendAnnounce(conn_it->second, sid, tfn.name_space);

                    SPDLOG_LOGGER_INFO(logger_,
                                       "Publish track has new track namespace hash: {0} name hash: {1}",
                                       th.track_namespace_hash,
                                       th.track_name_hash);
                }
            }
        } else {
            // Add state to received request ID since a subscribe will not be received for this request
            conn_it->second.recv_req_id[sid] = { track_handler->GetFullTrackName() };

            track_handler->SetStatus(PublishTrackHandler::Status::kPendingPublishOk);
            SendPublish(conn_it->second,
                        sid,
                        tfn,
                        th,
                        track_handler->GetDefaultPriority(),
                        GroupOrder::kAscending,
                        1,
                        { track_handler->prev_object_group_id_, track_handler->prev_object_id_ },
                        true);
        }

        track_handler->connection_handle_ = conn_id;
        track_handler->publish_data_ctx_id_ =
          quic_transport_->CreateDataContext(conn_id,
                                             track_handler->default_track_mode_ == TrackMode::kDatagram ? false : true,
                                             track_handler->default_priority_,
                                             false);

        // Setup the function for the track handler to use to send objects with thread safety
        std::weak_ptr<PublishTrackHandler> weak_handler(track_handler);
        track_handler->publish_object_func_ =
          [&, weak_handler](uint8_t priority,
                            uint32_t ttl,
                            bool stream_header_needed,
                            uint64_t group_id,
                            uint64_t subgroup_id,
                            uint64_t object_id,
                            std::optional<Extensions> extensions,
                            std::span<const uint8_t> data) -> PublishTrackHandler::PublishObjectStatus {
            auto handler = weak_handler.lock();
            if (!handler) {
                return PublishTrackHandler::PublishObjectStatus::kInternalError;
            }
            return SendObject(
              *handler, priority, ttl, stream_header_needed, group_id, subgroup_id, object_id, extensions, data);
        };

        track_handler->forward_publish_data_func_ =
          [&,
           weak_handler](uint8_t priority,
                         uint32_t ttl,
                         bool stream_header_needed,
                         std::shared_ptr<const std::vector<uint8_t>> data) -> PublishTrackHandler::PublishObjectStatus {
            auto handler = weak_handler.lock();
            if (!handler) {
                return PublishTrackHandler::PublishObjectStatus::kInternalError;
            }
            return SendData(*handler, priority, ttl, stream_header_needed, data);
        };

        // Hold ref to track handler
        conn_it->second.pub_tracks_by_request_id[sid] = track_handler;
        conn_it->second.pub_tracks_by_name[th.track_namespace_hash][th.track_name_hash] = track_handler;
        conn_it->second.pub_tracks_by_track_alias[th.track_fullname_hash] = track_handler;
        conn_it->second.pub_tracks_by_data_ctx_id[track_handler->publish_data_ctx_id_] = std::move(track_handler);
    }

    bool Transport::FetchReceived([[maybe_unused]] ConnectionHandle connection_handle,
                                  [[maybe_unused]] uint64_t request_id,
                                  [[maybe_unused]] const FullTrackName& track_full_name,
                                  [[maybe_unused]] const quicr::messages::FetchAttributes& attributes)
    {
        return false;
    }

    void Transport::FetchTrack(ConnectionHandle connection_handle, std::shared_ptr<FetchTrackHandler> track_handler)
    {
        const auto& tfn = track_handler->GetFullTrackName();
        auto th = TrackHash(tfn);

        track_handler->SetTrackAlias(th.track_fullname_hash);

        SPDLOG_LOGGER_INFO(logger_, "Fetch track conn_id: {0} hash: {1}", connection_handle, th.track_fullname_hash);

        std::lock_guard<std::mutex> _(state_mutex_);
        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Fetch track conn_id: {0} does not exist.", connection_handle);
            return;
        }

        auto sid = conn_it->second.next_request_id++ << 1;
        if (client_mode_) {
            sid++;
        }

        track_handler->SetRequestId(sid);

        SPDLOG_LOGGER_DEBUG(logger_, "subscribe id (from fetch) to add to memory: {0}", sid);

        track_handler->SetRequestId(sid);

        auto priority = track_handler->GetPriority();
        auto group_order = track_handler->GetGroupOrder();
        auto start_group = track_handler->GetStartGroup();
        auto start_object = track_handler->GetStartObject();
        auto end_group = track_handler->GetEndGroup();
        auto end_object = track_handler->GetEndObject();

        track_handler->SetStatus(FetchTrackHandler::Status::kPendingResponse);

        conn_it->second.tracks_by_request_id[sid] = std::move(track_handler);

        SendFetch(conn_it->second, sid, tfn, priority, group_order, start_group, start_object, end_group, end_object);
    }

    void Transport::CancelFetchTrack(ConnectionHandle connection_handle,
                                     std::shared_ptr<FetchTrackHandler> track_handler)
    {
        std::lock_guard<std::mutex> _(state_mutex_);
        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Fetch track conn_id: {0} does not exist.", connection_handle);
            return;
        }

        const auto sub_id = track_handler->GetRequestId();
        if (!sub_id.has_value()) {
            return;
        }

        SendFetchCancel(conn_it->second, sub_id.value());

        track_handler->SetRequestId(std::nullopt);
        track_handler->SetStatus(FetchTrackHandler::Status::kNotConnected);
    }

    PublishTrackHandler::PublishObjectStatus Transport::SendData(PublishTrackHandler& track_handler,
                                                                 uint8_t priority,
                                                                 uint32_t ttl,
                                                                 bool stream_header_needed,
                                                                 std::shared_ptr<const std::vector<uint8_t>> data)
    {
        if (!track_handler.GetRequestId().has_value()) {
            return PublishTrackHandler::PublishObjectStatus::kNoSubscribers;
        }

        ITransport::EnqueueFlags eflags;

        switch (track_handler.default_track_mode_) {
            case TrackMode::kDatagram: {
                eflags.use_reliable = false;
                break;
            }
            default: {
                eflags.use_reliable = true;

                if (stream_header_needed) {
                    eflags.new_stream = true;
                    eflags.clear_tx_queue = true;
                    eflags.use_reset = true;
                }

                break;
            }
        }

        auto result = quic_transport_->Enqueue(
          track_handler.connection_handle_, track_handler.publish_data_ctx_id_, data, priority, ttl, 0, eflags);

        if (result != TransportError::kNone) {
            throw TransportException(result);
        }

        return PublishTrackHandler::PublishObjectStatus::kOk;
    }

    PublishTrackHandler::PublishObjectStatus Transport::SendObject(PublishTrackHandler& track_handler,
                                                                   uint8_t priority,
                                                                   uint32_t ttl,
                                                                   bool stream_header_needed,
                                                                   uint64_t group_id,
                                                                   uint64_t subgroup_id,
                                                                   uint64_t object_id,
                                                                   std::optional<Extensions> extensions,
                                                                   BytesSpan data)
    {
        if (!track_handler.GetRequestId().has_value()) {
            return PublishTrackHandler::PublishObjectStatus::kNoSubscribers;
        }
        const auto request_id = track_handler.GetRequestId().value();

        ITransport::EnqueueFlags eflags;

        track_handler.object_msg_buffer_.clear();

        switch (track_handler.default_track_mode_) {
            case TrackMode::kDatagram: {
                messages::ObjectDatagram object;
                object.group_id = group_id;
                object.object_id = object_id;
                object.priority = priority;
                object.track_alias = track_handler.GetTrackAlias(request_id);
                object.extensions = extensions;
                object.payload.assign(data.begin(), data.end());
                track_handler.object_msg_buffer_ << object;
                break;
            }
            default: {
                // use stream per subgroup, group change
                eflags.use_reliable = true;

                if (stream_header_needed) {
                    eflags.new_stream = true;
                    eflags.clear_tx_queue = true;
                    eflags.use_reset = true;

                    messages::StreamHeaderSubGroup subgroup_hdr;
                    subgroup_hdr.type = track_handler.GetStreamMode();
                    subgroup_hdr.group_id = group_id;
                    subgroup_hdr.subgroup_id = subgroup_id;
                    subgroup_hdr.priority = priority;
                    subgroup_hdr.track_alias = track_handler.GetTrackAlias(request_id);
                    track_handler.object_msg_buffer_ << subgroup_hdr;

                    auto result = quic_transport_->Enqueue(
                      track_handler.connection_handle_,
                      track_handler.publish_data_ctx_id_,
                      std::make_shared<std::vector<uint8_t>>(track_handler.object_msg_buffer_.begin(),
                                                             track_handler.object_msg_buffer_.end()),
                      priority,
                      ttl,
                      0,
                      eflags);

                    track_handler.object_msg_buffer_.clear();
                    eflags.new_stream = false;
                    eflags.clear_tx_queue = false;
                    eflags.use_reset = false;

                    if (result != TransportError::kNone) {
                        throw TransportException(result);
                    }
                }

                messages::StreamSubGroupObject object;
                object.object_id = object_id;
                object.stream_type = track_handler.GetStreamMode();
                object.extensions = extensions;
                object.payload.assign(data.begin(), data.end());
                track_handler.object_msg_buffer_ << object;
                break;
            }
        }

        auto result =
          quic_transport_->Enqueue(track_handler.connection_handle_,
                                   track_handler.publish_data_ctx_id_,
                                   std::make_shared<std::vector<uint8_t>>(track_handler.object_msg_buffer_.begin(),
                                                                          track_handler.object_msg_buffer_.end()),
                                   priority,
                                   ttl,
                                   0,
                                   eflags);

        if (result != TransportError::kNone) {
            throw TransportException(result);
        }

        return PublishTrackHandler::PublishObjectStatus::kOk;
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
        for (const auto& [sub_id, handler] : conn_ctx.tracks_by_request_id) {
            if (not handler->IsPublisherInitiated())
                RemoveSubscribeTrack(conn_ctx, *handler, false);
        }

        // Notify publish handlers of disconnect
        for (const auto& [data_ctx_id, handler] : conn_ctx.pub_tracks_by_data_ctx_id) {
            handler->SetStatus(PublishTrackHandler::Status::kNotConnected);
            handler->SetRequestId(std::nullopt);
        }

        conn_ctx.pub_tracks_by_data_ctx_id.clear();
        conn_ctx.pub_tracks_by_name.clear();
        conn_ctx.recv_req_id.clear();
        conn_ctx.tracks_by_request_id.clear();
        conn_ctx.sub_by_track_alias.clear();
    }

    // ---------------------------------------------------------------------------------------
    // Transport handler callbacks
    // ---------------------------------------------------------------------------------------

    void Transport::OnConnectionStatus(const TransportConnId& conn_id, const TransportStatus status)
    {
        SPDLOG_LOGGER_DEBUG(logger_, "Connection status conn_id: {0} status: {1}", conn_id, static_cast<int>(status));
        ConnectionStatus conn_status = ConnectionStatus::kConnected;
        bool remove_connection = false;

        switch (status) {
            case TransportStatus::kReady: {
                if (client_mode_) {
                    auto& conn_ctx = connections_[conn_id];
                    SPDLOG_LOGGER_INFO(logger_,
                                       "Connection established, creating bi-dir stream and sending CLIENT_SETUP");

                    conn_ctx.ctrl_data_ctx_id = quic_transport_->CreateDataContext(conn_id, true, 0, true);

                    SendClientSetup();

                    if (client_mode_) {
                        status_ = Status::kPendingSeverSetup;
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
                remove_connection = true;
                break;

            case TransportStatus::kIdleTimeout:
                conn_status = ConnectionStatus::kIdleTimeout;
                remove_connection = true;
                break;

            case TransportStatus::kDisconnected: {
                conn_status = ConnectionStatus::kNotConnected;
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

            auto data_opt = rx_ctx->data_queue.Pop();
            if (not data_opt.has_value()) {
                break;
            }

            auto& data = *data_opt.value();

            // CONTROL STREAM
            if (is_bidir) {
                conn_ctx.ctrl_msg_buffer.insert(conn_ctx.ctrl_msg_buffer.end(), data.begin(), data.end());

                if (not conn_ctx.ctrl_data_ctx_id) {
                    if (not data_ctx_id) {
                        CloseConnection(conn_id,
                                        messages::TerminationReason::kInternalError,
                                        "Received bidir is missing data context");
                        return;
                    }
                    conn_ctx.ctrl_data_ctx_id = data_ctx_id;
                }

                while (conn_ctx.ctrl_msg_buffer.size() > 0) {
                    if (not conn_ctx.ctrl_msg_type_received.has_value()) {
                        // Decode message type
                        auto uv_sz = UintVar::Size(conn_ctx.ctrl_msg_buffer.front());
                        if (conn_ctx.ctrl_msg_buffer.size() < uv_sz) {
                            i = kReadLoopMaxPerStream - 4;
                            break; // Not enough bytes to process control message. Try again once more.
                        }

                        auto msg_type = uint64_t(quicr::UintVar(
                          { conn_ctx.ctrl_msg_buffer.begin(), conn_ctx.ctrl_msg_buffer.begin() + uv_sz }));

                        conn_ctx.ctrl_msg_buffer.erase(conn_ctx.ctrl_msg_buffer.begin(),
                                                       conn_ctx.ctrl_msg_buffer.begin() + uv_sz);

                        conn_ctx.ctrl_msg_type_received = static_cast<ControlMessageType>(msg_type);
                    }

                    uint16_t payload_len = 0;

                    // Decode control payload length in bytes
                    if (conn_ctx.ctrl_msg_buffer.size() < sizeof(payload_len)) {
                        i = kReadLoopMaxPerStream - 4;
                        break; // Not enough bytes to process control message. Try again once more.
                    }

                    std::memcpy(&payload_len, conn_ctx.ctrl_msg_buffer.data(), sizeof(payload_len));
                    payload_len = SwapBytes(payload_len);

                    if (conn_ctx.ctrl_msg_buffer.size() < payload_len + sizeof(payload_len)) {
                        i = kReadLoopMaxPerStream - 4;
                        break; // Not enough bytes to process control message. Try again once more.
                    }

                    if (ProcessCtrlMessage(
                          conn_ctx,
                          { conn_ctx.ctrl_msg_buffer.begin() + sizeof(payload_len), conn_ctx.ctrl_msg_buffer.end() })) {

                        // Reset the control message buffer and message type to start a new message.
                        conn_ctx.ctrl_msg_type_received = std::nullopt;
                        conn_ctx.ctrl_msg_buffer.erase(conn_ctx.ctrl_msg_buffer.begin(),
                                                       conn_ctx.ctrl_msg_buffer.begin() + sizeof(payload_len) +
                                                         payload_len);
                    } else {
                        conn_ctx.metrics.invalid_ctrl_stream_msg++;
                        conn_ctx.ctrl_msg_type_received = std::nullopt;
                        conn_ctx.ctrl_msg_buffer.erase(conn_ctx.ctrl_msg_buffer.begin(),
                                                       conn_ctx.ctrl_msg_buffer.begin() + sizeof(payload_len) +
                                                         payload_len);
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

                bool break_loop = false;
                const auto type = static_cast<StreamMessageType>(msg_type);
                if (TypeIsStreamHeaderType(type)) {
                    const auto stream_header_type = static_cast<StreamHeaderType>(msg_type);
                    break_loop = OnRecvSubgroup(stream_header_type, cursor_it, *rx_ctx, stream_id, conn_ctx, *data_opt);
                } else if (type == StreamMessageType::kFetchHeader) {
                    break_loop = OnRecvFetch(cursor_it, *rx_ctx, stream_id, conn_ctx, *data_opt);
                } else {
                    SPDLOG_LOGGER_DEBUG(logger_, "Received start of stream with invalid header type, dropping");
                    conn_ctx.metrics.rx_stream_invalid_type++;

                    // TODO(tievens): Need to reset this stream as this is invalid.
                    return;
                }
                if (break_loop) {
                    break;
                }
            } else if (rx_ctx->caller_any.has_value()) {
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
                }
            }
        } // end of for loop rx data queue
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception on receiving stream. (error={})", e.what());
        throw;

        // TODO(tievens): Add metrics to track if this happens
    }

    bool Transport::OnRecvSubgroup(StreamHeaderType type,
                                   std::vector<uint8_t>::const_iterator cursor_it,
                                   StreamRxContext& rx_ctx,
                                   std::uint64_t stream_id,
                                   ConnectionContext& conn_ctx,
                                   std::shared_ptr<const std::vector<uint8_t>> data) const
    {
        uint64_t track_alias = 0;
        uint8_t priority = 0;

        try {
            // First header in subgroup starts with track alias
            auto ta_sz = UintVar::Size(*cursor_it);
            track_alias = uint64_t(quicr::UintVar({ cursor_it, cursor_it + ta_sz }));
            cursor_it += ta_sz;

            auto group_id_sz = UintVar::Size(*cursor_it);
            cursor_it += group_id_sz;

            const auto properties = StreamHeaderProperties(type);
            if (properties.subgroup_id_type == SubgroupIdType::kExplicit) {
                auto subgroup_id_sz = UintVar::Size(*cursor_it);
                cursor_it += subgroup_id_sz;
            }

            priority = *cursor_it;

        } catch (std::invalid_argument&) {
            SPDLOG_LOGGER_WARN(logger_, "Received start of stream without enough bytes to process uintvar");
            return false;
        }

        rx_ctx.is_new = false;

        auto sub_it = conn_ctx.sub_by_track_alias.find(track_alias);
        if (sub_it == conn_ctx.sub_by_track_alias.end()) {
            conn_ctx.metrics.rx_stream_unknown_track_alias++;
            SPDLOG_LOGGER_WARN(
              logger_,
              "Received stream_header_subgroup to unknown subscribe track track_alias: {} stream: {}, ignored",
              track_alias,
              stream_id);

            // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging
            return false;
        }

        rx_ctx.caller_any = std::make_any<std::weak_ptr<SubscribeTrackHandler>>(sub_it->second);
        sub_it->second->SetPriority(priority);
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

        const auto fetch_it = conn_ctx.tracks_by_request_id.find(request_id);
        if (fetch_it == conn_ctx.tracks_by_request_id.end()) {
            // TODO: Metrics.
            SPDLOG_LOGGER_WARN(logger_,
                               "Received fetch_header to unknown fetch track request_id: {} stream: {}, ignored",
                               request_id,
                               stream_id);

            // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging
            return false;
        }

        rx_ctx.caller_any = std::make_any<std::weak_ptr<SubscribeTrackHandler>>(fetch_it->second);
        fetch_it->second->StreamDataRecv(true, stream_id, std::move(data));
        return true;
    }

    void Transport::OnRecvDgram(const TransportConnId& conn_id, std::optional<DataContextId> data_ctx_id)
    {
        messages::ObjectDatagram object_datagram_out;
        for (int i = 0; i < kReadLoopMaxPerStream; i++) {
            auto data = quic_transport_->Dequeue(conn_id, data_ctx_id);
            if (data && !data->empty() && data->size() > 3) {
                auto msg_type = data->front();

                // TODO: Handle ObjectDatagramStatus objects as well.

                // Message type needs to be either datagram header types or status types.
                const auto data_type = static_cast<DatagramMessageType>(msg_type);
                if (!TypeIsDatagram(data_type)) {
                    SPDLOG_LOGGER_DEBUG(
                      logger_, "Received datagram that is not a supported datagram type, dropping: {0}", msg_type);
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
                auto sub_it = conn_ctx.sub_by_track_alias.find(track_alias);
                if (sub_it == conn_ctx.sub_by_track_alias.end()) {
                    conn_ctx.metrics.rx_dgram_unknown_track_alias++;

                    SPDLOG_LOGGER_DEBUG(
                      logger_, "Received datagram to unknown subscribe track track alias: {0}, ignored", track_alias);

                    // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                    continue;
                }

                SPDLOG_LOGGER_TRACE(logger_,
                                    "Received object datagram conn_id: {0} data_ctx_id: {1} subscriber_id: {2} "
                                    "track_alias: {3} group_id: {4} object_id: {5} data size: {6}",
                                    conn_id,
                                    (data_ctx_id ? *data_ctx_id : 0),
                                    sub_id,
                                    track_alias,
                                    data.value()->size());

                auto& handler = sub_it->second;

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
        auto& conn = connections_[conn_id];

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
        const auto& conn = connections_[conn_id];
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

        for (const auto& [_, sub_h] : conn.tracks_by_request_id) {
            sub_h->MetricsSampled(sub_h->subscribe_track_metrics_);
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
            case messages::TerminationReason::kDupTrackAlias:
                log_msg << " duplicate track alias: " << reason_str;
                break;
            case messages::TerminationReason::kParamLengthMismatch:
                log_msg << " param length mismatch: " << reason_str;
                break;
            case messages::TerminationReason::kGoAwayTimeout:
                log_msg << " goaway timeout: " << reason_str;
                break;
        }

        SPDLOG_LOGGER_INFO(logger_, log_msg.str());

        quic_transport_->Close(conn_id, static_cast<uint64_t>(reason));

        if (client_mode_) {
            SPDLOG_LOGGER_INFO(logger_, "Client connection closed, stopping client");
            stop_ = true;
        }
    }

} // namespace moq
