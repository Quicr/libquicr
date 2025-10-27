// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/detail/transport.h"

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

    void Client::PublishNamespaceStatusChanged(const TrackNamespace&, const PublishNamespaceStatus) {}
    void Client::PublishNamespaceReceived(const TrackNamespace&, const PublishNamespaceAttributes&) {}
    void Client::PublishNamespaceDoneReceived(const TrackNamespace&) {}

    void Client::SubscribeNamespaceStatusChanged(const TrackNamespace&,
                                                 std::optional<messages::SubscribeNamespaceErrorCode>,
                                                 std::optional<messages::ReasonPhrase>)
    {
    }

    void Client::UnpublishedSubscribeReceived(const FullTrackName&, const messages::SubscribeAttributes&)
    {
        // TODO: add the default response
    }

    void Client::ResolveSubscribe(ConnectionHandle connection_handle,
                                  uint64_t request_id,
                                  uint64_t track_alias,
                                  const SubscribeResponse& subscribe_response)
    {
        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        switch (subscribe_response.reason_code) {
            case SubscribeResponse::ReasonCode::kOk: {
                SendSubscribeOk(
                  conn_it->second, request_id, track_alias, kSubscribeExpires, subscribe_response.largest_location);
                break;
            }

            default:
                SendSubscribeError(
                  conn_it->second, request_id, messages::SubscribeErrorCode::kInternalError, "Internal error");
                break;
        }
    }

    void Client::StandaloneFetchReceived(ConnectionHandle,
                                         uint64_t,
                                         const FullTrackName&,
                                         const quicr::messages::StandaloneFetchAttributes&)
    {
    }

    void Client::JoiningFetchReceived(ConnectionHandle,
                                      uint64_t,
                                      const FullTrackName&,
                                      const quicr::messages::JoiningFetchAttributes&)
    {
    }

    void Client::FetchCancelReceived(ConnectionHandle, uint64_t) {}

    void Client::ResolveFetch(ConnectionHandle connection_handle,
                              uint64_t request_id,
                              messages::SubscriberPriority priority,
                              messages::GroupOrder group_order,
                              const FetchResponse& response)
    {
        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        switch (response.reason_code) {
            case FetchResponse::ReasonCode::kOk: {
                SendFetchOk(conn_it->second, request_id, group_order, priority, response.largest_location.value());
                break;
            }
            default:
                SendFetchError(conn_it->second,
                               request_id,
                               messages::FetchErrorCode::kInternalError,
                               response.error_reason.has_value() ? response.error_reason.value() : "Internal error");
                break;
        }
    }

    void Client::BindFetchTrack(TransportConnId conn_id, std::shared_ptr<PublishFetchHandler> track_handler)
    {
        const std::uint64_t request_id = *track_handler->GetRequestId();

        SPDLOG_LOGGER_INFO(logger_, "Publish fetch track conn_id: {0} subscribe: {1}", conn_id, request_id);

        std::lock_guard lock(state_mutex_);

        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Publish fetch track conn_id: {0} does not exist.", conn_id);
            return;
        }

        track_handler->connection_handle_ = conn_id;
        track_handler->publish_data_ctx_id_ =
          quic_transport_->CreateDataContext(conn_id, true, track_handler->GetDefaultPriority(), false);

        track_handler->SetTransport(GetSharedPtr());

        conn_it->second.pub_fetch_tracks_by_request_id[request_id] = std::move(track_handler);
    }

    void Client::UnbindFetchTrack(ConnectionHandle connection_handle,
                                  const std::shared_ptr<PublishFetchHandler>& track_handler)
    {
        std::lock_guard lock(state_mutex_);

        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }
        auto request_id = *track_handler->GetRequestId();
        SPDLOG_LOGGER_DEBUG(
          logger_, "Client publish fetch track conn_id: {} subscribe id: {} unbind", connection_handle, request_id);

        conn_it->second.pub_fetch_tracks_by_request_id.erase(request_id);
        quic_transport_->DeleteDataContext(connection_handle, track_handler->publish_data_ctx_id_, true);
    }

    PublishTrackHandler::PublishObjectStatus Client::SendFetchObject(PublishFetchHandler& track_handler,
                                                                     uint8_t priority,
                                                                     uint32_t ttl,
                                                                     bool stream_header_needed,
                                                                     uint64_t group_id,
                                                                     uint64_t subgroup_id,
                                                                     uint64_t object_id,
                                                                     std::optional<Extensions> extensions,
                                                                     BytesSpan data) const
    {
        const auto request_id = *track_handler.GetRequestId();

        ITransport::EnqueueFlags eflags;

        track_handler.object_msg_buffer_.clear();

        // use stream per subgroup, group change
        eflags.use_reliable = true;

        if (stream_header_needed) {
            eflags.new_stream = true;
            eflags.clear_tx_queue = true;
            eflags.use_reset = true;

            messages::FetchHeader fetch_header{};
            fetch_header.request_id = request_id;
            track_handler.object_msg_buffer_ << fetch_header;

            quic_transport_->Enqueue(track_handler.connection_handle_,
                                     track_handler.publish_data_ctx_id_,
                                     group_id,
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
        }

        messages::FetchObject object{};
        object.group_id = group_id;
        object.subgroup_id = subgroup_id;
        object.object_id = object_id;
        object.publisher_priority = priority;
        object.extensions = extensions;
        object.payload.assign(data.begin(), data.end());
        track_handler.object_msg_buffer_ << object;

        quic_transport_->Enqueue(track_handler.connection_handle_,
                                 track_handler.publish_data_ctx_id_,
                                 group_id,
                                 std::make_shared<std::vector<uint8_t>>(track_handler.object_msg_buffer_.begin(),
                                                                        track_handler.object_msg_buffer_.end()),
                                 priority,
                                 ttl,
                                 0,
                                 eflags);
        return PublishTrackHandler::PublishObjectStatus::kOk;
    }

    void Client::MetricsSampled(const ConnectionMetrics&) {}

    PublishNamespaceStatus Client::GetPublishNamespaceStatus(const TrackNamespace&)
    {
        return PublishNamespaceStatus();
    }

    void Client::PublishNamespace(const TrackNamespace&) {}

    void Client::PublishNamespaceDone(const TrackNamespace&) {}

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

                auto tfn = FullTrackName{ msg.track_namespace, msg.track_name };
                auto th = TrackHash(tfn);

                conn_ctx.recv_req_id[msg.request_id] = { .track_full_name = tfn, .track_hash = th };

                // For client/publisher, notify track that there is a subscriber
                auto ptd = GetPubTrackHandler(conn_ctx, th);
                if (ptd == nullptr) {
                    SPDLOG_LOGGER_WARN(logger_,
                                       "Received subscribe unknown publish track conn_id: {} namespace hash: {} "
                                       "name hash: {} request_id: {}",
                                       conn_ctx.connection_handle,
                                       th.track_namespace_hash,
                                       th.track_name_hash,
                                       msg.request_id);

                    SendSubscribeError(conn_ctx,
                                       msg.request_id,
                                       messages::SubscribeErrorCode::kTrackNotExist,
                                       "Published track not found");
                    return true;
                }

                ResolveSubscribe(conn_ctx.connection_handle,
                                 msg.request_id,
                                 ptd->GetTrackAlias().value(),
                                 { SubscribeResponse::ReasonCode::kOk });

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received subscribe to announced track alias: {0} recv request_id: {1}, setting "
                                    "send state to ready",
                                    ptd->GetTrackAlias().value(),
                                    msg.request_id);

                // Indicate send is ready upon subscribe
                ptd->SetRequestId(msg.request_id);
                ptd->SetTrackAlias(ptd->GetTrackAlias().value());
                ptd->SetStatus(PublishTrackHandler::Status::kOk);

                conn_ctx.recv_req_id[msg.request_id] = { .track_full_name = tfn, .track_hash = th };
                return true;
            }
            case messages::ControlMessageType::kSubscribeUpdate: {
                messages::SubscribeUpdate msg;
                msg_bytes >> msg;

                if (conn_ctx.recv_req_id.count(msg.subscription_request_id) == 0) {
                    // update for invalid subscription
                    SPDLOG_LOGGER_WARN(
                      logger_,
                      "Received subscribe_update request_id: {} for unknown subscription request_id: {} conn_id: {}",
                      msg.request_id,
                      msg.subscription_request_id,
                      conn_ctx.connection_handle);

                    SendSubscribeError(
                      conn_ctx, msg.request_id, messages::SubscribeErrorCode::kTrackNotExist, "Subscription not found");
                    return true;
                }

                auto th = conn_ctx.recv_req_id[msg.subscription_request_id].track_hash;

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
                                       messages::SubscribeErrorCode::kTrackNotExist,
                                       "Published track not found");
                    return true;
                }

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received subscribe_update to track alias: {} recv request_id: {} subscribe "
                                    "request_id: {} forward: {}",
                                    th.track_fullname_hash,
                                    msg.request_id,
                                    msg.subscription_request_id,
                                    msg.forward);

                if (not msg.forward) {
                    ptd->SetStatus(PublishTrackHandler::Status::kPaused);
                } else {
                    uint64_t new_group_request_id{ 0 };

                    for (const auto& param : msg.parameters) {
                        if (param.type == messages::ParameterType::kNewGroupRequest) {
                            std::memcpy(&new_group_request_id,
                                        param.value.data(),
                                        param.value.size() > sizeof(uint64_t) ? sizeof(uint64_t) : param.value.size());

                            if (!(ptd->pending_new_group_request_id_.has_value() &&
                                  *ptd->pending_new_group_request_id_ == 0 && new_group_request_id == 0) &&
                                (new_group_request_id == 0 || ptd->latest_group_id_ < new_group_request_id)) {

                                ptd->pending_new_group_request_id_ = new_group_request_id;
                                ptd->SetStatus(PublishTrackHandler::Status::kNewGroupRequested);
                            }
                            return true;
                        }
                    }

                    ptd->SetStatus(PublishTrackHandler::Status::kSubscriptionUpdated);
                }

                return true;
            }
            case messages::ControlMessageType::kSubscribeOk: {
                messages::SubscribeOk msg([](messages::SubscribeOk& msg) {
                    if (msg.content_exists == 1) {
                        msg.group_0 = std::make_optional<messages::SubscribeOk::Group_0>();
                    }
                });
                msg_bytes >> msg;

                auto sub_it = conn_ctx.sub_tracks_by_request_id.find(msg.request_id);

                if (sub_it == conn_ctx.sub_tracks_by_request_id.end()) {
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

                    if (msg.group_0.has_value())
                        sub_it->second.get()->SetLatestLocation(msg.group_0->largest_location);
                }

                for (const auto& param : msg.parameters) {
                    if (param.type == messages::ParameterType::kDynamicGroups) {
                        sub_it->second->support_new_group_request_ = true;
                        break;
                    }
                }

                sub_it->second.get()->SetReceivedTrackAlias(msg.track_alias);
                conn_ctx.sub_by_recv_track_alias[msg.track_alias] = sub_it->second;

                if (msg.group_0.has_value())
                    sub_it->second.get()->SetLatestLocation(msg.group_0.value().largest_location);

                sub_it->second.get()->SetStatus(SubscribeTrackHandler::Status::kOk);

                return true;
            }
            case messages::ControlMessageType::kSubscribeError: {
                messages::SubscribeError msg;
                msg_bytes >> msg;

                auto sub_it = conn_ctx.sub_tracks_by_request_id.find(msg.request_id);

                if (sub_it == conn_ctx.sub_tracks_by_request_id.end()) {
                    SPDLOG_LOGGER_WARN(
                      logger_,
                      "Received subscribe error to unknown request_id conn_id: {0} request_id: {1}, ignored",
                      conn_ctx.connection_handle,
                      msg.request_id);

                    // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race
                    // condition
                    return true;
                }

                SPDLOG_LOGGER_INFO(logger_,
                                   "Received subscribe error conn_id: {} request_id: {} reason: {} code: {}",
                                   conn_ctx.connection_handle,
                                   msg.request_id,
                                   std::string(msg.error_reason.begin(), msg.error_reason.end()),
                                   static_cast<std::uint64_t>(msg.error_code));

                sub_it->second.get()->SetStatus(SubscribeTrackHandler::Status::kError);
                RemoveSubscribeTrack(conn_ctx, *sub_it->second);
                return true;
            }
            case messages::ControlMessageType::kPublishNamespace: {
                messages::PublishNamespace msg;
                msg_bytes >> msg;

                PublishNamespaceReceived(msg.track_namespace, { .request_id = msg.request_id });
                return true;
            }
            case messages::ControlMessageType::kPublishNamespaceDone: {
                messages::PublishNamespaceDone msg;
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, {} };
                PublishNamespaceDoneReceived(tfn.name_space);

                return true;
            }
            case messages::ControlMessageType::kPublishNamespaceOk: {
                messages::PublishNamespaceOk msg;
                msg_bytes >> msg;

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received publish namespace ok, conn_id: {} request_id: {}",
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
            case messages::ControlMessageType::kPublishNamespaceError: {
                messages::PublishNamespaceError msg;
                msg_bytes >> msg;

                std::string reason = "unknown";
                reason.assign(msg.error_reason.begin(), msg.error_reason.end());

                SPDLOG_LOGGER_INFO(logger_,
                                   "Received publish namespace error for request_id: {} error code: {} reason: {}",
                                   msg.request_id,
                                   static_cast<std::uint64_t>(msg.error_code),
                                   reason);

                return true;
            }
            case messages::ControlMessageType::kSubscribeNamespaceOk: {
                messages::SubscribeNamespaceOk msg;
                msg_bytes >> msg;

                const auto it = conn_ctx.sub_namespace_prefix_by_request_id.find(msg.request_id);
                if (it != conn_ctx.sub_namespace_prefix_by_request_id.end()) {
                    SubscribeNamespaceStatusChanged(it->second, std::nullopt, std::nullopt);
                }

                return true;
            }
            case messages::ControlMessageType::kSubscribeNamespaceError: {
                messages::SubscribeNamespaceError msg;
                msg_bytes >> msg;

                const auto it = conn_ctx.sub_namespace_prefix_by_request_id.find(msg.request_id);
                if (it != conn_ctx.sub_namespace_prefix_by_request_id.end()) {
                    SubscribeNamespaceStatusChanged(it->second, std::nullopt, std::nullopt);

                    auto error_code = static_cast<messages::SubscribeNamespaceErrorCode>(msg.error_code);
                    SubscribeNamespaceStatusChanged(
                      it->second, error_code, std::make_optional<messages::ReasonPhrase>(msg.error_reason));
                }

                return true;
            }
            case messages::ControlMessageType::kUnsubscribe: {
                messages::Unsubscribe msg;
                msg_bytes >> msg;

                const auto& th_it = conn_ctx.recv_req_id.find(msg.request_id);

                if (th_it == conn_ctx.recv_req_id.end()) {
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
            case messages::ControlMessageType::kPublishDone: {
                messages::PublishDone msg;
                msg_bytes >> msg;

                auto sub_it = conn_ctx.sub_tracks_by_request_id.find(msg.request_id);
                if (sub_it == conn_ctx.sub_tracks_by_request_id.end()) {
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
                                    "Received publish done conn_id: {0} request_id: {1} track namespace hash: {2} "
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
            case messages::ControlMessageType::kPublishNamespaceCancel: {
                messages::PublishNamespaceCancel msg;
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, {} };
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_INFO(
                  logger_, "Received publish namespace cancel for namespace_hash: {0}", th.track_namespace_hash);
                PublishNamespaceStatusChanged(tfn.name_space, PublishNamespaceStatus::kNotPublished);
                return true;
            }
            case messages::ControlMessageType::kTrackStatus: {
                auto msg = messages::TrackStatus(
                  [](messages::TrackStatus& msg) {
                      if (msg.filter_type == messages::FilterType::kAbsoluteStart ||
                          msg.filter_type == messages::FilterType::kAbsoluteRange) {
                          msg.group_0 = std::make_optional<messages::TrackStatus::Group_0>();
                      }
                  },
                  [](messages::TrackStatus& msg) {
                      if (msg.filter_type == messages::FilterType::kAbsoluteRange) {
                          msg.group_1 = std::make_optional<messages::TrackStatus::Group_1>();
                      }
                  });

                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, msg.track_name };
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received track status request for namespace_hash: {0} name_hash: {1}",
                                    th.track_namespace_hash,
                                    th.track_name_hash);

                TrackStatusReceived(conn_ctx.connection_handle,
                                    msg.request_id,
                                    tfn,
                                    { msg.subscriber_priority,
                                      static_cast<messages::GroupOrder>(msg.group_order),
                                      std::chrono::milliseconds{ 0 },
                                      msg.forward });
                return true;
            }
            case messages::ControlMessageType::kTrackStatusOk: {
                auto msg = messages::TrackStatusOk([](messages::TrackStatusOk& msg) {
                    if (msg.content_exists == 1) {
                        msg.group_0 = std::make_optional<messages::TrackStatusOk::Group_0>();
                    }
                });
                msg_bytes >> msg;

                std::optional<messages::LargestLocation> largest_location;

                if (msg.group_0.has_value()) {
                    largest_location = msg.group_0->largest_location;
                }

                SPDLOG_LOGGER_DEBUG(
                  logger_,
                  "Received track status for request_id: {} has_content: {} largest group: {} largest object: {}",
                  msg.request_id,
                  msg.content_exists ? "Yes" : "No",
                  largest_location.has_value() ? largest_location->group : 0,
                  largest_location.has_value() ? largest_location->object : 0);

                TrackStatusResponseReceived(conn_ctx.connection_handle,
                                            msg.request_id,
                                            { SubscribeResponse::ReasonCode::kOk, std::nullopt, largest_location });

                return true;
            }
            case messages::ControlMessageType::kTrackStatusError: {
                auto msg = messages::TrackStatusError{};
                msg_bytes >> msg;

                SubscribeResponse response;
                response.error_reason = std::string(msg.error_reason.begin(), msg.error_reason.end());

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received track status error request_id: {} error code: {} reason: {}",
                                    msg.request_id,
                                    static_cast<std::uint64_t>(msg.error_code),
                                    response.error_reason.value());

                switch (msg.error_code) {
                    case messages::SubscribeErrorCode::kUnauthorized:
                        response.reason_code = SubscribeResponse::ReasonCode::kUnauthorized;
                        break;
                    case messages::SubscribeErrorCode::kTrackDoesNotExist:
                        response.reason_code = SubscribeResponse::ReasonCode::kTrackDoesNotExist;
                        break;
                    default:
                        response.reason_code = SubscribeResponse::ReasonCode::kInternalError;
                        break;
                }

                TrackStatusResponseReceived(conn_ctx.connection_handle, msg.request_id, response);
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
                    if (param.type == messages::SetupParameterType::kEndpointId) {
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
                messages::FetchOk msg;
                msg_bytes >> msg;

                auto fetch_it = conn_ctx.sub_tracks_by_request_id.find(msg.request_id);
                if (fetch_it == conn_ctx.sub_tracks_by_request_id.end()) {
                    SPDLOG_LOGGER_WARN(
                      logger_,
                      "Received fetch ok for unknown fetch track conn_id: {0} request_id: {1}, ignored",
                      conn_ctx.connection_handle,
                      msg.request_id);
                    return true;
                }

                fetch_it->second.get()->SetLatestLocation(msg.end_location);
                fetch_it->second.get()->SetStatus(FetchTrackHandler::Status::kOk);

                return true;
            }
            case messages::ControlMessageType::kFetchError: {
                messages::FetchError msg;
                msg_bytes >> msg;

                auto fetch_it = conn_ctx.sub_tracks_by_request_id.find(msg.request_id);
                if (fetch_it == conn_ctx.sub_tracks_by_request_id.end()) {
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
                conn_ctx.sub_tracks_by_request_id.erase(fetch_it);

                return true;
            }
            case messages::ControlMessageType::kFetch: {
                auto msg = messages::Fetch(
                  [](messages::Fetch& msg) {
                      if (msg.fetch_type == messages::FetchType::kStandalone) {
                          msg.group_0 = std::make_optional<messages::Fetch::Group_0>();
                      }
                  },
                  [](messages::Fetch& msg) {
                      if (msg.fetch_type == messages::FetchType::kRelativeJoiningFetch ||
                          msg.fetch_type == messages::FetchType::kAbsoluteJoiningFetch) {
                          msg.group_1 = std::make_optional<messages::Fetch::Group_1>();
                      }
                  });

                msg_bytes >> msg;
                bool relative_joining{ false };

                switch (msg.fetch_type) {
                    case messages::FetchType::kStandalone: {
                        FullTrackName tfn{ msg.group_0->standalone.track_namespace,
                                           msg.group_0->standalone.track_name };

                        messages::StandaloneFetchAttributes attrs = {
                            .priority = msg.subscriber_priority,
                            .group_order = msg.group_order,
                            .start_location = msg.group_0->standalone.start,
                            .end_location = { .group = msg.group_0->standalone.end.group,
                                              .object = msg.group_0->standalone.end.object > 0
                                                          ? msg.group_0->standalone.end.object - 1
                                                          : 0 },
                        };

                        StandaloneFetchReceived(conn_ctx.connection_handle, msg.request_id, tfn, attrs);
                        return true;
                    }
                    case messages::FetchType::kRelativeJoiningFetch: {
                        [[fallthrough]];
                    }
                    case messages::FetchType::kAbsoluteJoiningFetch: {
                        const auto subscribe_state = conn_ctx.recv_req_id.find(msg.group_1->joining.request_id);
                        if (subscribe_state == conn_ctx.recv_req_id.end()) {
                            SendFetchError(conn_ctx,
                                           msg.request_id,
                                           messages::FetchErrorCode::kTrackDoesNotExist,
                                           "Corresponding subscribe does not exist");
                            return true;
                        }

                        FullTrackName tfn = subscribe_state->second.track_full_name;

                        messages::JoiningFetchAttributes attrs = {
                            .priority = msg.subscriber_priority,
                            .group_order = msg.group_order,
                            .joining_request_id = msg.group_1->joining.request_id,
                            .relative = relative_joining,
                            .joining_start = msg.group_1->joining.joining_start,
                        };

                        JoiningFetchReceived(conn_ctx.connection_handle, msg.request_id, tfn, attrs);
                        return true;
                    }
                    default: {
                        SendFetchError(
                          conn_ctx, msg.request_id, messages::FetchErrorCode::kNotSupported, "Unknown fetch type");
                        return true;
                    }
                }
            }
            case messages::ControlMessageType::kFetchCancel: {
                messages::FetchCancel msg;
                msg_bytes >> msg;

                if (conn_ctx.recv_req_id.find(msg.request_id) == conn_ctx.recv_req_id.end()) {
                    SPDLOG_LOGGER_WARN(logger_, "Received Fetch Cancel for unknown subscribe ID: {0}", msg.request_id);
                }

                const auto fetch_it = conn_ctx.sub_tracks_by_request_id.find(msg.request_id);
                if (fetch_it == conn_ctx.sub_tracks_by_request_id.end()) {
                    FetchCancelReceived(conn_ctx.connection_handle, msg.request_id);
                    return true;
                }

                fetch_it->second->SetStatus(FetchTrackHandler::Status::kCancelled);

                FetchCancelReceived(conn_ctx.connection_handle, msg.request_id);

                conn_ctx.sub_tracks_by_request_id.erase(fetch_it);
                conn_ctx.recv_req_id.erase(msg.request_id);

                return true;
            }
            case messages::ControlMessageType::kPublishOk: {
                messages::PublishOk msg(
                  [](messages::PublishOk& msg) {
                      if (msg.filter_type == messages::FilterType::kAbsoluteStart ||
                          msg.filter_type == messages::FilterType::kAbsoluteRange) {
                          msg.group_0 =
                            std::make_optional<messages::PublishOk::Group_0>(messages::PublishOk::Group_0{ 0, 0 });
                      }
                  },
                  [](messages::PublishOk& msg) {
                      if (msg.filter_type == messages::FilterType::kAbsoluteRange) {
                          msg.group_1 =
                            std::make_optional<messages::PublishOk::Group_1>(messages::PublishOk::Group_1{ 0 });
                      }
                  });
                msg_bytes >> msg;

                auto pub_it = conn_ctx.pub_tracks_by_request_id.find(msg.request_id);

                if (pub_it == conn_ctx.pub_tracks_by_request_id.end()) {
                    SPDLOG_LOGGER_WARN(
                      logger_,
                      "Received publish ok to unknown publish track conn_id: {0} request_id: {1}, ignored",
                      conn_ctx.connection_handle,
                      msg.request_id);

                    return true;
                }

                if (msg.group_0.has_value()) {
                    SPDLOG_LOGGER_DEBUG(
                      logger_,
                      "Received publish ok conn_id: {} request_id: {} start_group: {} start_object: {} endgroup: {}",
                      conn_ctx.connection_handle,
                      msg.request_id,
                      msg.group_0->start_location.group,
                      msg.group_0->start_location.object,
                      msg.group_1->end_group);
                }
                pub_it->second.get()->SetStatus(PublishTrackHandler::Status::kOk);
                return true;
            }
            default: {
                SPDLOG_LOGGER_ERROR(logger_,
                                    "Unsupported MOQT message type: {0}, bad stream",
                                    static_cast<uint64_t>(*conn_ctx.ctrl_msg_type_received));
                return false;
            }
        }

        return false;

    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_,
                            "Caught exception trying to process control message. (type={}, error={})",
                            static_cast<int>(*conn_ctx.ctrl_msg_type_received),
                            e.what());
        return false;
    }
} // namespace quicr
