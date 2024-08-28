/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#include <moq/server.h>

namespace moq {
    Server::Status Server::Start()
    {
        return Transport::Start();
    }

    void Server::Stop()
    {
        stop_ = true;
        Transport::Stop();
    }

    void Server::NewConnectionAccepted(moq::ConnectionHandle connection_handle, const ConnectionRemoteInfo& remote) {
        SPDLOG_LOGGER_INFO(
          logger_, "New connection conn_id: {0} remote ip: {1} port: {2}", connection_handle, remote.ip, remote.port);
    }

    bool Server::ProcessCtrlMessage(ConnectionContext& conn_ctx, std::shared_ptr<StreamBuffer<uint8_t>>& stream_buffer)
    {
        if (stream_buffer->Size() == 0) { // should never happen
            CloseConnection(conn_ctx.connection_handle,
                            messages::MoqTerminationReason::INTERNAL_ERROR,
                            "Stream buffer cannot be zero when parsing message type");
            return false;
        }

        if (not conn_ctx.ctrl_msg_type_received) { // should never happen

            CloseConnection(conn_ctx.connection_handle,
                            messages::MoqTerminationReason::INTERNAL_ERROR,
                            "Process recv message connection context is missing message type");

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

                    // TODO(tievens): add filter type when caching supports it
                    SubscribeReceived(conn_ctx.connection_handle, msg.subscribe_id, msg.track_alias,  tfn, {});

                    // TODO(tievens): Delay the subscribe OK till ResolveSubscribe() is called
                    SendSubscribeOk(conn_ctx, msg.subscribe_id, kSubscribeExpires, false);

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
            case messages::MoqMessageType::ANNOUNCE: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqAnnounce>(stream_buffer);
                if (parsed) {
                    auto tfn = FullTrackName{ msg.track_namespace, {} };
                    auto th = TrackHash(tfn);

                    AnnounceReceived(conn_ctx.connection_handle, tfn.name_space, {});

                    // TODO(tievens): Delay announce OK till ResolveAnnounce() is called
                    SendAnnounceOk(conn_ctx, msg.track_namespace);

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
                        auto tfn = FullTrackName{ *msg.track_namespace, {} };
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

            case messages::MoqMessageType::UNANNOUNCE: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqUnannounce>(stream_buffer);
                if (parsed) {

                    auto tfn = FullTrackName{ msg.track_namespace, {} };
                    auto th = TrackHash(tfn);

                    SPDLOG_LOGGER_INFO(logger_, "Received unannounce for namespace_hash: {0}", th.track_namespace_hash);

                    UnannounceReceived(conn_ctx.connection_handle, tfn.name_space);

                    stream_buffer->ResetAny();
                    return true;
                }
                break;
            }

            case messages::MoqMessageType::UNSUBSCRIBE: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqUnsubscribe>(stream_buffer);
                if (parsed) {

                    const auto& [name_space, name] = conn_ctx.recv_sub_id[msg.subscribe_id];
                    TrackHash th(name_space, name);
                    if (auto pdt = GetPubTrackHandler(conn_ctx, th)) {
                        pdt->lock()->SetStatus(PublishTrackHandler::Status::kNoSubscribers);
                    }

                    conn_ctx.recv_sub_id.erase(msg.subscribe_id);

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
                    auto th = TrackHash(tfn);

                    SPDLOG_LOGGER_DEBUG(
                      logger_,
                      "Received subscribe done conn_id: {0} subscribe_id: {1} track namespace hash: {2} "
                      "name hash: {3} track alias: {4}",
                      conn_ctx.connection_handle,
                      msg.subscribe_id,
                      th.track_namespace_hash,
                      th.track_name_hash,
                      th.track_fullname_hash);

                    sub_it->second.get()->SetStatus(SubscribeTrackHandler::Status::kNotSubscribed);

                    stream_buffer->ResetAny();
                    return true;
                }
                break;
            }
            case messages::MoqMessageType::ANNOUNCE_CANCEL: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqAnnounceCancel>(stream_buffer);
                if (parsed) {
                    auto tfn = FullTrackName{ msg.track_namespace, {} };
                    auto th = TrackHash(tfn);

                    SPDLOG_LOGGER_INFO(
                      logger_, "Received announce cancel for namespace_hash: {0}", th.track_namespace_hash);
                    stream_buffer->ResetAny();
                    return true;
                }
                break;
            }
            case messages::MoqMessageType::TRACK_STATUS_REQUEST: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqTrackStatusRequest>(stream_buffer);
                if (parsed) {
                    auto tfn = FullTrackName{ msg.track_namespace, msg.track_name };
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
                    auto tfn = FullTrackName{ msg.track_namespace, msg.track_name };
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
            case messages::MoqMessageType::CLIENT_SETUP: {
                auto&& [msg, parsed] = ParseControlMessage<messages::MoqClientSetup>(stream_buffer);
                if (parsed) {
                    if (!msg.supported_versions.size()) { // should never happen
                        CloseConnection(conn_ctx.connection_handle,
                                        messages::MoqTerminationReason::PROTOCOL_VIOLATION,
                                        "Client setup contained zero versions");
                        stream_buffer->ResetAny();
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
                    stream_buffer->ResetAny();

                    // TODO(tievens): Revisit sending sever setup immediately or wait for something else from server
                    SendServerSetup(conn_ctx);
                    conn_ctx.setup_complete = true;
                    return true;
                }
                break;
            }

            default:
                SPDLOG_LOGGER_ERROR(logger_,
                                    "Unsupported MOQT message type: {0}",
                                    static_cast<uint64_t>(*conn_ctx.ctrl_msg_type_received));

                CloseConnection(conn_ctx.connection_handle,
                                messages::MoqTerminationReason::PROTOCOL_VIOLATION,
                                "Unsupported MOQT message type");
                return true;

        } // End of switch(msg type)

        SPDLOG_LOGGER_DEBUG(logger_,
                            "type: {0} sbuf_size: {1}",
                            static_cast<int>(*conn_ctx.ctrl_msg_type_received),
                            stream_buffer->Size());

        return false;
    }

} // namespace moq
