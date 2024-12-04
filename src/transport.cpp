// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/detail/transport.h"

#include <sstream>

namespace quicr {
    using namespace quicr::messages;

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
        } else {
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
    }

    Transport::Status Transport::Stop()
    {
        return Status();
    }

    void Transport::SendCtrlMsg(const ConnectionContext& conn_ctx, BytesSpan data)
    {
        if (not conn_ctx.ctrl_data_ctx_id) {
            CloseConnection(
              conn_ctx.connection_handle, MoqTerminationReason::PROTOCOL_VIOLATION, "Control bidir stream not created");
            return;
        }

        quic_transport_->Enqueue(
          conn_ctx.connection_handle, *conn_ctx.ctrl_data_ctx_id, data, 0, 2000, 0, { true, false, false, false });
    }

    void Transport::SendClientSetup()
    {
        auto client_setup = MoqClientSetup{};

        client_setup.num_versions = 1; // NOTE: Not used for encode, version vector size is used
        client_setup.supported_versions = { kMoqtVersion };
        client_setup.role_parameter.type = static_cast<uint64_t>(ParameterType::Role);
        client_setup.role_parameter.length = 0x1; // NOTE: not used for encode, size of value is used
        client_setup.role_parameter.value = { 0x03 };
        client_setup.endpoint_id_parameter.value.assign(client_config_.endpoint_id.begin(),
                                                        client_config_.endpoint_id.end());

        Bytes buffer;
        buffer.reserve(sizeof(MoqClientSetup));
        buffer << client_setup;

        auto& conn_ctx = connections_.begin()->second;

        SendCtrlMsg(conn_ctx, buffer);
    }

    void Transport::SendServerSetup(ConnectionContext& conn_ctx)
    {
        auto server_setup = MoqServerSetup{};

        server_setup.selection_version = { conn_ctx.client_version };
        server_setup.role_parameter.type = static_cast<uint64_t>(ParameterType::Role);
        server_setup.role_parameter.length = 0x1; // NOTE: not used for encode, size of value is used
        server_setup.role_parameter.value = { 0x03 };
        server_setup.endpoint_id_parameter.value.assign(server_config_.endpoint_id.begin(),
                                                        server_config_.endpoint_id.end());

        Bytes buffer;
        buffer.reserve(sizeof(MoqServerSetup));
        buffer << server_setup;

        SPDLOG_LOGGER_DEBUG(logger_, "Sending SERVER_SETUP to conn_id: {0}", conn_ctx.connection_handle);

        SendCtrlMsg(conn_ctx, buffer);
    }

    void Transport::SendAnnounce(ConnectionContext& conn_ctx, const TrackNamespace& track_namespace)
    {
        auto announce = MoqAnnounce{};

        announce.track_namespace = track_namespace;
        announce.params = {};

        Bytes buffer;
        buffer << announce;

        SPDLOG_LOGGER_DEBUG(logger_, "Sending ANNOUNCE to conn_id: {0}", conn_ctx.connection_handle);

        SendCtrlMsg(conn_ctx, buffer);
    }

    void Transport::SendAnnounceOk(ConnectionContext& conn_ctx, const TrackNamespace& track_namespace)
    {
        auto announce_ok = MoqAnnounceOk{};

        announce_ok.track_namespace = track_namespace;

        Bytes buffer;
        buffer << announce_ok;

        SPDLOG_LOGGER_DEBUG(logger_, "Sending ANNOUNCE OK to conn_id: {0}", conn_ctx.connection_handle);

        SendCtrlMsg(conn_ctx, buffer);
    }

    void Transport::SendUnannounce(ConnectionContext& conn_ctx, const TrackNamespace& track_namespace)
    {
        auto unannounce = MoqUnannounce{};

        unannounce.track_namespace = track_namespace;

        Bytes buffer;
        buffer << unannounce;

        SPDLOG_LOGGER_DEBUG(logger_, "Sending UNANNOUNCE to conn_id: {0}", conn_ctx.connection_handle);

        SendCtrlMsg(conn_ctx, buffer);
    }

    void Transport::SendSubscribe(ConnectionContext& conn_ctx,
                                  uint64_t subscribe_id,
                                  const FullTrackName& tfn,
                                  TrackHash th,
                                  messages::ObjectPriority priority,
                                  messages::GroupOrder group_order)
    {
        auto subscribe = MoqSubscribe{};
        subscribe.subscribe_id = subscribe_id;
        subscribe.track_alias = th.track_fullname_hash;
        subscribe.track_namespace = tfn.name_space;
        subscribe.track_name.assign(tfn.name.begin(), tfn.name.end());
        subscribe.priority = priority;
        subscribe.group_order = group_order;
        subscribe.filter_type = FilterType::LatestGroup;

        Bytes buffer;
        buffer.reserve(sizeof(MoqSubscribe));
        buffer << subscribe;

        SPDLOG_LOGGER_DEBUG(
          logger_,
          "Sending SUBSCRIBE to conn_id: {0} subscribe_id: {1} track namespace hash: {2} name hash: {3}",
          conn_ctx.connection_handle,
          subscribe_id,
          th.track_namespace_hash,
          th.track_name_hash);

        SendCtrlMsg(conn_ctx, buffer);
    }

    void Transport::SendSubscribeUpdate(quicr::Transport::ConnectionContext& conn_ctx,
                                        uint64_t subscribe_id,
                                        quicr::TrackHash th,
                                        messages::GroupId start_group_id,
                                        messages::ObjectId start_object_id,
                                        messages::GroupId end_group_id,
                                        messages::ObjectPriority priority)
    {
        auto subscribe_update = MoqSubscribeUpdate{};
        subscribe_update.subscribe_id = subscribe_id;
        subscribe_update.start_group = start_group_id;
        subscribe_update.start_object = start_object_id;
        subscribe_update.end_group = end_group_id;
        subscribe_update.priority = priority;

        Bytes buffer;
        buffer.reserve(sizeof(MoqSubscribeUpdate));
        buffer << subscribe_update;

        SPDLOG_LOGGER_DEBUG(
          logger_,
          "Sending SUBSCRIBE_UPDATe to conn_id: {0} subscribe_id: {1} track namespace hash: {2} name hash: {3}",
          conn_ctx.connection_handle,
          subscribe_id,
          th.track_namespace_hash,
          th.track_name_hash);

        SendCtrlMsg(conn_ctx, buffer);
    }

    void Transport::SendSubscribeOk(ConnectionContext& conn_ctx,
                                    uint64_t subscribe_id,
                                    uint64_t expires,
                                    bool content_exists)
    {
        auto subscribe_ok = MoqSubscribeOk{};
        subscribe_ok.subscribe_id = subscribe_id;
        subscribe_ok.expires = expires;
        subscribe_ok.content_exists = content_exists;

        Bytes buffer;
        buffer.reserve(sizeof(MoqSubscribeOk));
        buffer << subscribe_ok;

        SPDLOG_LOGGER_DEBUG(
          logger_, "Sending SUBSCRIBE OK to conn_id: {0} subscribe_id: {1}", conn_ctx.connection_handle, subscribe_id);

        SendCtrlMsg(conn_ctx, buffer);
    }

    void Transport::SendSubscribeDone(ConnectionContext& conn_ctx, uint64_t subscribe_id, const std::string& reason)
    {
        auto subscribe_done = MoqSubscribeDone{};
        subscribe_done.subscribe_id = subscribe_id;
        subscribe_done.reason_phrase.assign(reason.begin(), reason.end());
        subscribe_done.content_exists = false;

        Bytes buffer;
        buffer.reserve(sizeof(MoqSubscribeDone));
        buffer << subscribe_done;

        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending SUBSCRIBE DONE to conn_id: {0} subscribe_id: {1}",
                            conn_ctx.connection_handle,
                            subscribe_id);

        SendCtrlMsg(conn_ctx, buffer);
    }

    void Transport::SendUnsubscribe(ConnectionContext& conn_ctx, uint64_t subscribe_id)
    {
        auto unsubscribe = MoqUnsubscribe{};
        unsubscribe.subscribe_id = subscribe_id;

        Bytes buffer;
        buffer.reserve(sizeof(MoqUnsubscribe));
        buffer << unsubscribe;

        SPDLOG_LOGGER_DEBUG(
          logger_, "Sending UNSUBSCRIBE to conn_id: {0} subscribe_id: {1}", conn_ctx.connection_handle, subscribe_id);

        SendCtrlMsg(conn_ctx, buffer);
    }

    void Transport::SendSubscribeError(ConnectionContext& conn_ctx,
                                       [[maybe_unused]] uint64_t subscribe_id,
                                       uint64_t track_alias,
                                       SubscribeError error,
                                       const std::string& reason)
    {
        auto subscribe_err = MoqSubscribeError{};
        subscribe_err.subscribe_id = 0x1;
        subscribe_err.err_code = static_cast<uint64_t>(error);
        subscribe_err.track_alias = track_alias;
        subscribe_err.reason_phrase.assign(reason.begin(), reason.end());

        Bytes buffer;
        buffer.reserve(sizeof(MoqSubscribeError));
        buffer << subscribe_err;

        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending SUBSCRIBE ERROR to conn_id: {0} subscribe_id: {1} error code: {2} reason: {3}",
                            conn_ctx.connection_handle,
                            subscribe_id,
                            static_cast<int>(error),
                            reason);

        SendCtrlMsg(conn_ctx, buffer);
    }

    void Transport::SendFetch(ConnectionContext& conn_ctx,
                              uint64_t subscribe_id,
                              const FullTrackName& tfn,
                              messages::ObjectPriority priority,
                              messages::GroupOrder group_order,
                              messages::GroupId start_group,
                              messages::GroupId start_object,
                              messages::GroupId end_group,
                              messages::GroupId end_object)
    {
        MoqFetch fetch;
        fetch.subscribe_id = subscribe_id;
        fetch.track_namespace = tfn.name_space;
        fetch.track_name.assign(tfn.name.begin(), tfn.name.end());
        fetch.priority = priority;
        fetch.group_order = group_order;
        fetch.start_group = start_group;
        fetch.start_object = start_object;
        fetch.end_group = end_group;
        fetch.end_object = end_object;

        Bytes buffer;
        buffer.reserve(MoqFetch::SizeOf(fetch));
        buffer << fetch;

        SendCtrlMsg(conn_ctx, buffer);
    }

    void Transport::SendFetchCancel(ConnectionContext& conn_ctx, uint64_t subscribe_id)
    {
        MoqFetchCancel fetch_cancel;
        fetch_cancel.subscribe_id = subscribe_id;

        Bytes buffer;
        buffer.reserve(sizeof(MoqFetchCancel));
        buffer << fetch_cancel;

        SendCtrlMsg(conn_ctx, buffer);
    }

    void Transport::SendFetchOk(ConnectionContext& conn_ctx,
                                uint64_t subscribe_id,
                                messages::GroupOrder group_order,
                                bool end_of_track,
                                messages::GroupId largest_group,
                                messages::GroupId largest_object)
    {
        MoqFetchOk fetch_ok;
        fetch_ok.subscribe_id = subscribe_id;
        fetch_ok.group_order = group_order;
        fetch_ok.end_of_track = end_of_track;
        fetch_ok.largest_group = largest_group;
        fetch_ok.largest_object = largest_object;

        Bytes buffer;
        buffer.reserve(sizeof(MoqFetchOk));
        buffer << fetch_ok;

        SendCtrlMsg(conn_ctx, buffer);
    }

    void Transport::SendFetchError(ConnectionContext& conn_ctx,
                                   [[maybe_unused]] uint64_t subscribe_id,
                                   FetchError error,
                                   const std::string& reason)
    {
        auto fetch_err = MoqFetchError{};
        fetch_err.subscribe_id = 0x1;
        fetch_err.err_code = static_cast<uint64_t>(error);
        fetch_err.reason_phrase.assign(reason.begin(), reason.end());

        Bytes buffer;
        buffer.reserve(sizeof(MoqFetchError) + sizeof(reason.size()));
        buffer << fetch_err;

        SPDLOG_LOGGER_DEBUG(logger_,
                            "Sending FETCH ERROR to conn_id: {0} subscribe_id: {1} error code: {2} reason: {3}",
                            conn_ctx.connection_handle,
                            subscribe_id,
                            static_cast<int>(error),
                            reason);

        SendCtrlMsg(conn_ctx, buffer);
    }

    void Transport::SubscribeTrack(TransportConnId conn_id, std::shared_ptr<SubscribeTrackHandler> track_handler)
    {
        const auto& tfn = track_handler->GetFullTrackName();

        // Track hash is the track alias for now.
        // TODO(tievens): Evaluate; change hash to be more than 62 bits to avoid collisions
        auto th = TrackHash(tfn);

        track_handler->SetTrackAlias(th.track_fullname_hash);

        SPDLOG_LOGGER_INFO(logger_, "Subscribe track conn_id: {0} hash: {1}", conn_id, th.track_fullname_hash);

        std::lock_guard<std::mutex> _(state_mutex_);
        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Subscribe track conn_id: {0} does not exist.", conn_id);
            return;
        }

        auto sid = conn_it->second.current_subscribe_id++;

        SPDLOG_LOGGER_DEBUG(logger_, "subscribe id (from subscribe) to add to memory: {0}", sid);

        track_handler->SetSubscribeId(sid);

        auto priority = track_handler->GetPriority();
        auto group_order = track_handler->GetGroupOrder();

        // Set the track handler for pub/sub using _sub_pub_id, which is the subscribe Id in MOQT
        conn_it->second.tracks_by_sub_id[sid] = std::move(track_handler);

        SendSubscribe(conn_it->second, sid, tfn, th, priority, group_order);
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

        SPDLOG_LOGGER_DEBUG(
          logger_, "subscribe id (from subscribe) to add to memory: {0}", track_handler->GetSubscribeId().value());

        auto priority = track_handler->GetPriority();
        SendSubscribeUpdate(conn_it->second, track_handler->GetSubscribeId().value(), th, 0x0, 0x0, 0x0, priority);
    }

    void Transport::RemoveSubscribeTrack(ConnectionContext& conn_ctx,
                                         SubscribeTrackHandler& handler,
                                         bool remove_handler)
    {
        handler.SetStatus(SubscribeTrackHandler::Status::kNotSubscribed);

        auto subscribe_id = handler.GetSubscribeId();

        handler.SetSubscribeId(std::nullopt);

        if (subscribe_id.has_value()) {
            SendUnsubscribe(conn_ctx, *subscribe_id);

            SPDLOG_LOGGER_DEBUG(logger_, "Removed subscribe track subscribe id: {0}", *subscribe_id);

            if (remove_handler) {
                handler.SetStatus(SubscribeTrackHandler::Status::kNotConnected); // Set after remove subscribe track

                std::lock_guard<std::mutex> _(state_mutex_);
                conn_ctx.tracks_by_sub_id.erase(*subscribe_id);
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

        // Check if this published track is a new namespace or existing.
        auto pub_ns_it = conn_it->second.pub_tracks_by_name.find(th.track_namespace_hash);
        if (pub_ns_it != conn_it->second.pub_tracks_by_name.end()) {
            auto pub_n_it = pub_ns_it->second.find(th.track_name_hash);
            if (pub_n_it != pub_ns_it->second.end()) {
                // Send subscribe done if track has subscriber and is sending
                if (pub_n_it->second->GetStatus() == PublishTrackHandler::Status::kOk &&
                    pub_n_it->second->GetSubscribeId().has_value()) {
                    SPDLOG_LOGGER_INFO(
                      logger_,
                      "Unpublish track namespace hash: {0} track_name_hash: {1} track_alias: {2}, sending "
                      "subscribe_done",
                      th.track_namespace_hash,
                      th.track_name_hash,
                      th.track_fullname_hash);
                    SendSubscribeDone(conn_it->second, *pub_n_it->second->GetSubscribeId(), "Unpublish track");
                } else {
                    SPDLOG_LOGGER_INFO(logger_,
                                       "Unpublish track namespace hash: {0} track_name_hash: {1} track_alias: {2}",
                                       th.track_namespace_hash,
                                       th.track_name_hash,
                                       th.track_fullname_hash);
                }

                pub_n_it->second->publish_data_ctx_id_ = 0;

                lock.unlock();

                pub_n_it->second->SetStatus(PublishTrackHandler::Status::kNotAnnounced);

                lock.lock();

                pub_ns_it->second.erase(pub_n_it);
            }

            if (!pub_ns_it->second.size()) {
                SPDLOG_LOGGER_INFO(
                  logger_, "Unpublish namespace hash: {0}, has no tracks, sending unannounce", th.track_namespace_hash);

                SendUnannounce(conn_it->second, tfn.name_space);
                conn_it->second.pub_tracks_by_name.erase(pub_ns_it);
            }
        }
    }

    void Transport::PublishTrack(TransportConnId conn_id, std::shared_ptr<PublishTrackHandler> track_handler)
    {
        // Generate track alias
        auto tfn = track_handler->GetFullTrackName();

        // Track hash is the track alias for now.
        // TODO(tievens): Evaluate; change hash to be more than 62 bits to avoid collisions
        auto th = TrackHash(tfn);

        track_handler->SetTrackAlias(th.track_fullname_hash);

        SPDLOG_LOGGER_INFO(logger_, "Publish track conn_id: {0} hash: {1}", conn_id, th.track_fullname_hash);

        std::unique_lock<std::mutex> lock(state_mutex_);

        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Publish track conn_id: {0} does not exist.", conn_id);
            return;
        }

        // Check if this published track is a new namespace or existing.
        auto pub_ns_it = conn_it->second.pub_tracks_by_name.find(th.track_namespace_hash);
        if (pub_ns_it == conn_it->second.pub_tracks_by_name.end()) {
            SPDLOG_LOGGER_INFO(
              logger_, "Publish track has new namespace hash: {0} sending ANNOUNCE message", th.track_namespace_hash);

            lock.unlock();

            track_handler->SetStatus(PublishTrackHandler::Status::kPendingAnnounceResponse);

            lock.lock();

            SendAnnounce(conn_it->second, tfn.name_space);

        } else {
            auto pub_n_it = pub_ns_it->second.find(th.track_name_hash);
            if (pub_n_it == pub_ns_it->second.end()) {
                SPDLOG_LOGGER_INFO(logger_,
                                   "Publish track has new track namespace hash: {0} name hash: {1}",
                                   th.track_namespace_hash,
                                   th.track_name_hash);
            }
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
                            Span<const uint8_t> data) -> PublishTrackHandler::PublishObjectStatus {
            auto handler = weak_handler.lock();
            if (!handler) {
                return PublishTrackHandler::PublishObjectStatus::kInternalError;
            }
            return SendObject(
              *handler, priority, ttl, stream_header_needed, group_id, subgroup_id, object_id, extensions, data);
        };

        // Hold ref to track handler
        conn_it->second.pub_tracks_by_name[th.track_namespace_hash][th.track_name_hash] = track_handler;
        conn_it->second.pub_tracks_by_data_ctx_id[track_handler->publish_data_ctx_id_] = std::move(track_handler);
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

        auto sid = conn_it->second.current_subscribe_id++;

        SPDLOG_LOGGER_DEBUG(logger_, "subscribe id (from fetch) to add to memory: {0}", sid);

        track_handler->SetSubscribeId(sid);

        auto priority = track_handler->GetPriority();
        auto group_order = track_handler->GetGroupOrder();
        auto start_group = track_handler->GetStartGroup();
        auto start_object = track_handler->GetStartObject();
        auto end_group = track_handler->GetEndGroup();
        auto end_object = track_handler->GetEndObject();

        track_handler->SetStatus(FetchTrackHandler::Status::kPendingResponse);

        conn_it->second.tracks_by_sub_id[sid] = std::move(track_handler);

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

        const auto sub_id = track_handler->GetSubscribeId();
        if (!sub_id.has_value()) {
            return;
        }

        SendFetchCancel(conn_it->second, sub_id.value());

        track_handler->SetSubscribeId(std::nullopt);
        track_handler->SetStatus(FetchTrackHandler::Status::kNotConnected);
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
        if (!track_handler.GetTrackAlias().has_value()) {
            return PublishTrackHandler::PublishObjectStatus::kNotAnnounced;
        }

        if (!track_handler.GetSubscribeId().has_value()) {
            return PublishTrackHandler::PublishObjectStatus::kNoSubscribers;
        }

        ITransport::EnqueueFlags eflags;

        track_handler.object_msg_buffer_.clear();

        switch (track_handler.default_track_mode_) {
            case TrackMode::kDatagram: {
                MoqObjectDatagram object;
                object.group_id = group_id;
                object.object_id = object_id;
                object.priority = priority;
                object.subscribe_id = *track_handler.GetSubscribeId();
                object.track_alias = *track_handler.GetTrackAlias();
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

                    MoqStreamHeaderSubGroup subgroup_hdr;
                    subgroup_hdr.group_id = group_id;
                    subgroup_hdr.subgroup_id = subgroup_id;
                    subgroup_hdr.priority = priority;
                    subgroup_hdr.track_alias = *track_handler.GetTrackAlias();
                    subgroup_hdr.subscribe_id = *track_handler.GetSubscribeId();
                    track_handler.object_msg_buffer_ << subgroup_hdr;
                }

                MoqStreamSubGroupObject object;
                object.object_id = object_id;
                object.extensions = extensions;
                object.payload.assign(data.begin(), data.end());
                track_handler.object_msg_buffer_ << object;
                break;
            }
        }

        quic_transport_->Enqueue(track_handler.connection_handle_,
                                 track_handler.publish_data_ctx_id_,
                                 track_handler.object_msg_buffer_,
                                 priority,
                                 ttl,
                                 0,
                                 eflags);

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
        for (const auto& [sub_id, handler] : conn_ctx.tracks_by_sub_id) {
            RemoveSubscribeTrack(conn_ctx, *handler, false);
        }

        // Notify publish handlers of disconnect
        for (const auto& [data_ctx_id, handler] : conn_ctx.pub_tracks_by_data_ctx_id) {
            handler->SetStatus(PublishTrackHandler::Status::kNotConnected);
            handler->SetSubscribeId(std::nullopt);
        }

        conn_ctx.pub_tracks_by_data_ctx_id.clear();
        conn_ctx.pub_tracks_by_name.clear();
        conn_ctx.recv_sub_id.clear();
        conn_ctx.tracks_by_sub_id.clear();
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

                std::lock_guard<std::mutex> _(state_mutex_);
                connections_.erase(conn_it);
            }
        }

        ConnectionStatusChanged(conn_id, conn_status);
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
    {
        auto stream_buf = quic_transport_->GetStreamBuffer(conn_id, stream_id);
        if (stream_buf == nullptr) {
            return;
        }

        // TODO(tievens): Considering moving lock to here... std::lock_guard<std::mutex> _(state_mutex_);

        auto& conn_ctx = connections_[conn_id];

        if (is_bidir && not conn_ctx.ctrl_data_ctx_id) {
            if (not data_ctx_id) {
                CloseConnection(
                  conn_id, MoqTerminationReason::INTERNAL_ERROR, "Received bidir is missing data context");
                return;
            }
            conn_ctx.ctrl_data_ctx_id = data_ctx_id;
        }

        /*
         * Loop many times to continue to read objects. This loop should only continue if the current object is read and
         * has been completed and there is more data. If there isn't enough data to parse the message, the loop should
         * be stopped. The ProcessCtrlMessage() and ProcessStreamDataMessage() methods return **true** to indicate that
         * the loop should continue. They return **false** to indicate that there wasn't enough data and more data
         * should be provided.
         */
        for (int i = 0; i < kReadLoopMaxPerStream; i++) { // don't loop forever, especially on bad stream
            if (stream_buf->Empty()) {
                break;
            }

            // bidir is Control stream, data streams are unidirectional
            if (is_bidir) {
                if (not conn_ctx.ctrl_msg_type_received.has_value()) {
                    auto msg_type = stream_buf->DecodeUintV();

                    if (msg_type) {
                        conn_ctx.ctrl_msg_type_received = static_cast<ControlMessageType>(*msg_type);
                    } else {
                        break;
                    }
                }

                if (auto msg_bytes = stream_buf->DecodeBytes(); msg_bytes != std::nullopt) {
                    if (ProcessCtrlMessage(conn_ctx, *msg_bytes)) {
                        conn_ctx.ctrl_msg_type_received = std::nullopt; // Clear current type now that it's complete
                    } else {
                        conn_ctx.metrics.invalid_ctrl_stream_msg++;
                    }
                } else {
                    break;
                }
            }

            // Data stream, unidirectional
            else {
                if (!ProcessStreamDataMessage(conn_ctx, stream_buf)) {
                    break; // More data is needed, wait for next callback
                }
            }
        }
    }

    void Transport::OnRecvDgram(const TransportConnId& conn_id, std::optional<DataContextId> data_ctx_id)
    {
        MoqObjectDatagram object_datagram_out;
        for (int i = 0; i < kReadLoopMaxPerStream; i++) {
            auto data = quic_transport_->Dequeue(conn_id, data_ctx_id);
            if (data.has_value() && !data->empty()) {
                StreamBuffer<uint8_t> buffer;
                buffer.Push(data.value());

                auto msg_type = buffer.DecodeUintV();
                if (!msg_type || static_cast<DataMessageType>(*msg_type) != DataMessageType::OBJECT_DATAGRAM) {
                    SPDLOG_LOGGER_DEBUG(logger_,
                                        "Received datagram that is not message type OBJECT_DATAGRAM, dropping");
                    auto& conn_ctx = connections_[conn_id];
                    conn_ctx.metrics.rx_dgram_invalid_type++;
                    continue;
                }

                MoqObjectDatagram msg;
                if (buffer >> msg) {
                    ////TODO(tievens): Considering moving lock to here... std::lock_guard<std::mutex> _(state_mutex_);

                    auto& conn_ctx = connections_[conn_id];
                    auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                    if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                        conn_ctx.metrics.rx_dgram_unknown_subscribe_id++;

                        SPDLOG_LOGGER_DEBUG(logger_,
                                            "Received datagram to unknown subscribe track subscribe_id: {0}, ignored",
                                            msg.subscribe_id);

                        // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                        continue;
                    }

                    SPDLOG_LOGGER_TRACE(logger_,
                                        "Received object datagram conn_id: {0} data_ctx_id: {1} subscriber_id: {2} "
                                        "track_alias: {3} group_id: {4} object_id: {5} data size: {6}",
                                        conn_id,
                                        (data_ctx_id ? *data_ctx_id : 0),
                                        msg.subscribe_id,
                                        msg.track_alias,
                                        msg.group_id,
                                        msg.object_id,
                                        msg.payload.size());

                    auto& handler = sub_it->second;

                    handler->subscribe_track_metrics_.objects_received++;
                    handler->subscribe_track_metrics_.bytes_received += msg.payload.size();
                    handler->ObjectReceived(
                      {
                        msg.group_id,
                        msg.object_id,
                        0, // datagrams don't have subgroups
                        msg.payload.size(),
                        ObjectStatus::kAvailable,
                        msg.priority,
                        std::nullopt,
                        TrackMode::kDatagram,
                        msg.extensions,
                      },
                      msg.payload);

                } else {
                    auto& conn_ctx = connections_[conn_id];
                    conn_ctx.metrics.rx_dgram_decode_failed++;

                    SPDLOG_LOGGER_DEBUG(logger_,
                                        "Failed to decode datagram conn_id: {0} data_ctx_id: {1}",
                                        conn_id,
                                        (data_ctx_id ? *data_ctx_id : 0));
                }
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

        for (const auto& [_, sub_h] : conn.tracks_by_sub_id) {
            sub_h->MetricsSampled(sub_h->subscribe_track_metrics_);
        }
    }

    void Transport::OnNewDataContext(const ConnectionHandle&, const DataContextId&) {}

    void Transport::CloseConnection(TransportConnId conn_id,
                                    messages::MoqTerminationReason reason,
                                    const std::string& reason_str)
    {
        std::ostringstream log_msg;
        log_msg << "Closing conn_id: " << conn_id;
        switch (reason) {
            case MoqTerminationReason::NO_ERROR:
                log_msg << " no error";
                break;
            case MoqTerminationReason::INTERNAL_ERROR:
                log_msg << " internal error: " << reason_str;
                break;
            case MoqTerminationReason::UNAUTHORIZED:
                log_msg << " unauthorized: " << reason_str;
                break;
            case MoqTerminationReason::PROTOCOL_VIOLATION:
                log_msg << " protocol violation: " << reason_str;
                break;
            case MoqTerminationReason::DUP_TRACK_ALIAS:
                log_msg << " duplicate track alias: " << reason_str;
                break;
            case MoqTerminationReason::PARAM_LEN_MISMATCH:
                log_msg << " param length mismatch: " << reason_str;
                break;
            case MoqTerminationReason::GOAWAY_TIMEOUT:
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

    bool Transport::ProcessStreamDataMessage(ConnectionContext& conn_ctx,
                                             std::shared_ptr<SafeStreamBuffer<uint8_t>>& stream_buffer)
    {
        if (stream_buffer->Empty()) { // should never happen
            conn_ctx.metrics.rx_stream_buffer_error++;
            SPDLOG_LOGGER_DEBUG(logger_, "Stream buffer cannot be zero when parsing message type, bad stream");

            return false;
        }

        // Header not set, get the header for this stream or datagram
        DataMessageType data_type;

        auto dt = stream_buffer->GetAnyType();
        if (dt.has_value()) {
            data_type = static_cast<DataMessageType>(*dt);
        } else {
            auto val = stream_buffer->DecodeUintV();
            data_type = static_cast<DataMessageType>(*val);
            stream_buffer->SetAnyType(*val);
        }

        switch (data_type) {
            case messages::DataMessageType::STREAM_HEADER_SUBGROUP: {
                auto&& [msg, parsed] = ParseStreamData<MoqStreamHeaderSubGroup, MoqStreamSubGroupObject>(
                  stream_buffer, DataMessageType::STREAM_HEADER_SUBGROUP);

                if (!parsed) {
                    break;
                }

                auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                    conn_ctx.metrics.rx_dgram_unknown_subscribe_id++;
                    SPDLOG_LOGGER_ERROR(
                      logger_,
                      "Received stream_header_subgroup to unknown subscribe track subscribe_id: {0}, ignored",
                      msg.subscribe_id);

                    // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging
                    stream_buffer->ResetAnyB<MoqStreamSubGroupObject>();
                    return true;
                }

                auto& obj = stream_buffer->GetAnyB<MoqStreamSubGroupObject>();
                if (*stream_buffer >> obj) {
                    SPDLOG_LOGGER_TRACE(
                      logger_,
                      "Received stream_subgroup_object subscribe_id: {0} priority: {1} track_alias: {2} "
                      "group_id: {3} subgroup_id: {4} object_id: {5} data size: {6}",
                      msg.subscribe_id,
                      msg.priority,
                      msg.track_alias,
                      msg.group_id,
                      msg.subgroup_id,
                      obj.object_id,
                      obj.payload.size());

                    auto& handler = sub_it->second;

                    handler->subscribe_track_metrics_.objects_received++;
                    handler->subscribe_track_metrics_.bytes_received += obj.payload.size();
                    handler->ObjectReceived({ msg.group_id,
                                              obj.object_id,
                                              msg.subgroup_id,
                                              obj.payload.size(),
                                              obj.object_status,
                                              msg.priority,
                                              std::nullopt,
                                              TrackMode::kStream,
                                              obj.extensions },
                                            obj.payload);

                    stream_buffer->ResetAnyB<MoqStreamSubGroupObject>();
                    return true;
                }
                break;
            }
            default:
                // Process the stream object type
                conn_ctx.metrics.rx_stream_invalid_type++;

                SPDLOG_LOGGER_DEBUG(
                  logger_, "Unsupported MOQT data message type: {0}, bad stream", static_cast<uint64_t>(data_type));
                return false;
        }

        return false;
    }

    template<class MessageType>
    std::pair<MessageType&, bool> Transport::ParseDataMessage(std::shared_ptr<SafeStreamBuffer<uint8_t>>& stream_buffer,
                                                              DataMessageType msg_type)
    {
        if (!stream_buffer->AnyHasValue()) {
            SPDLOG_LOGGER_DEBUG(logger_,
                                "Received stream message (type = {0}), init stream buffer",
                                static_cast<std::uint64_t>(msg_type));
            stream_buffer->InitAny<MessageType>(static_cast<uint64_t>(msg_type));
        }

        auto& msg = stream_buffer->GetAny<MessageType>();
        if (*stream_buffer >> msg) {
            return { msg, true };
        }

        return { msg, false };
    }

    template<class HeaderType, class MessageType>
    std::pair<HeaderType&, bool> Transport::ParseStreamData(std::shared_ptr<SafeStreamBuffer<uint8_t>>& stream_buffer,
                                                            DataMessageType msg_type)
    {
        if (!stream_buffer->AnyHasValue()) {
            SPDLOG_LOGGER_DEBUG(
              logger_, "Received stream header (type = {0}), init stream buffer", static_cast<std::uint64_t>(msg_type));
            stream_buffer->InitAny<HeaderType>(static_cast<uint64_t>(msg_type));
        }

        auto& msg = stream_buffer->GetAny<HeaderType>();
        if (!stream_buffer->AnyHasValueB() && *stream_buffer >> msg) {
            stream_buffer->InitAnyB<MessageType>();
            return { msg, true };
        }

        return { msg, stream_buffer->AnyHasValueB() };
    }

    template std::pair<messages::MoqStreamHeaderSubGroup&, bool>
    Transport::ParseStreamData<messages::MoqStreamHeaderSubGroup, messages::MoqStreamSubGroupObject>(
      std::shared_ptr<SafeStreamBuffer<uint8_t>>& stream_buffer,
      DataMessageType msg_type);

} // namespace moq
