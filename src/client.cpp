// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <optional>
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
    void Client::AnnounceReceived(const TrackNamespace&, const PublishAnnounceAttributes&) {}
    void Client::UnannounceReceived(const TrackNamespace&) {}

    void Client::SubscribeAnnouncesStatusChanged(const TrackNamespace&,
                                                 std::optional<messages::SubscribeAnnouncesErrorCode>,
                                                 std::optional<messages::ReasonPhrase>)
    {
    }

    void Client::UnpublishedSubscribeReceived(const FullTrackName&, const messages::SubscribeAttributes&)
    {
        // TODO: add the default response
    }

    void Client::ResolveSubscribe(ConnectionHandle connection_handle,
                                  uint64_t request_id,
                                  const SubscribeResponse& subscribe_response)
    {
        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        switch (subscribe_response.reason_code) {
            case SubscribeResponse::ReasonCode::kOk: {
                SendSubscribeOk(conn_it->second,
                                request_id,
                                kSubscribeExpires,
                                subscribe_response.largest_location.has_value(),
                                subscribe_response.largest_location.has_value()
                                  ? subscribe_response.largest_location.value()
                                  : messages::Location());
                break;
            }

            default:
                SendSubscribeError(
                  conn_it->second, request_id, {}, messages::SubscribeErrorCode::kInternalError, "Internal error");
                break;
        }
    }

    void Client::MetricsSampled(const ConnectionMetrics&) {}

    PublishAnnounceStatus Client::GetAnnounceStatus(const TrackNamespace&)
    {
        return PublishAnnounceStatus();
    }

    void Client::PublishAnnounce(const TrackNamespace&) {}

    void Client::PublishUnannounce(const TrackNamespace&) {}

    bool Client::ProcessCtrlMessage(ConnectionContext& conn_ctx, BytesSpan msg_bytes)
    try {
        switch (*conn_ctx.ctrl_msg_type_received) {
            case messages::ControlMessageType::kSubscribe: {
                messages::Subscribe msg(
                  [](messages::Subscribe& msg) {
                      if (msg.filter_type == messages::FilterType::kAbsoluteStart ||
                          msg.filter_type == messages::FilterType::kAbsoluteRange) {
                          msg.group_0 =
                            std::make_optional<messages::Subscribe::Group_0>(messages::Subscribe::Group_0{ 0, 0 });
                      }
                  },
                  [](messages::Subscribe& msg) {
                      if (msg.filter_type == messages::FilterType::kAbsoluteRange) {
                          msg.group_1 =
                            std::make_optional<messages::Subscribe::Group_1>(messages::Subscribe::Group_1{ 0 });
                      }
                  });

                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, msg.track_name, std::nullopt };
                auto th = TrackHash(tfn);

                if (msg.request_id > conn_ctx.next_request_id) {
                    conn_ctx.next_request_id = msg.request_id + 1;
                }

                conn_ctx.recv_sub_id[msg.request_id] = { .track_full_name = tfn };

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
                                       msg.request_id,
                                       msg.track_alias,
                                       messages::SubscribeErrorCode::kTrackNotExist,
                                       "Published track not found");
                    return true;
                }

                ResolveSubscribe(conn_ctx.connection_handle, msg.request_id, { SubscribeResponse::ReasonCode::kOk });

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received subscribe to announced track alias: {0} recv request_id: {1}, setting "
                                    "send state to ready",
                                    msg.track_alias,
                                    msg.request_id);

                // Indicate send is ready upon subscribe
                // TODO(tievens): Maybe needs a delay as subscriber may have not received ok before data is sent
                ptd->SetRequestId(msg.request_id);
                ptd->SetTrackAlias(msg.track_alias);
                ptd->SetStatus(PublishTrackHandler::Status::kOk);

                conn_ctx.recv_sub_id[msg.request_id] = { tfn };
                return true;
            }
            case messages::ControlMessageType::kSubscribeUpdate: {
                messages::SubscribeUpdate msg;
                msg_bytes >> msg;

                if (conn_ctx.recv_sub_id.count(msg.request_id) == 0) {
                    // update for invalid subscription
                    SPDLOG_LOGGER_WARN(logger_,
                                       "Received subscribe_update {0} for unknown subscription conn_id: {1}",
                                       msg.request_id,
                                       conn_ctx.connection_handle);

                    SendSubscribeError(conn_ctx,
                                       msg.request_id,
                                       0x0,
                                       messages::SubscribeErrorCode::kTrackNotExist,
                                       "Subscription not found");
                    return true;
                }

                auto tfn = conn_ctx.recv_sub_id[msg.request_id].track_full_name;
                auto th = TrackHash(tfn);

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
                                       msg.request_id,
                                       th.track_fullname_hash,
                                       messages::SubscribeErrorCode::kTrackNotExist,
                                       "Published track not found");
                    return true;
                }

                SendSubscribeOk(conn_ctx, msg.request_id, kSubscribeExpires, false, { 0, 0 });

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received subscribe_update to track alias: {0} recv request_id: {1}",
                                    th.track_fullname_hash,
                                    msg.request_id);

                ptd->SetStatus(PublishTrackHandler::Status::kSubscriptionUpdated);
                return true;
            }
            case messages::ControlMessageType::kSubscribeOk: {
                messages::SubscribeOk msg([](messages::SubscribeOk& msg) {
                    if (msg.content_exists == 1) {
                        msg.group_0 = std::make_optional<messages::SubscribeOk::Group_0>();
                    }
                });
                msg_bytes >> msg;

                auto sub_it = conn_ctx.tracks_by_request_id.find(msg.request_id);

                if (sub_it == conn_ctx.tracks_by_request_id.end()) {
                    SPDLOG_LOGGER_WARN(
                      logger_,
                      "Received subscribe ok to unknown subscribe track conn_id: {0} request_id: {1}, ignored",
                      conn_ctx.connection_handle,
                      msg.request_id);

                    // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race
                    // condition
                    return true;
                }

                if (msg.group_0.has_value()) {
                    SPDLOG_LOGGER_DEBUG(
                      logger_,
                      "Received subscribe ok conn_id: {} request_id: {} latest_group: {} latest_object: {}",
                      conn_ctx.connection_handle,
                      msg.request_id,
                      msg.group_0->largest_location.group,
                      msg.group_0->largest_location.object);

                    sub_it->second.get()->SetLatestLocation(msg.group_0->largest_location);
                }
                sub_it->second.get()->SetStatus(SubscribeTrackHandler::Status::kOk);
                return true;
            }
            case messages::ControlMessageType::kSubscribeError: {
                messages::SubscribeError msg;
                msg_bytes >> msg;

                auto sub_it = conn_ctx.tracks_by_request_id.find(msg.request_id);

                if (sub_it == conn_ctx.tracks_by_request_id.end()) {
                    SPDLOG_LOGGER_WARN(
                      logger_,
                      "Received subscribe error to unknown request_id conn_id: {0} request_id: {1}, ignored",
                      conn_ctx.connection_handle,
                      msg.request_id);

                    // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race
                    // condition
                    return true;
                }

                SPDLOG_LOGGER_INFO(
                  logger_,
                  "Received subscribe error conn_id: {} request_id: {} reason: {} code: {} requested track_alias: {}",
                  conn_ctx.connection_handle,
                  msg.request_id,
                  std::string(msg.error_reason.begin(), msg.error_reason.end()),
                  static_cast<std::uint64_t>(msg.error_code),
                  msg.track_alias);

                sub_it->second.get()->SetStatus(SubscribeTrackHandler::Status::kError);
                RemoveSubscribeTrack(conn_ctx, *sub_it->second);
                return true;
            }

            case messages::ControlMessageType::kAnnounce: {
                messages::Announce msg;
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, {}, std::nullopt };

                AnnounceReceived(tfn.name_space, {});
                return true;
            }

            case messages::ControlMessageType::kUnannounce: {
                messages::Unannounce msg;
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, {}, std::nullopt };
                UnannounceReceived(tfn.name_space);

                return true;
            }

            case messages::ControlMessageType::kAnnounceOk: {
                messages::AnnounceOk msg;
                msg_bytes >> msg;

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received announce ok, conn_id: {} request_id: {}",
                                    conn_ctx.connection_handle,
                                    msg.request_id);

                // Update each track to indicate status is okay to publish
                auto pub_ns_it = conn_ctx.pub_tracks_ns_by_request_id.find(msg.request_id);
                if (pub_ns_it == conn_ctx.pub_tracks_ns_by_request_id.end()) {
                    break;
                }

                auto pub_it = conn_ctx.pub_tracks_by_name.find(pub_ns_it->second);
                for (const auto& td : pub_it->second) {
                    if (td.second.get()->GetStatus() != PublishTrackHandler::Status::kOk)
                        td.second.get()->SetStatus(PublishTrackHandler::Status::kNoSubscribers);
                }
                return true;
            }
            case messages::ControlMessageType::kAnnounceError: {
                messages::AnnounceError msg;
                msg_bytes >> msg;

                std::string reason = "unknown";
                reason.assign(msg.error_reason.begin(), msg.error_reason.end());

                SPDLOG_LOGGER_INFO(logger_,
                                   "Received announce error for request_id: {} error code: {} reason: {}",
                                   msg.request_id,
                                   static_cast<std::uint64_t>(msg.error_code),
                                   reason);

                return true;
            }
            case messages::ControlMessageType::kSubscribeAnnouncesOk: {
                messages::SubscribeAnnouncesOk msg;
                msg_bytes >> msg;

                const auto it = conn_ctx.sub_announces_by_request_id.find(msg.request_id);
                if (it != conn_ctx.sub_announces_by_request_id.end()) {
                    SubscribeAnnouncesStatusChanged(it->second, std::nullopt, std::nullopt);
                }

                return true;
            }
            case messages::ControlMessageType::kSubscribeAnnouncesError: {
                messages::SubscribeAnnouncesError msg;
                msg_bytes >> msg;

                const auto it = conn_ctx.sub_announces_by_request_id.find(msg.request_id);
                if (it != conn_ctx.sub_announces_by_request_id.end()) {
                    SubscribeAnnouncesStatusChanged(it->second, std::nullopt, std::nullopt);

                    auto error_code = static_cast<messages::SubscribeAnnouncesErrorCode>(msg.error_code);
                    SubscribeAnnouncesStatusChanged(
                      it->second, error_code, std::make_optional<messages::ReasonPhrase>(msg.error_reason));
                }

                return true;
            }

            case messages::ControlMessageType::kUnsubscribe: {
                messages::Unsubscribe msg;
                msg_bytes >> msg;

                const auto& th_it = conn_ctx.recv_sub_id.find(msg.request_id);

                if (th_it == conn_ctx.recv_sub_id.end()) {
                    SPDLOG_LOGGER_WARN(
                      logger_,
                      "Received unsubscribe to unknown request_id conn_id: {0} request_id: {1}, ignored",
                      conn_ctx.connection_handle,
                      msg.request_id);

                    // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to
                    // race condition
                    return true;
                }

                const auto& th = TrackHash(th_it->second.track_full_name);
                const auto& ns_hash = th.track_namespace_hash;
                const auto& name_hash = th.track_name_hash;
                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received unsubscribe conn_id: {0} request_id: {1}",
                                    conn_ctx.connection_handle,
                                    msg.request_id);

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
            case messages::ControlMessageType::kSubscribeDone: {
                messages::SubscribeDone msg;
                msg_bytes >> msg;

                auto sub_it = conn_ctx.tracks_by_request_id.find(msg.request_id);
                if (sub_it == conn_ctx.tracks_by_request_id.end()) {
                    SPDLOG_LOGGER_WARN(logger_,
                                       "Received subscribe done to unknown request_id conn_id: {0} request_id: {1}",
                                       conn_ctx.connection_handle,
                                       msg.request_id);

                    // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race
                    // condition
                    return true;
                }
                auto tfn = sub_it->second->GetFullTrackName();

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received subscribe done conn_id: {0} request_id: {1} track namespace hash: {2} "
                                    "name hash: {3} track alias: {4}",
                                    conn_ctx.connection_handle,
                                    msg.request_id,
                                    TrackHash(tfn).track_namespace_hash,
                                    TrackHash(tfn).track_name_hash,
                                    TrackHash(tfn).track_fullname_hash);

                sub_it->second.get()->SetStatus(SubscribeTrackHandler::Status::kNotSubscribed);
                return true;
            }
            case messages::ControlMessageType::kRequestsBlocked: {
                messages::RequestsBlocked msg;
                msg_bytes >> msg;

                SPDLOG_LOGGER_WARN(logger_, "Subscribe was blocked, maximum_request_id: {}", msg.maximum_request_id);

                // TODO: React to this somehow.
                // See https://www.ietf.org/archive/id/draft-ietf-moq-transport-08.html#section-7.21
                // A publisher MAY send a MAX_REQUEST_ID upon receipt of SUBSCRIBES_BLOCKED, but it MUST NOT rely on
                // SUBSCRIBES_BLOCKED to trigger sending a MAX_REQUEST_ID, because sending SUBSCRIBES_BLOCKED is not
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
                AnnounceStatusChanged(tfn.name_space, PublishAnnounceStatus::kNotAnnounced);
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

                SPDLOG_LOGGER_INFO(logger_, "Received track status for request_id: {}", msg.request_id);
                return true;
            }
            case messages::ControlMessageType::kGoaway: {
                messages::Goaway msg;
                msg_bytes >> msg;

                std::string new_sess_uri(msg.new_session_uri.begin(), msg.new_session_uri.end());
                SPDLOG_LOGGER_INFO(logger_, "Received goaway new session uri: {0}", new_sess_uri);
                return true;
            }
            case messages::ControlMessageType::kServerSetup: {
                messages::ServerSetup msg;
                msg_bytes >> msg;

                std::string endpoint_id = "Unknown Endpoint ID";
                for (const auto& param : msg.setup_parameters) {
                    if (param.type == messages::ParameterType::kEndpointId) {
                        endpoint_id = std::string(param.value.begin(), param.value.end());
                        break; // only looking for 1 endpoint ID
                    }
                }

                ServerSetupReceived({ msg.selected_version, endpoint_id });
                SetStatus(Status::kReady);

                SPDLOG_LOGGER_INFO(logger_,
                                   "Server setup received conn_id: {} from: {} selected_version: {}",
                                   conn_ctx.connection_handle,
                                   endpoint_id,
                                   msg.selected_version);

                conn_ctx.setup_complete = true;
                return true;
            }
            case messages::ControlMessageType::kFetchOk: {
                messages::FetchError msg;
                msg_bytes >> msg;

                auto fetch_it = conn_ctx.tracks_by_request_id.find(msg.request_id);
                if (fetch_it == conn_ctx.tracks_by_request_id.end()) {
                    SPDLOG_LOGGER_WARN(
                      logger_,
                      "Received fetch ok for unknown fetch track conn_id: {0} request_id: {1}, ignored",
                      conn_ctx.connection_handle,
                      msg.request_id);
                    return true;
                }

                fetch_it->second.get()->SetStatus(FetchTrackHandler::Status::kOk);

                return true;
            }
            case messages::ControlMessageType::kFetchError: {
                messages::FetchError msg;
                msg_bytes >> msg;

                auto fetch_it = conn_ctx.tracks_by_request_id.find(msg.request_id);
                if (fetch_it == conn_ctx.tracks_by_request_id.end()) {
                    SPDLOG_LOGGER_WARN(logger_,
                                       "Received fetch error for unknown fetch track conn_id: {} request_id: {} "
                                       "error code: {}, ignored",
                                       conn_ctx.connection_handle,
                                       msg.request_id,
                                       static_cast<std::uint64_t>(msg.error_code));
                    return true;
                }

                SPDLOG_LOGGER_WARN(logger_,
                                   "Received fetch error conn_id: {} request_id: {} "
                                   "error code: {} reason: {}",
                                   conn_ctx.connection_handle,
                                   msg.request_id,
                                   static_cast<std::uint64_t>(msg.error_code),
                                   std::string(msg.error_reason.begin(), msg.error_reason.end()));

                fetch_it->second.get()->SetStatus(FetchTrackHandler::Status::kError);
                conn_ctx.tracks_by_request_id.erase(fetch_it);

                return true;
            }
            case messages::ControlMessageType::kNewGroupRequest: {
                messages::NewGroupRequest msg;
                msg_bytes >> msg;

                try {
                    if (auto handler = conn_ctx.pub_tracks_by_track_alias.at(msg.track_alias)) {
                        handler->SetStatus(PublishTrackHandler::Status::kNewGroupRequested);
                    }
                } catch (const std::exception& e) {
                    SPDLOG_LOGGER_ERROR(
                      logger_, "Failed to fins publisher by alias: {} error: {}", msg.track_alias, e.what());
                }

                return true;
            }
            default:
                SPDLOG_LOGGER_ERROR(logger_,
                                    "Unsupported MOQT message type: {0}, bad stream",
                                    static_cast<uint64_t>(*conn_ctx.ctrl_msg_type_received));
                return false;

        } // End of switch(msg type)

        return false;
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_, "Caught exception trying to process control message. (error={})", e.what());
        return false;
    }
} // namespace quicr
