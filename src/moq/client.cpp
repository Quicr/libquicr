/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#include <moq/client.h>

namespace moq {

    Client::Status Client::Connect()
    {
        Transport::Start();
        return Status::kConnecting;
    }

    Client::Status Client::Disconnect()
    {
        Transport::Stop();
        return Status::kDisconnecting;
    }

    Client::ControlMessage Client::ProcessCtrlMessage(ConnectionContext& conn_ctx,
                                              std::shared_ptr<StreamBuffer<uint8_t>>& stream_buffer)
    {

        ControlMessage ctrl_msg;
        ctrl_msg.status = ControlMessageStatus::kMessageIncomplete;

        if (last_control_message_complete_) {
            stream_buffer->ResetAny();
            last_control_message_complete_ = false;
        }

        if (stream_buffer->Size() == 0) { // should never happen
            ctrl_msg.status = ControlMessageStatus::kStreamBufferCannotBeZero;
            return ctrl_msg;
            /*
                        CloseConnection(conn_ctx.connection_handle,
                                        MoqTerminationReason::INTERNAL_ERROR,
                                        "Stream buffer cannot be zero when parsing message type");
            */
        }

        if (not conn_ctx.ctrl_msg_type_received) { // should never happen
            ctrl_msg.status = ControlMessageStatus::kStreamBufferMissingType;
            return ctrl_msg;

            /*
            CloseConnection(conn_ctx.connection_handle,
                            MoqTerminationReason::INTERNAL_ERROR,
                            "Process recv message connection context is missing message type");
            */
        }

        ctrl_msg.message_type = *conn_ctx.ctrl_msg_type_received;

        switch (*conn_ctx.ctrl_msg_type_received) {
            case messages::MoqMessageType::SUBSCRIBE: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqSubscribe>(ctrl_msg, stream_buffer);
                if (parsed) {
                    /*
                    auto tfn = FullTrackName{ msg.track_namespace, msg.track_name, std::nullopt };
                    auto th = TrackHash(tfn);

                    if (msg.subscribe_id > conn_ctx.current_subscribe_id) {
                        conn_ctx.current_subscribe_id = msg.subscribe_id + 1;
                    }

                    // For client/publisher, notify track that there is a subscriber
                    if (client_mode_) {
                        auto ptd = GetPubTrackHandler(conn_ctx, th);
                        if (not ptd.has_value()) {
                            LOGGER_WARN(logger_,
                                        "Received subscribe unknown publish track conn_id: {0} namespace hash: {1} "
                                        "name hash: {2}",
                                        conn_ctx.connection_handle,
                                        th.track_namespace_hash,
                                        th.track_name_hash);

                            SendSubscribeError(conn_ctx,
                                               msg.subscribe_id,
                                               msg.track_alias,
                                               SubscribeError::TRACK_NOT_EXIST,
                                               "Published track not found");
                            return true;
                        }

                        SendSubscribeOk(conn_ctx, msg.subscribe_id, kSubscribeExpires, false);

                        LOGGER_DEBUG(logger_,
                                     "Received subscribe to announced track alias: {0} recv subscribe_id: {1}, setting "
                                     "send state to ready",
                                     msg.track_alias,
                                     msg.subscribe_id);

                        // Indicate send is ready upon subscribe
                        // TODO(tievens): Maybe needs a delay as subscriber may have not received ok before data is sent
                        auto ptd_l = ptd->lock();
                        ptd_l->SetSubscribeId(msg.subscribe_id);
                        ptd_l->SetStatus(PublishTrackHandler::Status::kOk);
                        ptd_l->StatusChanged(PublishTrackHandler::Status::kOk);

                        conn_ctx.recv_sub_id[msg.subscribe_id] = { th.track_namespace_hash, th.track_name_hash };

                    } else { // Server mode
                        // TODO(tievens): add filter type when caching supports it
                        if (_delegate->cb_subscribe(
                              conn_ctx.connection_handle, msg.subscribe_id, tfn.name_space, tfn.name)) {
                            SendSubscribeOk(conn_ctx, msg.subscribe_id, MOQ_SUBSCRIBE_EXPIRES, false);
                        }
                    }
                    */
                    stream_buffer->ResetAny();
                }
                break;
            }
            case messages::MoqMessageType::SUBSCRIBE_OK: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqSubscribeOk>(ctrl_msg, stream_buffer);
                if (parsed) {
                    /*
                    auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);

                    if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                        LOGGER_WARN(
                          logger_,
                          "Received subscribe ok to unknown subscribe track conn_id: {0} subscribe_id: {1}, ignored",
                          conn_ctx.connection_handle,
                          msg.subscribe_id);

                        // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race
                        // condition
                        stream_buffer->ResetAny();
                        return true;
                    }

                    sub_it->second.get()->cb_readReady();
                    */
                    stream_buffer->ResetAny();
                }
                break;

            }
            case messages::MoqMessageType::SUBSCRIBE_ERROR: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqSubscribeError>(ctrl_msg, stream_buffer);
                if (parsed) {
                    /*
                    auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);

                    if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                        LOGGER_WARN(
                          logger_,
                          "Received subscribe error to unknown subscribe_id conn_id: {0} subscribe_id: {1}, ignored",
                          conn_ctx.connection_handle,
                          msg.subscribe_id);

                        // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race
                        // condition
                        stream_buffer->ResetAny();
                        return true;
                    }

                    sub_it->second.get()->cb_readNotReady(MoqTrackDelegate::TrackReadStatus::SUBSCRIBE_ERROR);
                    RemoveSubscribeTrack(conn_ctx, *sub_it->second);

                    */
                    stream_buffer->ResetAny();
                }
                break;
            }
            case messages::MoqMessageType::ANNOUNCE: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqAnnounce>(ctrl_msg, stream_buffer);
                if (parsed) {
                    /*
                    auto tfn = FullTrackName{ msg.track_namespace, {} };
                    auto th = TrackHash(tfn);

                    if (_delegate->cb_announce(conn_ctx.connection_handle, th.track_namespace_hash)) {
                        SendAnnounceOk(conn_ctx, msg.track_namespace);
                        _delegate->cb_announce_post(conn_ctx.connection_handle, th.track_namespace_hash);
                    }

                    */
                    stream_buffer->ResetAny();
                }
                break;
            }
            case messages::MoqMessageType::ANNOUNCE_OK: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqAnnounceOk>(ctrl_msg, stream_buffer);
                if (parsed) {
                    /*
                    auto tfn = FullTrackName{ msg.track_namespace, {} };
                    auto th = TrackHash(tfn);
                    LOGGER_DEBUG(logger_,
                                 "Received announce ok, conn_id: {0} namespace_hash: {1}",
                                 conn_ctx.connection_handle,
                                 th.track_namespace_hash);

                    // Update each track to indicate status is okay to publish
                    auto pub_it = conn_ctx.pub_tracks_by_name.find(th.track_namespace_hash);
                    for (const auto& td : pub_it->second) {
                        if (td.second.get()->GetSendStatus() != MoqTrackDelegate::TrackSendStatus::OK)
                            td.second.get()->SetSendStatus(MoqTrackDelegate::TrackSendStatus::NO_SUBSCRIBERS);
                    }

                    */
                    stream_buffer->ResetAny();
                }
                break;
            }
            case messages::MoqMessageType::ANNOUNCE_ERROR: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqAnnounceError>(ctrl_msg, stream_buffer);
                if (parsed) {
                    /*
                    if (msg.track_namespace) {
                        std::string reason = "unknown";
                        auto tfn = FullTrackName{ *msg.track_namespace, {} };
                        auto th = TrackHash(tfn);

                        if (msg.reason_phrase) {
                            reason.assign(msg.reason_phrase->begin(), msg.reason_phrase->end());
                        }

                        LOGGER_INFO(logger_,
                                    "Received announce error for namespace_hash: {0} error code: {1} reason: {2}",
                                    th.track_namespace_hash,
                                    (msg.err_code.has_value() ? *msg.err_code : 0),
                                    reason);
                    } */
                    stream_buffer->ResetAny();
                }
                break;
            }
            case messages::MoqMessageType::UNANNOUNCE: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqUnannounce>(ctrl_msg, stream_buffer);
                if (parsed) {
                    /*
                    auto tfn = FullTrackName{ msg.track_namespace, {} };
                    auto th = TrackHash(tfn);

                    LOGGER_INFO(logger_, "Received unannounce for namespace_hash: {0}", th.track_namespace_hash);

                    _delegate->cb_unannounce(conn_ctx.connection_handle, th.track_namespace_hash, std::nullopt);
                    */
                    stream_buffer->ResetAny();
                }
                break;
            }
            case messages::MoqMessageType::UNSUBSCRIBE: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqUnsubscribe>(ctrl_msg, stream_buffer);
                if (parsed) {
                    /*
                    if (!client_mode_) {
                        auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                        if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                            LOGGER_WARN(
                              logger_,
                              "Received unsubscribe to unknown subscribe_id conn_id: {0} subscribe_id: {1}, ignored",
                              conn_ctx.connection_handle,
                              msg.subscribe_id);

                            // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to
                            // race condition
                            stream_buffer->ResetAny();
                            return true;
                        }

                        LOGGER_DEBUG(logger_,
                                     "Received unsubscribe conn_id: {0} subscribe_id: {1}"
                                       << conn_ctx.connection_handle,
                                     msg.subscribe_id);
                        sub_it->second.get()->cb_readNotReady(MoqTrackDelegate::TrackReadStatus::NOT_SUBSCRIBED);

                        _delegate->cb_unsubscribe(conn_ctx.connection_handle, msg.subscribe_id);

                        RemoveSubscribeTrack(conn_ctx, *sub_it->second);
                    } else {
                        const auto& [name_space, name] = conn_ctx.recv_sub_id[msg.subscribe_id];
                        TrackHash th(name_space, name);
                        if (auto pdt = GetPubTrackHandler(conn_ctx, th)) {
                            pdt->lock()->SetSendStatus(MoqTrackDelegate::TrackSendStatus::NO_SUBSCRIBERS);
                            pdt->lock()->cb_sendNotReady(MoqTrackDelegate::TrackSendStatus::NO_SUBSCRIBERS);
                        }

                        conn_ctx.recv_sub_id.erase(msg.subscribe_id);
                    }
                    */
                    stream_buffer->ResetAny();
                }
                break;
            }
            case messages::MoqMessageType::SUBSCRIBE_DONE: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqSubscribeDone>(ctrl_msg, stream_buffer);
                if (parsed) {
                    /*
                    auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                    if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                        LOGGER_WARN(logger_,
                                    "Received subscribe done to unknown subscribe_id conn_id: {0} subscribe_id: {1}",
                                    conn_ctx.connection_handle,
                                    msg.subscribe_id);

                        // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race
                        // condition
                        stream_buffer->ResetAny();
                        return true;
                    }
                    auto tfn = sub_it->second->GetFullTrackName();
                    auto th = TrackHash(tfn);

                    LOGGER_DEBUG(logger_,
                                 "Received subscribe done conn_id: {0} subscribe_id: {1} track namespace hash: {2} "
                                 "name hash: {3} track alias: {4}",
                                 conn_ctx.connection_handle,
                                 msg.subscribe_id,
                                 th.track_namespace_hash,
                                 th.track_name_hash,
                                 th.track_fullname_hash);

                    sub_it->second.get()->cb_readNotReady(MoqTrackDelegate::TrackReadStatus::NOT_SUBSCRIBED);
                    _delegate->cb_unannounce(conn_ctx.connection_handle, th.track_namespace_hash, th.track_name_hash);
                    */
                    stream_buffer->ResetAny();
                }
                break;
            }
            case messages::MoqMessageType::ANNOUNCE_CANCEL: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqAnnounceCancel>(ctrl_msg, stream_buffer);
                if (parsed) {
                    /*
                    auto tfn = FullTrackName{ msg.track_namespace, {} };
                    auto th = TrackHash(tfn);

                    LOGGER_INFO(logger_, "Received announce cancel for namespace_hash: {0}", th.track_namespace_hash);
                     */
                    stream_buffer->ResetAny();
                }
                break;
            }
            case messages::MoqMessageType::TRACK_STATUS_REQUEST: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqTrackStatusRequest>(ctrl_msg, stream_buffer);
                if (parsed) {
                    /*
                    auto tfn = FullTrackName{ msg.track_namespace, msg.track_name };
                    auto th = TrackHash(tfn);

                    LOGGER_INFO(logger_,
                                "Received track status request for namespace_hash: {0} name_hash: {1}",
                                th.track_namespace_hash,
                                th.track_name_hash);
                     */
                    stream_buffer->ResetAny();
                }
                break;
            }
            case messages::MoqMessageType::TRACK_STATUS: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqTrackStatus>(ctrl_msg, stream_buffer);
                if (parsed) {
                    /*
                    auto tfn = FullTrackName{ msg.track_namespace, msg.track_name };
                    auto th = TrackHash(tfn);

                    LOGGER_INFO(logger_,
                                "Received track status for namespace_hash: {0} name_hash: {1}",
                                th.track_namespace_hash,
                                th.track_name_hash);
                     */
                    stream_buffer->ResetAny();
                }
                break;
            }
            case messages::MoqMessageType::GOAWAY: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqGoaway>(ctrl_msg, stream_buffer);
                if (parsed) {
                    /*
                    std::string new_sess_uri(msg.new_session_uri.begin(), msg.new_session_uri.end());
                    LOGGER_INFO(logger_, "Received goaway new session uri: {0}", new_sess_uri);
                     */
                    stream_buffer->ResetAny();
                }
                break;
            }
            case messages::MoqMessageType::CLIENT_SETUP: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqClientSetup>(ctrl_msg, stream_buffer);
                if (parsed) {
                    /*
                    if (!msg.supported_versions.size()) { // should never happen
                        CloseConnection(conn_ctx.connection_handle,
                                        MoqTerminationReason::PROTOCOL_VIOLATION,
                                        "Client setup contained zero versions");
                    }

                    std::string client_endpoint_id(msg.endpoint_id_parameter.value.begin(),
                                                   msg.endpoint_id_parameter.value.end());

                    _delegate->cb_connectionStatus(
                      conn_ctx.connection_handle, msg.endpoint_id_parameter.value, TransportStatus::Ready);

                    LOGGER_INFO(logger_,
                                "Client setup received conn_id: {0} from: {1} num_versions: {2} role: {3} version: {4}",
                                conn_ctx.connection_handle,
                                client_endpoint_id,
                                msg.num_versions,
                                static_cast<int>(msg.role_parameter.value.front()),
                                msg.supported_versions.front());

                    conn_ctx.client_version = msg.supported_versions.front();

                    SendServerSetup(conn_ctx);
                    conn_ctx.setup_complete = true;
                     */
                    stream_buffer->ResetAny();
                }
                break;
            }
            case messages::MoqMessageType::SERVER_SETUP: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqServerSetup>(ctrl_msg, stream_buffer);
                if (parsed) {
                    /*
                    std::string server_endpoint_id(msg.endpoint_id_parameter.value.begin(),
                                                   msg.endpoint_id_parameter.value.end());

                    _delegate->cb_connectionStatus(
                      conn_ctx.connection_handle, msg.endpoint_id_parameter.value, TransportStatus::Ready);

                    LOGGER_INFO(logger_,
                                "Server setup received conn_id: {0} from: {1} role: {2} selected_version: {3}",
                                conn_ctx.connection_handle,
                                server_endpoint_id,
                                static_cast<int>(msg.role_parameter.value.front()),
                                msg.selection_version);

                    conn_ctx.setup_complete = true;
                     */
                    stream_buffer->ResetAny();
                }
                break;
            }

            default:
                SPDLOG_LOGGER_ERROR(logger_,
                                    "Unsupported MOQT message type: {0}",
                                    static_cast<uint64_t>(*conn_ctx.ctrl_msg_type_received));

                ctrl_msg.status = ControlMessageStatus::kUnsupportedMessageType;

                /*
                CloseConnection(conn_ctx.connection_handle,
                                MoqTerminationReason::PROTOCOL_VIOLATION,
                                "Unsupported MOQT message type");
                return true;
                 */
                break;

        } // End of switch(msg type)

        SPDLOG_LOGGER_DEBUG(logger_,
                            "type: {0} sbuf_size: {1}",
                            static_cast<int>(*conn_ctx.ctrl_msg_type_received),
                            stream_buffer->Size());

        return ctrl_msg;
    }
} // namespace moq
