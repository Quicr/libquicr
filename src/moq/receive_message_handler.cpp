/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#include "moq/detail/receive_message_handler.h"

namespace moq {
    using namespace moq::messages;

    ReceiveMessageHandler::ControlMessage ReceiveMessageHandler::ProcessCtrlMessage(
      Transport::ConnectionContext& conn_ctx,
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
            case MoqMessageType::SUBSCRIBE: {
                if (not stream_buffer->AnyHasValue()) {
                    SPDLOG_LOGGER_DEBUG(logger_, "Received subscribe, init stream buffer");
                    stream_buffer->InitAny<MoqSubscribe>();
                }

                auto& msg = stream_buffer->GetAny<MoqSubscribe>();
                if (*stream_buffer >> msg) {
                    ctrl_msg.status = ControlMessageStatus::kMessageComplete;

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

                    stream_buffer->ResetAny();
                    */
                }
                break;
            }
            case MoqMessageType::SUBSCRIBE_OK: {
                if (not stream_buffer->AnyHasValue()) {
                    SPDLOG_LOGGER_DEBUG(logger_, "Received subscribe ok, init stream buffer");
                    stream_buffer->InitAny<MoqSubscribeOk>();
                }

                auto& msg = stream_buffer->GetAny<MoqSubscribeOk>();
                if (*stream_buffer >> msg) {
                    ctrl_msg.status = ControlMessageStatus::kMessageComplete;

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

                    stream_buffer->ResetAny();
                    */
                }
                break;
            }
            case MoqMessageType::SUBSCRIBE_ERROR: {
                if (not stream_buffer->AnyHasValue()) {
                    SPDLOG_LOGGER_DEBUG(logger_, "Received subscribe error, init stream buffer");
                    stream_buffer->InitAny<MoqSubscribeError>();
                }

                auto& msg = stream_buffer->GetAny<MoqSubscribeError>();
                if (*stream_buffer >> msg) {
                    ctrl_msg.status = ControlMessageStatus::kMessageComplete;

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

                    stream_buffer->ResetAny();
                    */
                }
                break;
            }
            case MoqMessageType::ANNOUNCE: {
                if (not stream_buffer->AnyHasValue()) {
                    SPDLOG_LOGGER_DEBUG(logger_, "Received announce, init stream buffer");
                    stream_buffer->InitAny<MoqAnnounce>();
                }

                auto& msg = stream_buffer->GetAny<MoqAnnounce>();
                if (*stream_buffer >> msg) {
                    ctrl_msg.status = ControlMessageStatus::kMessageComplete;

                    /*
                    auto tfn = FullTrackName{ msg.track_namespace, {} };
                    auto th = TrackHash(tfn);

                    if (_delegate->cb_announce(conn_ctx.connection_handle, th.track_namespace_hash)) {
                        SendAnnounceOk(conn_ctx, msg.track_namespace);
                        _delegate->cb_announce_post(conn_ctx.connection_handle, th.track_namespace_hash);
                    }

                    stream_buffer->ResetAny();
                    */
                }
                break;
            }
            case MoqMessageType::ANNOUNCE_OK: {
                if (not stream_buffer->AnyHasValue()) {
                    SPDLOG_LOGGER_DEBUG(logger_, "Received announce ok, init stream buffer");
                    stream_buffer->InitAny<MoqAnnounceOk>();
                }

                auto& msg = stream_buffer->GetAny<MoqAnnounceOk>();
                if (*stream_buffer >> msg) {
                    ctrl_msg.status = ControlMessageStatus::kMessageComplete;

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

                    stream_buffer->ResetAny();
                    */
                }
                break;
            }
            case MoqMessageType::ANNOUNCE_ERROR: {
                if (not stream_buffer->AnyHasValue()) {
                    SPDLOG_LOGGER_DEBUG(logger_, "Received announce error, init stream buffer");
                    stream_buffer->InitAny<MoqAnnounceError>();
                }

                auto& msg = stream_buffer->GetAny<MoqAnnounceError>();
                if (*stream_buffer >> msg) {
                    ctrl_msg.status = ControlMessageStatus::kMessageComplete;

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

                        stream_buffer->ResetAny();
                        return true;
                    } */
                }

                break;
            }
            case MoqMessageType::UNANNOUNCE: {
                if (not stream_buffer->AnyHasValue()) {
                    SPDLOG_LOGGER_DEBUG(logger_, "Received unannounce, init stream buffer");
                    stream_buffer->InitAny<MoqUnannounce>();
                }

                auto& msg = stream_buffer->GetAny<MoqUnannounce>();
                if (*stream_buffer >> msg) {
                    ctrl_msg.status = ControlMessageStatus::kMessageComplete;

                    /*
                    auto tfn = FullTrackName{ msg.track_namespace, {} };
                    auto th = TrackHash(tfn);

                    LOGGER_INFO(logger_, "Received unannounce for namespace_hash: {0}", th.track_namespace_hash);

                    _delegate->cb_unannounce(conn_ctx.connection_handle, th.track_namespace_hash, std::nullopt);

                    stream_buffer->ResetAny();
                    return true;
                    */
                }

                break;
            }
            case MoqMessageType::UNSUBSCRIBE: {
                if (not stream_buffer->AnyHasValue()) {
                    SPDLOG_LOGGER_DEBUG(logger_, "Received unsubscribe, init stream buffer");
                    stream_buffer->InitAny<MoqUnsubscribe>();
                }

                auto& msg = stream_buffer->GetAny<MoqUnsubscribe>();
                if (*stream_buffer >> msg) {
                    ctrl_msg.status = ControlMessageStatus::kMessageComplete;

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

                    stream_buffer->ResetAny();
                    return true;
                    */
                }

                break;
            }
            case MoqMessageType::SUBSCRIBE_DONE: {
                if (not stream_buffer->AnyHasValue()) {
                    SPDLOG_LOGGER_DEBUG(logger_, "Received subscribe done, init stream buffer");
                    stream_buffer->InitAny<MoqSubscribeDone>();
                }

                auto& msg = stream_buffer->GetAny<MoqSubscribeDone>();
                if (*stream_buffer >> msg) {
                    ctrl_msg.status = ControlMessageStatus::kMessageComplete;

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

                    stream_buffer->ResetAny();
                    return true;
                    */
                }
                break;
            }
            case MoqMessageType::ANNOUNCE_CANCEL: {
                if (not stream_buffer->AnyHasValue()) {
                    SPDLOG_LOGGER_DEBUG(logger_, "Received announce cancel, init stream buffer");
                    stream_buffer->InitAny<MoqAnnounceCancel>();
                }

                auto& msg = stream_buffer->GetAny<MoqAnnounceCancel>();
                if (*stream_buffer >> msg) {
                    ctrl_msg.status = ControlMessageStatus::kMessageComplete;

                    /*
                    auto tfn = FullTrackName{ msg.track_namespace, {} };
                    auto th = TrackHash(tfn);

                    LOGGER_INFO(logger_, "Received announce cancel for namespace_hash: {0}", th.track_namespace_hash);

                    stream_buffer->ResetAny();
                    return true;
                     */
                }

                break;
            }
            case MoqMessageType::TRACK_STATUS_REQUEST: {
                if (not stream_buffer->AnyHasValue()) {
                    SPDLOG_LOGGER_DEBUG(logger_, "Received track status request, init stream buffer");
                    stream_buffer->InitAny<MoqTrackStatusRequest>();
                }

                auto& msg = stream_buffer->GetAny<MoqTrackStatusRequest>();
                if (*stream_buffer >> msg) {
                    ctrl_msg.status = ControlMessageStatus::kMessageComplete;

                    /*
                    auto tfn = FullTrackName{ msg.track_namespace, msg.track_name };
                    auto th = TrackHash(tfn);

                    LOGGER_INFO(logger_,
                                "Received track status request for namespace_hash: {0} name_hash: {1}",
                                th.track_namespace_hash,
                                th.track_name_hash);

                    stream_buffer->ResetAny();
                    return true;
                     */
                }

                break;
            }
            case MoqMessageType::TRACK_STATUS: {
                if (not stream_buffer->AnyHasValue()) {
                    SPDLOG_LOGGER_DEBUG(logger_, "Received track status, init stream buffer");
                    stream_buffer->InitAny<MoqTrackStatus>();
                }

                auto& msg = stream_buffer->GetAny<MoqTrackStatus>();
                if (*stream_buffer >> msg) {
                    ctrl_msg.status = ControlMessageStatus::kMessageComplete;

                    /*
                    auto tfn = FullTrackName{ msg.track_namespace, msg.track_name };
                    auto th = TrackHash(tfn);

                    LOGGER_INFO(logger_,
                                "Received track status for namespace_hash: {0} name_hash: {1}",
                                th.track_namespace_hash,
                                th.track_name_hash);

                    stream_buffer->ResetAny();
                    return true;
                     */
                }

                break;
            }
            case MoqMessageType::GOAWAY: {
                if (not stream_buffer->AnyHasValue()) {
                    SPDLOG_LOGGER_DEBUG(logger_, "Received goaway, init stream buffer");
                    stream_buffer->InitAny<MoqGoaway>();
                }

                auto& msg = stream_buffer->GetAny<MoqGoaway>();
                if (*stream_buffer >> msg) {
                    ctrl_msg.status = ControlMessageStatus::kMessageComplete;

                    /*
                    std::string new_sess_uri(msg.new_session_uri.begin(), msg.new_session_uri.end());
                    LOGGER_INFO(logger_, "Received goaway new session uri: {0}", new_sess_uri);

                    stream_buffer->ResetAny();
                    return true;
                     */
                }

                break;
            }
            case MoqMessageType::CLIENT_SETUP: {
                if (not stream_buffer->AnyHasValue()) {
                    SPDLOG_LOGGER_DEBUG(logger_, "Received client setup, init stream buffer");
                    stream_buffer->InitAny<MoqClientSetup>();
                }

                auto& msg = stream_buffer->GetAny<MoqClientSetup>();
                if (*stream_buffer >> msg) {
                    ctrl_msg.status = ControlMessageStatus::kMessageComplete;

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
                    stream_buffer->ResetAny();

                    SendServerSetup(conn_ctx);
                    conn_ctx.setup_complete = true;
                    return true;
                     */
                }
                break;
            }
            case MoqMessageType::SERVER_SETUP: {
                if (not stream_buffer->AnyHasValue()) {
                    SPDLOG_LOGGER_DEBUG(logger_, "Received server setup, init stream buffer");
                    stream_buffer->InitAny<MoqServerSetup>();
                }

                auto& msg = stream_buffer->GetAny<MoqServerSetup>();
                if (*stream_buffer >> msg) {
                    ctrl_msg.status = ControlMessageStatus::kMessageComplete;

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

                    stream_buffer->ResetAny();
                    conn_ctx.setup_complete = true;
                    return true;
                     */
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

    bool ReceiveMessageHandler::ProcessStreamDataMessage(Server::ConnectionContext& conn_ctx,
                                                 std::shared_ptr<StreamBuffer<uint8_t>>& stream_buffer)
    {
        if (stream_buffer->Size() == 0) { // should never happen
            CloseConnection(conn_ctx.connection_handle,
                            MoqTerminationReason::INTERNAL_ERROR,
                            "Stream buffer cannot be zero when parsing message type");
        }

        // Header not set, get the header for this stream or datagram
        MoqMessageType data_type;
        if (!stream_buffer->AnyHasValue()) {
            auto val = stream_buffer->DecodeUintV();
            if (val) {
                data_type = static_cast<MoqMessageType>(*val);
            } else {
                return false;
            }
        } else {
            auto dt = stream_buffer->GetAnyType();
            if (dt.has_value()) {
                data_type = static_cast<MoqMessageType>(*dt);
            }
            else {
                LOGGER_WARN(logger_, "Unknown data type for data stream");
                return true;
            }
        }

        switch (data_type) {
            case MoqMessageType::OBJECT_STREAM: {
                if (not stream_buffer->AnyHasValue()) {
                    LOGGER_DEBUG(logger_, "Received stream header object, init stream buffer");
                    stream_buffer->InitAny<MoqObjectStream>(
                      static_cast<uint64_t>(MoqMessageType::OBJECT_STREAM));
                }

                auto& msg = stream_buffer->GetAny<MoqObjectStream>();
                if (*stream_buffer >> msg) {
                    auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                    if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                        LOGGER_WARN(logger_, "Received stream_object to unknown subscribe track subscribe_id: {0}, ignored", msg.subscribe_id);

                        // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                        return true;
                    }

                    LOGGER_DEBUG(logger_,
                                 "Received stream_object subscribe_id: {0} priority: {1} track_alias: {2} group_id: "
                                 "{3} object_id: {4} data size: {5}",
                                 msg.subscribe_id,
                                 msg.priority,
                                 msg.track_alias,
                                 msg.group_id,
                                 msg.object_id,
                                 msg.payload.size());
                    sub_it->second->cb_objectReceived(msg.group_id, msg.object_id, msg.priority,
                                                      std::move(msg.payload),
                                                      TrackMode::kStreamPerObject);
                    stream_buffer->ResetAny();
                }
                break;
            }

            case MoqMessageType::STREAM_HEADER_TRACK: {
                if (not stream_buffer->AnyHasValue()) {
                    LOGGER_DEBUG(logger_, "Received stream header track, init stream buffer");
                    stream_buffer->InitAny<MoqStreamHeaderTrack>(
                      static_cast<uint64_t>(MoqMessageType::STREAM_HEADER_TRACK));
                }

                auto& msg = stream_buffer->GetAny<MoqStreamHeaderTrack>();
                if (!stream_buffer->AnyHasValueB() && *stream_buffer >> msg) {
                    auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                    if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                        LOGGER_WARN(logger_, "Received stream_header_track to unknown subscribe track subscribe_id: {0}, ignored", msg.subscribe_id);

                        // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                        return true;
                    }

                    // Init second working buffer to read data object
                    stream_buffer->InitAnyB<MoqStreamTrackObject>();

                    LOGGER_DEBUG(logger_,
                                 "Received stream_header_track subscribe_id: {0} priority: {1} track_alias: {2}",
                                 msg.subscribe_id,
                                 msg.priority,
                                 msg.track_alias);
                }

                if (stream_buffer->AnyHasValueB()) {
                    MoqStreamTrackObject obj;
                    if (*stream_buffer >> obj) {
                        auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                        if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                            LOGGER_WARN(logger_, "Received stream_header_group to unknown subscribe track subscribe_id: {0}, ignored", msg.subscribe_id);

                            // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                            return true;
                        }

                        LOGGER_DEBUG(logger_,
                                     "Received stream_track_object subscribe_id: {0} priority: {1} track_alias: {2} "
                                     "group_id: {3} object_id: {4} data size: {5}",
                                     msg.subscribe_id,
                                     msg.priority,
                                     msg.track_alias,
                                     obj.group_id,
                                     obj.object_id,
                                     obj.payload.size());
                        stream_buffer->ResetAnyB();

                        sub_it->second->cb_objectReceived(obj.group_id, obj.object_id, msg.priority,
                                                          std::move(obj.payload),
                                                          TrackMode::kStreamPerTrack);
                    }
                }
                break;
            }
            case MoqMessageType::STREAM_HEADER_GROUP: {
                if (not stream_buffer->AnyHasValue()) {
                    LOGGER_DEBUG(logger_, "Received stream header group, init stream buffer");
                    stream_buffer->InitAny<MoqStreamHeaderGroup>(static_cast<uint64_t>(MoqMessageType::STREAM_HEADER_GROUP));
                }

                auto& msg = stream_buffer->GetAny<MoqStreamHeaderGroup>();
                if (!stream_buffer->AnyHasValueB() && *stream_buffer >> msg) {
                    auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                    if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                        LOGGER_WARN(logger_, "Received stream_header_group to unknown subscribe track subscribe_id: {0}, ignored",  msg.subscribe_id);

                        // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                        return true;
                    }

                    // Init second working buffer to read data object
                    stream_buffer->InitAnyB<MoqStreamGroupObject>();

                    LOGGER_DEBUG(
                      logger_,
                      "Received stream_header_group subscribe_id: {0} priority: {1} track_alias: {2} group_id: {3}",
                      msg.subscribe_id,
                      msg.priority,
                      msg.track_alias,
                      msg.group_id);
                }

                if (stream_buffer->AnyHasValueB()) {
                    MoqStreamGroupObject obj;
                    if (*stream_buffer >> obj) {
                        auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                        if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                            LOGGER_WARN(logger_, "Received stream_header_group to unknown subscribe track subscribe_id: {0}, ignored", msg.subscribe_id);

                            // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                            return true;
                        }

                        LOGGER_DEBUG(logger_,
                                     "Received stream_group_object subscribe_id: {0} priority: {1} track_alias: {2} "
                                     "group_id: {3} object_id: {4} data size: {5}",
                                     msg.subscribe_id,
                                     msg.priority,
                                     msg.track_alias,
                                     msg.group_id,
                                     obj.object_id,
                                     obj.payload.size());
                        stream_buffer->ResetAnyB();

                        sub_it->second->cb_objectReceived(msg.group_id, obj.object_id, msg.priority,
                                                          std::move(obj.payload),
                                                          TrackMode::kStreamPerGroup);
                    }
                }

                break;
            }

            default:
                // Process the stream object type
                /*
                logger_->error << "Unsupported MOQT data message "
                               << "type: " << static_cast<uint64_t>(*conn_ctx.ctrl_msg_type_received)
                               << std::flush;
                CloseConnection(conn_ctx.connection_handle,
                                 MoqTerminationReason::PROTOCOL_VIOLATION,
                                 "Unsupported MOQT data message type");
                 */
                return true;

        }

        return false;
    }


} // namespace moq
