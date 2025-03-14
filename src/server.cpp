// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/server.h>

namespace quicr {
    Server::Status Server::Start()
    {
        return Transport::Start();
    }

    void Server::Stop()
    {
        stop_ = true;
        Transport::Stop();
    }

    void Server::NewConnectionAccepted(quicr::ConnectionHandle connection_handle, const ConnectionRemoteInfo& remote)
    {
        SPDLOG_LOGGER_DEBUG(
          logger_, "New connection conn_id: {0} remote ip: {1} port: {2}", connection_handle, remote.ip, remote.port);
    }

    void Server::ConnectionStatusChanged(ConnectionHandle, ConnectionStatus) {}

    void Server::MetricsSampled(ConnectionHandle, const ConnectionMetrics&) {}

    void Server::AnnounceReceived(ConnectionHandle, const TrackNamespace&, const PublishAnnounceAttributes&) {}

    std::pair<std::optional<messages::SubscribeAnnouncesErrorCode>, std::vector<TrackNamespace>>
    Server::SubscribeAnnouncesReceived(ConnectionHandle, const TrackNamespace&, const PublishAnnounceAttributes&)
    {
        return { std::nullopt, {} };
    }

    void Server::UnsubscribeAnnouncesReceived(ConnectionHandle, const TrackNamespace&) {}

    void Server::ResolveAnnounce(ConnectionHandle connection_handle,
                                 const TrackNamespace& track_namespace,
                                 const std::vector<ConnectionHandle>& subscribers,
                                 const AnnounceResponse& response)
    {
        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        switch (response.reason_code) {
            case AnnounceResponse::ReasonCode::kOk: {
                SendAnnounceOk(conn_it->second, track_namespace);

                for (const auto& sub_conn_handle : subscribers) {
                    auto it = connections_.find(sub_conn_handle);
                    if (it == connections_.end()) {
                        continue;
                    }

                    SendAnnounce(it->second, track_namespace);
                }
                break;
            }
            default: {
                // TODO: Send announce error
            }
        }
    }

    void Server::SubscribeReceived(ConnectionHandle,
                                   uint64_t,
                                   uint64_t,
                                   quicr::messages::FilterType,
                                   const FullTrackName&,
                                   const SubscribeAttributes&)
    {
    }

    bool Server::FetchReceived(ConnectionHandle, uint64_t, const FullTrackName&, const FetchAttributes&)
    {
        return false;
    }

    void Server::OnFetchOk(ConnectionHandle, uint64_t, const FullTrackName&, const FetchAttributes&) {}

    void Server::NewGroupRequested(ConnectionHandle, uint64_t, uint64_t) {}

    void Server::ResolveSubscribe(ConnectionHandle connection_handle,
                                  uint64_t subscribe_id,
                                  const SubscribeResponse& subscribe_response)
    {
        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        switch (subscribe_response.reason_code) {
            case SubscribeResponse::ReasonCode::kOk: {
                SendSubscribeOk(
                  conn_it->second,
                  subscribe_id,
                  kSubscribeExpires,
                  subscribe_response.largest_group.has_value() && subscribe_response.largest_object.has_value(),
                  subscribe_response.largest_group.has_value() ? subscribe_response.largest_group.value() : 0,
                  subscribe_response.largest_object.has_value() ? subscribe_response.largest_object.value() : 0);
                break;
            }
            case SubscribeResponse::ReasonCode::kRetryTrackAlias: {
                if (subscribe_response.track_alias.has_value()) {
                    SendSubscribeError(conn_it->second,
                                       subscribe_id,
                                       *subscribe_response.track_alias,
                                       messages::SubscribeErrorCode::kRetryTrackAlias,
                                       subscribe_response.reason_phrase.has_value() ? *subscribe_response.reason_phrase
                                                                                    : "internal error");
                } else {
                    SendSubscribeError(conn_it->second,
                                       subscribe_id,
                                       {},
                                       messages::SubscribeErrorCode::kInternalError,
                                       "Missing track alias");
                }
                break;
            }
            default:
                SendSubscribeError(
                  conn_it->second, subscribe_id, {}, messages::SubscribeErrorCode::kInternalError, "Internal error");
                break;
        }
    }

    void Server::UnbindPublisherTrack(ConnectionHandle connection_handle,
                                      const std::shared_ptr<PublishTrackHandler>& track_handler)
    {
        std::unique_lock lock(state_mutex_);

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

        conn_it->second.pub_tracks_by_name[th.track_namespace_hash].erase(th.track_name_hash);
        conn_it->second.pub_tracks_by_track_alias.erase(th.track_fullname_hash);

        if (conn_it->second.pub_tracks_by_name.count(th.track_namespace_hash) == 0) {
            SPDLOG_LOGGER_DEBUG(logger_,
                                "Server publish track conn_id: {} full_name_hash: {} namespace_hash: {} unbind",
                                connection_handle,
                                th.track_fullname_hash,
                                th.track_namespace_hash);

            conn_it->second.pub_tracks_by_name.erase(th.track_namespace_hash);
        }

        conn_it->second.pub_tracks_by_data_ctx_id.erase(track_handler->publish_data_ctx_id_);
    }

    void Server::BindPublisherTrack(TransportConnId conn_id,
                                    uint64_t subscribe_id,
                                    const std::shared_ptr<PublishTrackHandler>& track_handler,
                                    bool ephemeral)
    {
        // Generate track alias
        const auto& tfn = track_handler->GetFullTrackName();

        // Track hash is the track alias for now.
        auto th = TrackHash(tfn);

        if (not track_handler->GetTrackAlias().has_value()) {
            track_handler->SetTrackAlias(th.track_fullname_hash);
        }

        SPDLOG_LOGGER_INFO(logger_,
                           "Bind subscribe track handler conn_id: {0} hash: {1}",
                           conn_id,
                           track_handler->GetTrackAlias().value());

        std::unique_lock<std::mutex> lock(state_mutex_);
        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Subscribe track conn_id: {0} does not exist.", conn_id);
            return;
        }

        track_handler->SetSubscribeId(subscribe_id);

        track_handler->connection_handle_ = conn_id;

        track_handler->publish_data_ctx_id_ =
          quic_transport_->CreateDataContext(conn_id,
                                             track_handler->default_track_mode_ == TrackMode::kDatagram ? false : true,
                                             track_handler->default_priority_,
                                             false);

        // Setup the function for the track handler to use to send objects with thread safety
        std::weak_ptr weak_track_handler(track_handler);
        track_handler->publish_object_func_ =
          [&, weak_track_handler](uint8_t priority,
                                  uint32_t ttl,
                                  bool stream_header_needed,
                                  uint64_t group_id,
                                  uint64_t subgroup_id,
                                  uint64_t object_id,
                                  std::optional<Extensions> extensions,
                                  Span<uint8_t const> data) -> PublishTrackHandler::PublishObjectStatus {
            auto th = weak_track_handler.lock();
            if (!th) {
                return PublishTrackHandler::PublishObjectStatus::kInternalError;
            }

            return SendObject(
              *th, priority, ttl, stream_header_needed, group_id, subgroup_id, object_id, extensions, data);
        };

        track_handler->forward_publish_data_func_ =
          [&, weak_track_handler](
            uint8_t priority,
            uint32_t ttl,
            bool stream_header_needed,
            std::shared_ptr<const std::vector<uint8_t>> data) -> PublishTrackHandler::PublishObjectStatus {
            if (auto handler = weak_track_handler.lock()) {
                return SendData(*handler, priority, ttl, stream_header_needed, data);
            }
            return PublishTrackHandler::PublishObjectStatus::kInternalError;
        };

        if (!ephemeral) {
            // Hold onto track handler
            conn_it->second.pub_tracks_by_name[th.track_namespace_hash][th.track_name_hash] = track_handler;
            conn_it->second.pub_tracks_by_track_alias[th.track_fullname_hash] = track_handler;
            conn_it->second.pub_tracks_by_data_ctx_id[track_handler->publish_data_ctx_id_] = std::move(track_handler);
        }

        lock.unlock();
        track_handler->SetStatus(PublishTrackHandler::Status::kOk);
    }

    bool Server::ProcessCtrlMessage(ConnectionContext& conn_ctx, BytesSpan msg_bytes)
    try {
        switch (*conn_ctx.ctrl_msg_type_received) {
            case messages::ControlMessageType::kSubscribe: {
                messages::Subscribe msg;
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, msg.track_name, std::nullopt };
                auto th = TrackHash(tfn);

                conn_ctx.recv_sub_id[msg.subscribe_id] = { th.track_namespace_hash, th.track_name_hash };

                if (msg.subscribe_id > conn_ctx.current_subscribe_id) {
                    conn_ctx.current_subscribe_id = msg.subscribe_id + 1;
                }

                // TODO(tievens): add filter type when caching supports it
                SubscribeReceived(conn_ctx.connection_handle,
                                  msg.subscribe_id,
                                  msg.track_alias,
                                  msg.filter_type,
                                  tfn,
                                  { .priority = msg.priority, .group_order = msg.group_order });

                return true;
            }
            case messages::ControlMessageType::kSubscribeOk: {
                messages::SubscribeOk msg;
                msg_bytes >> msg;

                auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);

                if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                    SPDLOG_LOGGER_WARN(
                      logger_,
                      "Received subscribe ok to unknown subscribe track conn_id: {0} subscribe_id: {1}, ignored",
                      conn_ctx.connection_handle,
                      msg.subscribe_id);

                    // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race
                    // condition
                    return true;
                }

                sub_it->second.get()->SetStatus(SubscribeTrackHandler::Status::kOk);

                return true;
            }
            case messages::ControlMessageType::kSubscribeError: {
                messages::SubscribeError msg;
                msg_bytes >> msg;

                auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);

                if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                    SPDLOG_LOGGER_WARN(
                      logger_,
                      "Received subscribe error to unknown subscribe_id conn_id: {0} subscribe_id: {1}, ignored",
                      conn_ctx.connection_handle,
                      msg.subscribe_id);

                    // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race
                    // condition
                    return true;
                }

                sub_it->second->SetStatus(SubscribeTrackHandler::Status::kError);
                RemoveSubscribeTrack(conn_ctx, *sub_it->second);

                return true;
            }
            case messages::ControlMessageType::kAnnounce: {
                messages::Announce msg;
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, {}, std::nullopt };

                AnnounceReceived(conn_ctx.connection_handle, tfn.name_space, {});
                return true;
            }

            case messages::ControlMessageType::kSubscribeAnnounces: {
                messages::SubscribeAnnounces msg;
                msg_bytes >> msg;

                const auto& [err, matched_ns] =
                  SubscribeAnnouncesReceived(conn_ctx.connection_handle, msg.prefix_namespace, {});
                if (err.has_value()) {
                    SendSubscribeAnnouncesError(conn_ctx, msg.prefix_namespace, *err, {});
                } else {
                    for (const auto& ns : matched_ns) {
                        SendAnnounce(conn_ctx, ns);
                    }
                }

                return true;
            }

            case messages::ControlMessageType::kUnsubscribeAnnounces: {
                messages::UnsubscribeAnnounces msg;
                msg_bytes >> msg;

                UnsubscribeAnnouncesReceived(conn_ctx.connection_handle, msg.prefix_namespace);
            }

            case messages::ControlMessageType::kAnnounceError: {
                messages::AnnounceError msg;
                msg_bytes >> msg;

                if (msg.track_namespace) {
                    std::string reason = "unknown";
                    auto tfn = FullTrackName{ *msg.track_namespace, {}, std::nullopt };
                    auto th = TrackHash(tfn);

                    if (msg.reason_phrase) {
                        reason.assign(msg.reason_phrase->begin(), msg.reason_phrase->end());
                    }

                    SPDLOG_LOGGER_INFO(logger_,
                                       "Received announce error for namespace_hash: {0} error code: {1} reason: {2}",
                                       th.track_namespace_hash,
                                       (msg.err_code.has_value() ? *msg.err_code : 0),
                                       reason);
                }
                return true;
            }

            case messages::ControlMessageType::kUnannounce: {
                messages::Unannounce msg;
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, {}, std::nullopt };
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_INFO(logger_, "Received unannounce for namespace_hash: {0}", th.track_namespace_hash);

                auto sub_anno_conns = UnannounceReceived(conn_ctx.connection_handle, tfn.name_space);

                std::lock_guard<std::mutex> _(state_mutex_);
                for (auto conn_id : sub_anno_conns) {
                    auto conn_it = connections_.find(conn_id);
                    if (conn_it == connections_.end()) {
                        continue;
                    }

                    SendUnannounce(conn_it->second, msg.track_namespace);
                }

                return true;
            }

            case messages::ControlMessageType::kUnsubscribe: {
                messages::Unsubscribe msg;
                msg_bytes >> msg;

                const auto& [name_space, name] = conn_ctx.recv_sub_id[msg.subscribe_id];
                TrackHash th(name_space, name);
                if (auto pdt = GetPubTrackHandler(conn_ctx, th)) {
                    pdt->SetStatus(PublishTrackHandler::Status::kNoSubscribers);
                }

                UnsubscribeReceived(conn_ctx.connection_handle, msg.subscribe_id);
                conn_ctx.recv_sub_id.erase(msg.subscribe_id);

                return true;
            }
            case messages::ControlMessageType::kSubscribeDone: {
                messages::SubscribeDone msg;
                msg_bytes >> msg;

                auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                    SPDLOG_LOGGER_WARN(logger_,
                                       "Received subscribe done to unknown subscribe_id conn_id: {0} subscribe_id: {1}",
                                       conn_ctx.connection_handle,
                                       msg.subscribe_id);

                    // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race
                    // condition
                    return true;
                }
                auto tfn = sub_it->second->GetFullTrackName();
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_INFO(logger_,
                                   "Received subscribe done conn_id: {0} subscribe_id: {1} track namespace hash: {2} "
                                   "name hash: {3} track alias: {4}",
                                   conn_ctx.connection_handle,
                                   msg.subscribe_id,
                                   th.track_namespace_hash,
                                   th.track_name_hash,
                                   th.track_fullname_hash);

                sub_it->second.get()->SetStatus(SubscribeTrackHandler::Status::kNotSubscribed);

                UnsubscribeReceived(conn_ctx.connection_handle, msg.subscribe_id);
                conn_ctx.recv_sub_id.erase(msg.subscribe_id);

                return true;
            }
            case messages::ControlMessageType::kSubscribesBlocked: {
                messages::SubscribesBlocked msg;
                msg_bytes >> msg;

                SPDLOG_LOGGER_WARN(logger_, "Subscribe was blocked, maximum_subscribe_id: {}", msg.max_subscribe_id);

                // TODO: React to this somehow.
                // See https://www.ietf.org/archive/id/draft-ietf-moq-transport-08.html#section-7.21
                // A publisher MAY send a MAX_SUBSCRIBE_ID upon receipt of SUBSCRIBES_BLOCKED, but it MUST NOT rely on
                // SUBSCRIBES_BLOCKED to trigger sending a MAX_SUBSCRIBE_ID, because sending SUBSCRIBES_BLOCKED is not
                // required.

                return true;
            }
            case messages::ControlMessageType::kAnnounceCancel: {
                messages::AnnounceCancel msg;
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, {}, std::nullopt };
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_INFO(
                  logger_, "Received announce cancel for namespace_hash: {0}", th.track_namespace_hash);
                return true;
            }
            case messages::ControlMessageType::kTrackStatusRequest: {
                messages::TrackStatusRequest msg;
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, msg.track_name, std::nullopt };
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_INFO(logger_,
                                   "Received track status request for namespace_hash: {0} name_hash: {1}",
                                   th.track_namespace_hash,
                                   th.track_name_hash);
                return true;
            }
            case messages::ControlMessageType::kTrackStatus: {
                messages::TrackStatus msg;
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, msg.track_name, std::nullopt };
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_INFO(logger_,
                                   "Received track status for namespace_hash: {0} name_hash: {1}",
                                   th.track_namespace_hash,
                                   th.track_name_hash);
                return true;
            }
            case messages::ControlMessageType::kGoAway: {
                messages::GoAway msg;
                msg_bytes >> msg;

                std::string new_sess_uri(msg.new_session_uri.begin(), msg.new_session_uri.end());
                SPDLOG_LOGGER_INFO(logger_, "Received goaway new session uri: {0}", new_sess_uri);
                return true;
            }
            case messages::ControlMessageType::kClientSetup: {
                messages::ClientSetup msg;

                msg_bytes >> msg;

                if (!msg.supported_versions.size()) { // should never happen
                    CloseConnection(conn_ctx.connection_handle,
                                    messages::TerminationReason::kProtocolViolation,
                                    "Client setup contained zero versions");
                    return true;
                }

                std::string client_endpoint_id(msg.endpoint_id_parameter.value.begin(),
                                               msg.endpoint_id_parameter.value.end());

                ClientSetupReceived(
                  conn_ctx.connection_handle,
                  { { msg.endpoint_id_parameter.value.begin(), msg.endpoint_id_parameter.value.end() } });

                SPDLOG_LOGGER_INFO(logger_,
                                   "Client setup received conn_id: {} from: {} num_versions: {} version: {}",
                                   conn_ctx.connection_handle,
                                   client_endpoint_id,
                                   msg.num_versions,
                                   msg.supported_versions.front());

                conn_ctx.client_version = msg.supported_versions.front();

                // TODO(tievens): Revisit sending sever setup immediately or wait for something else from server
                SendServerSetup(conn_ctx);
                conn_ctx.setup_complete = true;

                return true;
            }
            case messages::ControlMessageType::kFetch: {
                messages::Fetch msg;
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, msg.track_name, std::nullopt };
                const FetchAttributes attrs{
                    .priority = msg.priority,
                    .group_order = msg.group_order,
                    .start_group = msg.start_group,
                    .start_object = msg.start_object,
                    .end_group = msg.end_group,
                    .end_object = msg.end_object > 0 ? std::optional(msg.end_object - 1) : std::nullopt,
                };

                if (!FetchReceived(conn_ctx.connection_handle, msg.subscribe_id, tfn, attrs)) {
                    SendFetchError(
                      conn_ctx, msg.subscribe_id, messages::FetchErrorCode::kTrackDoesNotExist, "Track does not exist");

                    return true;
                }

                auto th = TrackHash(tfn);
                conn_ctx.recv_sub_id[msg.subscribe_id] = { th.track_namespace_hash, th.track_name_hash };

                if (msg.subscribe_id > conn_ctx.current_subscribe_id) {
                    conn_ctx.current_subscribe_id = msg.subscribe_id + 1;
                }

                SendFetchOk(conn_ctx, msg.subscribe_id, msg.group_order, false, 0, 0);
                OnFetchOk(conn_ctx.connection_handle, msg.subscribe_id, tfn, attrs);

                return true;
            }
            case messages::ControlMessageType::kFetchCancel: {
                messages::FetchCancel msg;
                msg_bytes >> msg;

                if (conn_ctx.recv_sub_id.find(msg.subscribe_id) == conn_ctx.recv_sub_id.end()) {
                    SPDLOG_LOGGER_WARN(
                      logger_, "Received Fetch Cancel for unknown subscribe ID: {0}", msg.subscribe_id);
                }

                FetchCancelReceived(conn_ctx.connection_handle, msg.subscribe_id);
                conn_ctx.recv_sub_id.erase(msg.subscribe_id);

                return true;
            }
            case messages::ControlMessageType::kNewGroup: {
                messages::NewGroupRequest msg;
                msg_bytes >> msg;

                NewGroupRequested(conn_ctx.connection_handle, msg.subscribe_id, msg.track_alias);

                return true;
            }
            default:
                SPDLOG_LOGGER_ERROR(logger_,
                                    "Unsupported MOQT message type: {0}, bad stream",
                                    static_cast<uint64_t>(*conn_ctx.ctrl_msg_type_received));
                return false;

        } // End of switch(msg type)

    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_,
                            "Unable to parse {} control message: {}",
                            static_cast<uint64_t>(*conn_ctx.ctrl_msg_type_received),
                            e.what());
        CloseConnection(conn_ctx.connection_handle,
                        messages::TerminationReason::kProtocolViolation,
                        "Control message cannot be parsed");
        return false;
    } catch (...) {
        SPDLOG_LOGGER_ERROR(logger_, "Unable to parse control message");
        CloseConnection(conn_ctx.connection_handle,
                        messages::TerminationReason::kProtocolViolation,
                        "Control message cannot be parsed");
        return false;
    }

} // namespace quicr
