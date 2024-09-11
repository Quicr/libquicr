// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/client.h>

namespace quicr {

    Client::Status Client::Connect()
    {
        return Transport::Start();
    }

    Client::Status Client::Disconnect()
    {
        Transport::Stop();
        return Status::kDisconnecting;
    }

    void Client::ServerSetupReceived(const ServerSetupAttributes&) {}

    void Client::AnnounceStatusChanged(const TrackNamespace&, const PublishAnnounceStatus) {}

    void Client::UnpublishedSubscribeReceived(const FullTrackName&, const SubscribeAttributes&)
    {
        // TODO: add the default response
    }

    void Client::ResolveSubscribe(ConnectionHandle, uint64_t, const SubscribeResponse&) {}

    void Client::MetricsSampled(const ConnectionMetrics&) {}

    PublishAnnounceStatus Client::GetAnnounceStatus(const TrackNamespace&)
    {
        return PublishAnnounceStatus();
    }

    void Client::PublishAnnounce(const TrackNamespace&) {}

    void Client::PublishUnannounce(const TrackNamespace&) {}

    bool Client::ProcessCtrlMessage(ConnectionContext& conn_ctx,
                                    std::shared_ptr<SafeStreamBuffer<uint8_t>>& stream_buffer)
    {
        if (stream_buffer->Empty()) { // should never happen
            SPDLOG_LOGGER_ERROR(logger_, "Stream buffer cannot be zero when parsing message type, bad stream");
            return false;
        }

        if (not conn_ctx.ctrl_msg_type_received) { // should never happen
            SPDLOG_LOGGER_ERROR(logger_, "Process receive message connection context is missing, bad stream");
            return false;
        }

        switch (*conn_ctx.ctrl_msg_type_received) {
            case messages::MoqMessageType::SUBSCRIBE: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqSubscribe>(stream_buffer);
                if (parsed) {
                    auto tfn = FullTrackName{ msg.track_namespace, msg.track_name, std::nullopt };
                    auto th = TrackHash(tfn);

                    if (msg.subscribe_id > conn_ctx.current_subscribe_id) {
                        conn_ctx.current_subscribe_id = msg.subscribe_id + 1;
                    }

                    conn_ctx.recv_sub_id[msg.subscribe_id] = { th.track_namespace_hash, th.track_name_hash };

                    // For client/publisher, notify track that there is a subscriber
                    auto ptd = GetPubTrackHandler(conn_ctx, th);
                    if (not ptd.has_value()) {
                        SPDLOG_LOGGER_WARN(logger_,
                                           "Received subscribe unknown publish track conn_id: {0} namespace hash: {1} "
                                           "name hash: {2}",
                                           conn_ctx.connection_handle,
                                           th.track_namespace_hash,
                                           th.track_name_hash);

                        SendSubscribeError(conn_ctx,
                                           msg.subscribe_id,
                                           msg.track_alias,
                                           messages::SubscribeError::TRACK_NOT_EXIST,
                                           "Published track not found");
                        return true;
                    }

                    SendSubscribeOk(conn_ctx, msg.subscribe_id, kSubscribeExpires, false);

                    SPDLOG_LOGGER_DEBUG(
                      logger_,
                      "Received subscribe to announced track alias: {0} recv subscribe_id: {1}, setting "
                      "send state to ready",
                      msg.track_alias,
                      msg.subscribe_id);

                    // Indicate send is ready upon subscribe
                    // TODO(tievens): Maybe needs a delay as subscriber may have not received ok before data is sent
                    auto ptd_l = ptd->lock();
                    ptd_l->SetSubscribeId(msg.subscribe_id);
                    ptd_l->SetStatus(PublishTrackHandler::Status::kOk);

                    conn_ctx.recv_sub_id[msg.subscribe_id] = { th.track_namespace_hash, th.track_name_hash };
                    stream_buffer->ResetAny();
                    return true;
                }
                break;
            }
            case messages::MoqMessageType::SUBSCRIBE_OK: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqSubscribeOk>(stream_buffer);
                if (parsed) {
                    auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);

                    if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                        SPDLOG_LOGGER_WARN(
                          logger_,
                          "Received subscribe ok to unknown subscribe track conn_id: {0} subscribe_id: {1}, ignored",
                          conn_ctx.connection_handle,
                          msg.subscribe_id);

                        // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race
                        // condition
                        stream_buffer->ResetAny();
                        return true;
                    }

                    sub_it->second.get()->SetStatus(SubscribeTrackHandler::Status::kOk);
                    stream_buffer->ResetAny();
                    return true;
                }
                break;
            }
            case messages::MoqMessageType::SUBSCRIBE_ERROR: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqSubscribeError>(stream_buffer);
                if (parsed) {
                    auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);

                    if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                        SPDLOG_LOGGER_WARN(
                          logger_,
                          "Received subscribe error to unknown subscribe_id conn_id: {0} subscribe_id: {1}, ignored",
                          conn_ctx.connection_handle,
                          msg.subscribe_id);

                        // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race
                        // condition
                        stream_buffer->ResetAny();
                        return true;
                    }

                    sub_it->second.get()->SetStatus(SubscribeTrackHandler::Status::kSubscribeError);
                    RemoveSubscribeTrack(conn_ctx, *sub_it->second);

                    stream_buffer->ResetAny();
                    return true;
                }
                break;
            }
            case messages::MoqMessageType::ANNOUNCE_OK: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqAnnounceOk>(stream_buffer);
                if (parsed) {
                    auto tfn = FullTrackName{ msg.track_namespace, {}, std::nullopt };
                    auto th = TrackHash(tfn);
                    SPDLOG_LOGGER_DEBUG(logger_,
                                        "Received announce ok, conn_id: {0} namespace_hash: {1}",
                                        conn_ctx.connection_handle,
                                        th.track_namespace_hash);

                    // Update each track to indicate status is okay to publish
                    auto pub_it = conn_ctx.pub_tracks_by_name.find(th.track_namespace_hash);
                    for (const auto& td : pub_it->second) {
                        if (td.second.get()->GetStatus() != PublishTrackHandler::Status::kOk)
                            td.second.get()->SetStatus(PublishTrackHandler::Status::kNoSubscribers);
                    }
                    stream_buffer->ResetAny();
                    return true;
                }
                break;
            }
            case messages::MoqMessageType::ANNOUNCE_ERROR: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqAnnounceError>(stream_buffer);
                if (parsed) {
                    if (msg.track_namespace) {
                        std::string reason = "unknown";
                        auto tfn = FullTrackName{ *msg.track_namespace, {}, std::nullopt };
                        auto th = TrackHash(tfn);

                        if (msg.reason_phrase) {
                            reason.assign(msg.reason_phrase->begin(), msg.reason_phrase->end());
                        }

                        SPDLOG_LOGGER_INFO(
                          logger_,
                          "Received announce error for namespace_hash: {0} error code: {1} reason: {2}",
                          th.track_namespace_hash,
                          (msg.err_code.has_value() ? *msg.err_code : 0),
                          reason);
                    }
                    stream_buffer->ResetAny();
                    return true;
                }
                break;
            }
            case messages::MoqMessageType::UNSUBSCRIBE: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqUnsubscribe>(stream_buffer);
                if (parsed) {
                    auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                    if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                        SPDLOG_LOGGER_WARN(
                          logger_,
                          "Received unsubscribe to unknown subscribe_id conn_id: {0} subscribe_id: {1}, ignored",
                          conn_ctx.connection_handle,
                          msg.subscribe_id);

                        // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to
                        // race condition
                        stream_buffer->ResetAny();
                        return true;
                    }

                    SPDLOG_LOGGER_DEBUG(logger_,
                                        "Received unsubscribe conn_id: {0} subscribe_id: {1}",
                                        conn_ctx.connection_handle,
                                        msg.subscribe_id);
                    sub_it->second.get()->SetStatus(SubscribeTrackHandler::Status::kNotSubscribed);

                    RemoveSubscribeTrack(conn_ctx, *sub_it->second);

                    stream_buffer->ResetAny();
                    return true;
                }
                break;
            }
            case messages::MoqMessageType::SUBSCRIBE_DONE: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqSubscribeDone>(stream_buffer);
                if (parsed) {
                    auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                    if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                        SPDLOG_LOGGER_WARN(
                          logger_,
                          "Received subscribe done to unknown subscribe_id conn_id: {0} subscribe_id: {1}",
                          conn_ctx.connection_handle,
                          msg.subscribe_id);

                        // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race
                        // condition
                        stream_buffer->ResetAny();
                        return true;
                    }
                    auto tfn = sub_it->second->GetFullTrackName();

                    SPDLOG_LOGGER_DEBUG(
                      logger_,
                      "Received subscribe done conn_id: {0} subscribe_id: {1} track namespace hash: {2} "
                      "name hash: {3} track alias: {4}",
                      conn_ctx.connection_handle,
                      msg.subscribe_id,
                      TrackHash(tfn).track_namespace_hash,
                      TrackHash(tfn).track_name_hash,
                      TrackHash(tfn).track_fullname_hash);

                    sub_it->second.get()->SetStatus(SubscribeTrackHandler::Status::kNotSubscribed);
                    stream_buffer->ResetAny();
                    return true;
                }
                break;
            }
            case messages::MoqMessageType::ANNOUNCE_CANCEL: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqAnnounceCancel>(stream_buffer);
                if (parsed) {
                    auto tfn = FullTrackName{ msg.track_namespace, {}, std::nullopt };
                    auto th = TrackHash(tfn);

                    SPDLOG_LOGGER_INFO(
                      logger_, "Received announce cancel for namespace_hash: {0}", th.track_namespace_hash);
                    AnnounceStatusChanged(tfn.name_space, PublishAnnounceStatus::kNotAnnounced);

                    stream_buffer->ResetAny();
                    return true;
                }
                break;
            }
            case messages::MoqMessageType::TRACK_STATUS_REQUEST: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqTrackStatusRequest>(stream_buffer);
                if (parsed) {
                    auto tfn = FullTrackName{ msg.track_namespace, msg.track_name, std::nullopt };
                    auto th = TrackHash(tfn);

                    SPDLOG_LOGGER_INFO(logger_,
                                       "Received track status request for namespace_hash: {0} name_hash: {1}",
                                       th.track_namespace_hash,
                                       th.track_name_hash);
                    stream_buffer->ResetAny();
                    return true;
                }
                break;
            }
            case messages::MoqMessageType::TRACK_STATUS: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqTrackStatus>(stream_buffer);
                if (parsed) {
                    auto tfn = FullTrackName{ msg.track_namespace, msg.track_name, std::nullopt };
                    auto th = TrackHash(tfn);

                    SPDLOG_LOGGER_INFO(logger_,
                                       "Received track status for namespace_hash: {0} name_hash: {1}",
                                       th.track_namespace_hash,
                                       th.track_name_hash);
                    stream_buffer->ResetAny();
                    return true;
                }
                break;
            }
            case messages::MoqMessageType::GOAWAY: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqGoaway>(stream_buffer);
                if (parsed) {
                    std::string new_sess_uri(msg.new_session_uri.begin(), msg.new_session_uri.end());
                    SPDLOG_LOGGER_INFO(logger_, "Received goaway new session uri: {0}", new_sess_uri);
                    stream_buffer->ResetAny();
                    return true;
                }
                break;
            }
            case messages::MoqMessageType::SERVER_SETUP: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqServerSetup>(stream_buffer);
                if (parsed) {
                    std::string server_endpoint_id(msg.endpoint_id_parameter.value.begin(),
                                                   msg.endpoint_id_parameter.value.end());

                    SetStatus(Status::kReady);

                    SPDLOG_LOGGER_INFO(logger_,
                                       "Server setup received conn_id: {0} from: {1} role: {2} selected_version: {3}",
                                       conn_ctx.connection_handle,
                                       server_endpoint_id,
                                       static_cast<int>(msg.role_parameter.value.front()),
                                       msg.selection_version);

                    conn_ctx.setup_complete = true;
                    stream_buffer->ResetAny();
                    return true;
                }
                break;
            }

            default:
                SPDLOG_LOGGER_ERROR(logger_,
                                    "Unsupported MOQT message type: {0}, bad stream",
                                    static_cast<uint64_t>(*conn_ctx.ctrl_msg_type_received));
                return false;

        } // End of switch(msg type)

        return false;
    }

} // namespace quicr
