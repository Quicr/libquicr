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

    void Server::ResolveAnnounce(ConnectionHandle, const TrackNamespace&, const AnnounceResponse&) {}

    void Server::SubscribeReceived(ConnectionHandle,
                                   uint64_t,
                                   uint64_t,
                                   const FullTrackName&,
                                   const SubscribeAttributes&)
    {
    }

    bool Server::FetchReceived(ConnectionHandle, uint64_t, const FullTrackName&, const FetchAttributes&)
    {
        return false;
    }

    void Server::ResolveSubscribe(ConnectionHandle, uint64_t, const SubscribeResponse&) {}

    void Server::BindPublisherTrack(TransportConnId conn_id,
                                    uint64_t subscribe_id,
                                    const std::shared_ptr<PublishTrackHandler>& track_handler)
    {
        // Generate track alias
        const auto& tfn = track_handler->GetFullTrackName();

        // Track hash is the track alias for now.
        auto th = TrackHash(tfn);

        track_handler->SetTrackAlias(th.track_fullname_hash);

        SPDLOG_LOGGER_INFO(
          logger_, "Bind subscribe track handler conn_id: {0} hash: {1}", conn_id, th.track_fullname_hash);

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
        track_handler->publish_object_func_ = [&, track_handler, subscribe_id = track_handler->GetSubscribeId()](
                                                uint8_t priority,
                                                uint32_t ttl,
                                                bool stream_header_needed,
                                                uint64_t group_id,
                                                uint64_t subgroup_id,
                                                uint64_t object_id,
                                                std::optional<Extensions> extensions,
                                                Span<uint8_t const> data) -> PublishTrackHandler::PublishObjectStatus {
            return SendObject(
              *track_handler, priority, ttl, stream_header_needed, group_id, subgroup_id, object_id, extensions, data);
        };

        // Hold onto track handler
        conn_it->second.pub_tracks_by_name[th.track_namespace_hash][th.track_name_hash] = track_handler;
        conn_it->second.pub_tracks_by_data_ctx_id[track_handler->publish_data_ctx_id_] = std::move(track_handler);

        lock.unlock();
        track_handler->SetStatus(PublishTrackHandler::Status::kOk);
    }

    bool Server::ProcessCtrlMessage(ConnectionContext& conn_ctx, BytesSpan msg_bytes)
    try {
        switch (*conn_ctx.ctrl_msg_type_received) {
            case messages::ControlMessageType::SUBSCRIBE: {
                messages::MoqSubscribe msg;
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
                                  tfn,
                                  { .priority = msg.priority, .group_order = msg.group_order });

                // TODO(tievens): Delay the subscribe OK till ResolveSubscribe() is called
                SendSubscribeOk(conn_ctx, msg.subscribe_id, kSubscribeExpires, false);

                return true;
            }
            case messages::ControlMessageType::SUBSCRIBE_OK: {
                messages::MoqSubscribeOk msg;
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
            case messages::ControlMessageType::SUBSCRIBE_ERROR: {
                messages::MoqSubscribeError msg;
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

                sub_it->second.get()->SetStatus(SubscribeTrackHandler::Status::kError);
                RemoveSubscribeTrack(conn_ctx, *sub_it->second);

                return true;
            }
            case messages::ControlMessageType::ANNOUNCE: {
                messages::MoqAnnounce msg;
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, {}, std::nullopt };

                AnnounceReceived(conn_ctx.connection_handle, tfn.name_space, {});

                // TODO(tievens): Delay announce OK till ResolveAnnounce() is called
                SendAnnounceOk(conn_ctx, msg.track_namespace);

                return true;
            }
            case messages::ControlMessageType::ANNOUNCE_ERROR: {
                messages::MoqAnnounceError msg;
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

            case messages::ControlMessageType::UNANNOUNCE: {
                messages::MoqUnannounce msg;
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, {}, std::nullopt };
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_INFO(logger_, "Received unannounce for namespace_hash: {0}", th.track_namespace_hash);

                UnannounceReceived(conn_ctx.connection_handle, tfn.name_space);

                return true;
            }

            case messages::ControlMessageType::UNSUBSCRIBE: {
                messages::MoqUnsubscribe msg;
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
            case messages::ControlMessageType::SUBSCRIBE_DONE: {
                messages::MoqSubscribeDone msg;
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

                return true;
            }
            case messages::ControlMessageType::ANNOUNCE_CANCEL: {
                messages::MoqAnnounceCancel msg;
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, {}, std::nullopt };
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_INFO(
                  logger_, "Received announce cancel for namespace_hash: {0}", th.track_namespace_hash);
                return true;
            }
            case messages::ControlMessageType::TRACK_STATUS_REQUEST: {
                messages::MoqTrackStatusRequest msg;
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, msg.track_name, std::nullopt };
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_INFO(logger_,
                                   "Received track status request for namespace_hash: {0} name_hash: {1}",
                                   th.track_namespace_hash,
                                   th.track_name_hash);
                return true;
            }
            case messages::ControlMessageType::TRACK_STATUS: {
                messages::MoqTrackStatus msg;
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, msg.track_name, std::nullopt };
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_INFO(logger_,
                                   "Received track status for namespace_hash: {0} name_hash: {1}",
                                   th.track_namespace_hash,
                                   th.track_name_hash);
                return true;
            }
            case messages::ControlMessageType::GOAWAY: {
                messages::MoqGoaway msg;
                msg_bytes >> msg;

                std::string new_sess_uri(msg.new_session_uri.begin(), msg.new_session_uri.end());
                SPDLOG_LOGGER_INFO(logger_, "Received goaway new session uri: {0}", new_sess_uri);
                return true;
            }
            case messages::ControlMessageType::CLIENT_SETUP: {
                messages::MoqClientSetup msg;

                msg_bytes >> msg;

                if (!msg.supported_versions.size()) { // should never happen
                    CloseConnection(conn_ctx.connection_handle,
                                    messages::MoqTerminationReason::PROTOCOL_VIOLATION,
                                    "Client setup contained zero versions");
                    return true;
                }

                std::string client_endpoint_id(msg.endpoint_id_parameter.value.begin(),
                                               msg.endpoint_id_parameter.value.end());

                ClientSetupReceived(
                  conn_ctx.connection_handle,
                  { { msg.endpoint_id_parameter.value.begin(), msg.endpoint_id_parameter.value.end() } });

                SPDLOG_LOGGER_INFO(
                  logger_,
                  "Client setup received conn_id: {0} from: {1} num_versions: {2} role: {3} version: {4}",
                  conn_ctx.connection_handle,
                  client_endpoint_id,
                  msg.num_versions,
                  static_cast<int>(msg.role_parameter.value.front()),
                  msg.supported_versions.front());

                conn_ctx.client_version = msg.supported_versions.front();

                // TODO(tievens): Revisit sending sever setup immediately or wait for something else from server
                SendServerSetup(conn_ctx);
                conn_ctx.setup_complete = true;

                return true;
            }
            case messages::ControlMessageType::FETCH: {
                messages::MoqFetch msg;
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, msg.track_name, std::nullopt };

                if (!FetchReceived(conn_ctx.connection_handle,
                                   msg.subscribe_id,
                                   tfn,
                                   { .priority = msg.priority,
                                     .group_order = msg.group_order,
                                     .start_group = msg.start_group,
                                     .start_object = msg.start_object,
                                     .end_group = msg.end_group,
                                     .end_object = msg.end_object })) {
                    SendFetchError(
                      conn_ctx, msg.subscribe_id, messages::FetchError::kTrackDoesNotExist, "Track does not exist");

                    return true;
                }

                auto th = TrackHash(tfn);
                conn_ctx.recv_sub_id[msg.subscribe_id] = { th.track_namespace_hash, th.track_name_hash };

                if (msg.subscribe_id > conn_ctx.current_subscribe_id) {
                    conn_ctx.current_subscribe_id = msg.subscribe_id + 1;
                }

                SendFetchOk(conn_ctx, msg.subscribe_id, msg.group_order, false, 0, 0);

                return true;
            }
            default:
                SPDLOG_LOGGER_ERROR(logger_,
                                    "Unsupported MOQT message type: {0}, bad stream",
                                    static_cast<uint64_t>(*conn_ctx.ctrl_msg_type_received));
                return false;

        } // End of switch(msg type)

    } catch (...) {
        SPDLOG_LOGGER_ERROR(logger_, "Unable to parse control message");
        CloseConnection(conn_ctx.connection_handle,
                        messages::MoqTerminationReason::PROTOCOL_VIOLATION,
                        "Control message cannot be parsed");
        return false;
    }

} // namespace quicr
