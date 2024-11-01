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

    bool Client::ProcessCtrlMessage(ConnectionContext& conn_ctx, BytesSpan msg_bytes)
    {
        switch (*conn_ctx.ctrl_msg_type_received) {
            case messages::ControlMessageType::SUBSCRIBE: {
                messages::MoqSubscribe msg;
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, msg.track_name, std::nullopt };
                auto th = TrackHash(tfn);

                if (msg.subscribe_id > conn_ctx.current_subscribe_id) {
                    conn_ctx.current_subscribe_id = msg.subscribe_id + 1;
                }

                conn_ctx.recv_sub_id[msg.subscribe_id] = { th.track_namespace_hash, th.track_name_hash };

                // For client/publisher, notify track that there is a subscriber
                auto ptd = GetPubTrackHandler(conn_ctx, th);
                if (ptd == nullptr) {
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

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received subscribe to announced track alias: {0} recv subscribe_id: {1}, setting "
                                    "send state to ready",
                                    msg.track_alias,
                                    msg.subscribe_id);

                // Indicate send is ready upon subscribe
                // TODO(tievens): Maybe needs a delay as subscriber may have not received ok before data is sent
                ptd->SetSubscribeId(msg.subscribe_id);
                ptd->SetTrackAlias(msg.track_alias);
                ptd->SetStatus(PublishTrackHandler::Status::kOk);

                conn_ctx.recv_sub_id[msg.subscribe_id] = { th.track_namespace_hash, th.track_name_hash };
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

                sub_it->second.get()->SetStatus(SubscribeTrackHandler::Status::kSubscribeError);
                RemoveSubscribeTrack(conn_ctx, *sub_it->second);
                return true;
            }
            case messages::ControlMessageType::ANNOUNCE_OK: {
                messages::MoqAnnounceOk msg;
                msg_bytes >> msg;

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
            case messages::ControlMessageType::UNSUBSCRIBE: {
                messages::MoqUnsubscribe msg;
                msg_bytes >> msg;

                const auto& th_it = conn_ctx.recv_sub_id.find(msg.subscribe_id);

                if (th_it == conn_ctx.recv_sub_id.end()) {
                    SPDLOG_LOGGER_WARN(
                      logger_,
                      "Received unsubscribe to unknown subscribe_id conn_id: {0} subscribe_id: {1}, ignored",
                      conn_ctx.connection_handle,
                      msg.subscribe_id);

                    // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to
                    // race condition
                    return true;
                }

                const auto& [ns_hash, name_hash] = th_it->second;
                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received unsubscribe conn_id: {0} subscribe_id: {1}",
                                    conn_ctx.connection_handle,
                                    msg.subscribe_id);

                const auto pub_track_ns_it = conn_ctx.pub_tracks_by_name.find(ns_hash); // Find namespace
                if (pub_track_ns_it != conn_ctx.pub_tracks_by_name.end()) {
                    const auto& [_, handlers] = *pub_track_ns_it;
                    const auto pub_track_n_it = handlers.find(name_hash); // Find name
                    if (pub_track_n_it != handlers.end()) {
                        const auto& [_, handler] = *pub_track_n_it;
                        handler->SetStatus(PublishTrackHandler::Status::kNoSubscribers);
                    }
                }
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

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received subscribe done conn_id: {0} subscribe_id: {1} track namespace hash: {2} "
                                    "name hash: {3} track alias: {4}",
                                    conn_ctx.connection_handle,
                                    msg.subscribe_id,
                                    TrackHash(tfn).track_namespace_hash,
                                    TrackHash(tfn).track_name_hash,
                                    TrackHash(tfn).track_fullname_hash);

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
                AnnounceStatusChanged(tfn.name_space, PublishAnnounceStatus::kNotAnnounced);
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
            case messages::ControlMessageType::SERVER_SETUP: {
                messages::MoqServerSetup msg;
                msg_bytes >> msg;

                std::string server_endpoint_id(msg.endpoint_id_parameter.value.begin(),
                                               msg.endpoint_id_parameter.value.end());

                ServerSetupReceived({ msg.selection_version, server_endpoint_id });
                SetStatus(Status::kReady);

                SPDLOG_LOGGER_INFO(logger_,
                                   "Server setup received conn_id: {0} from: {1} role: {2} selected_version: {3}",
                                   conn_ctx.connection_handle,
                                   server_endpoint_id,
                                   static_cast<int>(msg.role_parameter.value.front()),
                                   msg.selection_version);

                conn_ctx.setup_complete = true;
                return true;
            }
            case messages::MoqMessageType::SUBSCRIBE_ANNOUNCES_OK: {
                messages::MoqSubscribeAnnouncesOk msg;
                msg_bytes >> msg;

                SPDLOG_LOGGER_DEBUG(
                  logger_, "Received subscribe_announce ok, conn_id: {0}", conn_ctx.connection_handle);

                auto it = conn_ctx.announce_subscriptions.find(msg.track_namespace_prefix);
                if (it->second.get()->GetStatus() != SubscribeAnnouncesHandler::Status::kOk) {
                    // TODO : report status to the handler
                    it->second.get()->SetStatus(SubscribeAnnouncesHandler::Status::kOk);
                }
                return true;
            }
            case messages::MoqMessageType::ANNOUNCE: {
                messages::MoqAnnounce msg;
                msg_bytes >> msg;

                SPDLOG_LOGGER_DEBUG(logger_, "Received announce on conn_id: {0}", conn_ctx.connection_handle);

                for (const auto& entry : conn_ctx.announce_subscriptions) {
                    if (entry.first.Contains(msg.track_namespace)) {
                        entry.second.get()->MatchingTrackNamespaceReceived(msg.track_namespace);
                    }
                }

                // TODO: Revisit sending OK in this case.
                // SendAnnounceOk(conn_ctx, msg.track_namespace);

                return true;
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
