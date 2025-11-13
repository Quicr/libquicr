// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/detail/messages.h>
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

    void Server::PublishNamespaceReceived(ConnectionHandle, const TrackNamespace&, const PublishNamespaceAttributes&) {}

    void Server::UnsubscribeNamespaceReceived(ConnectionHandle, const TrackNamespace&) {}

    void Server::ResolvePublishNamespace(ConnectionHandle connection_handle,
                                         uint64_t request_id,
                                         const TrackNamespace& track_namespace,
                                         const std::vector<ConnectionHandle>& subscribers,
                                         const PublishNamespaceResponse& response)
    {
        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        switch (response.reason_code) {
            case PublishNamespaceResponse::ReasonCode::kOk: {
                SendPublishNamespaceOk(conn_it->second, request_id);

                for (const auto& sub_conn_handle : subscribers) {
                    auto it = connections_.find(sub_conn_handle);
                    if (it == connections_.end()) {
                        continue;
                    }

                    // TODO: what request Id do we send for subscribe announces???
                    SendPublishNamespace(it->second, request_id, track_namespace);
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
                                   const FullTrackName&,
                                   const messages::SubscribeAttributes&)
    {
    }

    void Server::StandaloneFetchReceived(ConnectionHandle,
                                         uint64_t,
                                         const FullTrackName&,
                                         const quicr::messages::StandaloneFetchAttributes&)
    {
    }

    void Server::JoiningFetchReceived(ConnectionHandle,
                                      uint64_t,
                                      const FullTrackName&,
                                      const quicr::messages::JoiningFetchAttributes&)
    {
    }

    void Server::FetchCancelReceived(ConnectionHandle, uint64_t) {}

    void Server::NewGroupRequested(const FullTrackName&, messages::GroupId) {}

    void Server::ResolveSubscribe(ConnectionHandle connection_handle,
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
                // Save the latest state for joining fetch.
                auto req_it = conn_it->second.recv_req_id.find(request_id);
                if (req_it == conn_it->second.recv_req_id.end()) {
                    SPDLOG_LOGGER_WARN(logger_,
                                       "Resolve subscribe has no request_id: {} conn_id: {} track_alias: {}",
                                       request_id,
                                       connection_handle,
                                       track_alias);
                    break;
                }

                req_it->second.largest_location = subscribe_response.largest_location;

                if (!subscribe_response.is_publisher_initiated) {
                    // Send the ok.
                    SendSubscribeOk(
                      conn_it->second, request_id, track_alias, kSubscribeExpires, subscribe_response.largest_location);
                }
                break;
            }
            default:
                if (!subscribe_response.is_publisher_initiated) {
                    SendSubscribeError(
                      conn_it->second, request_id, messages::SubscribeErrorCode::kInternalError, "Internal error");
                }
                break;
        }
    }

    void Server::ResolveSubscribeNamespace(const ConnectionHandle connection_handle,
                                           const uint64_t request_id,
                                           const messages::TrackNamespacePrefix& prefix,
                                           const SubscribeNamespaceResponse& response)
    {
        const auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        if (response.reason_code != SubscribeNamespaceResponse::ReasonCode::kOk) {
            SendSubscribeNamespaceError(
              conn_it->second, request_id, messages::SubscribeNamespaceErrorCode::kInternalError, "Internal error");
            return;
        }

        SendSubscribeNamespaceOk(conn_it->second, request_id);

        // Fan out PUBLISH_NAMESPACE for matching namespaces.
        for (const auto& name_space : response.namespaces) {
            if (!prefix.IsPrefixOf(name_space)) {
                SPDLOG_LOGGER_WARN(logger_, "Dropping non prefix match");
                continue;
            }
            SendPublishNamespace(conn_it->second, conn_it->second.GetNextRequestId(), name_space);
        }

        // Fan out PUBLISH for matching tracks.
        for (const auto& track : response.tracks) {
            const auto pub_request_id = conn_it->second.GetNextRequestId();
            conn_it->second.pub_by_request_id[pub_request_id] = track.track_full_name;
            SendPublish(conn_it->second,
                        pub_request_id,
                        track.track_full_name,
                        track.attributes.track_alias,
                        track.attributes.group_order,
                        track.largest_location,
                        track.attributes.forward,
                        track.attributes.new_group_request_id.has_value());
        }
    }

    void Server::ResolveFetch(ConnectionHandle connection_handle,
                              uint64_t request_id,
                              messages::SubscriberPriority priority,
                              messages::GroupOrder group_order,
                              const FetchResponse& response)
    {
        auto error_code = messages::FetchErrorCode::kInternalError;

        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        switch (response.reason_code) {
            case FetchResponse::ReasonCode::kOk:
                SendFetchOk(conn_it->second, request_id, group_order, priority, response.largest_location.value());
                return;

            case FetchResponse::ReasonCode::kInvalidRange:
                error_code = messages::FetchErrorCode::kInvalidRange;
                break;
            case FetchResponse::ReasonCode::kNoObjects:
                error_code = messages::FetchErrorCode::kNoObjects;
                break;

            default:
                break;
        }

        SendFetchError(conn_it->second,
                       request_id,
                       error_code,
                       response.error_reason.has_value() ? response.error_reason.value() : "Internal error");
    }

    void Server::UnbindPublisherTrack(ConnectionHandle connection_handle,
                                      ConnectionHandle src_id,
                                      const std::shared_ptr<PublishTrackHandler>& track_handler)
    {
        std::lock_guard lock(state_mutex_);

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

        conn_it->second.pub_tracks_ns_by_request_id.erase(*track_handler->GetRequestId());
        conn_it->second.pub_tracks_by_name[th.track_namespace_hash].erase(th.track_name_hash);

        conn_it->second.pub_tracks_by_track_alias[th.track_fullname_hash].erase(src_id);
        if (conn_it->second.pub_tracks_by_track_alias[th.track_fullname_hash].empty()) {
            conn_it->second.pub_tracks_by_track_alias.erase(th.track_fullname_hash);
        }

        if (conn_it->second.pub_tracks_by_name.count(th.track_namespace_hash) == 0) {
            SPDLOG_LOGGER_DEBUG(logger_,
                                "Server publish track conn_id: {} full_name_hash: {} namespace_hash: {} unbind",
                                connection_handle,
                                th.track_fullname_hash,
                                th.track_namespace_hash);

            conn_it->second.pub_tracks_by_name.erase(th.track_namespace_hash);
        }

        conn_it->second.pub_tracks_by_data_ctx_id.erase(track_handler->publish_data_ctx_id_);

        quic_transport_->DeleteDataContext(connection_handle, track_handler->publish_data_ctx_id_);
    }

    void Server::BindPublisherTrack(ConnectionHandle conn_id,
                                    uint64_t src_id,
                                    uint64_t request_id,
                                    const std::shared_ptr<PublishTrackHandler>& track_handler,
                                    bool ephemeral)
    {
        // Generate track alias
        const auto& tfn = track_handler->GetFullTrackName();
        auto th = TrackHash(tfn);

        std::unique_lock<std::mutex> lock(state_mutex_);
        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Subscribe track conn_id: {0} does not exist.", conn_id);
            return;
        }

        if (!track_handler->GetTrackAlias().has_value()) {
            track_handler->SetTrackAlias(th.track_fullname_hash);
        }

        track_handler->SetRequestId(request_id);
        conn_it->second.pub_tracks_ns_by_request_id[request_id] = th.track_namespace_hash;

        track_handler->connection_handle_ = conn_id;

        track_handler->publish_data_ctx_id_ =
          quic_transport_->CreateDataContext(conn_id,
                                             track_handler->default_track_mode_ == TrackMode::kDatagram ? false : true,
                                             track_handler->default_priority_,
                                             false);

        // Set this transport as the one for the publisher to use.
        track_handler->SetTransport(GetSharedPtr());

        if (!ephemeral) {
            // Hold onto track handler
            conn_it->second.pub_tracks_by_name[th.track_namespace_hash][th.track_name_hash] = track_handler;
            conn_it->second.pub_tracks_by_track_alias[th.track_fullname_hash][src_id] = track_handler;
            conn_it->second.pub_tracks_by_data_ctx_id[track_handler->publish_data_ctx_id_] = track_handler;
        }

        lock.unlock();
        track_handler->SetStatus(PublishTrackHandler::Status::kOk);
    }

    void Server::UnbindFetchTrack(ConnectionHandle connection_handle,
                                  const std::shared_ptr<PublishFetchHandler>& track_handler)
    {
        std::lock_guard lock(state_mutex_);

        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }
        auto request_id = *track_handler->GetRequestId();
        SPDLOG_LOGGER_DEBUG(
          logger_, "Server publish fetch track conn_id: {} subscribe id: {} unbind", connection_handle, request_id);

        conn_it->second.pub_fetch_tracks_by_request_id.erase(request_id);
        quic_transport_->DeleteDataContext(connection_handle, track_handler->publish_data_ctx_id_, true);
    }

    void Server::BindFetchTrack(TransportConnId conn_id, std::shared_ptr<PublishFetchHandler> track_handler)
    {
        const std::uint64_t request_id = *track_handler->GetRequestId();
        SPDLOG_LOGGER_INFO(logger_, "Publish fetch track conn_id: {0} subscribe: {1}", conn_id, request_id);

        std::lock_guard lock(state_mutex_);

        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Publish fetch track conn_id: {0} does not exist.", conn_id);
            return;
        }

        track_handler->SetStatus(PublishFetchHandler::Status::kOk);
        track_handler->connection_handle_ = conn_id;
        track_handler->publish_data_ctx_id_ =
          quic_transport_->CreateDataContext(conn_id, true, track_handler->GetDefaultPriority(), false);

        track_handler->SetTransport(GetSharedPtr());

        // Hold ref to track handler
        conn_it->second.pub_fetch_tracks_by_request_id[request_id] = track_handler;
    }

    PublishTrackHandler::PublishObjectStatus Server::SendFetchObject(PublishFetchHandler& track_handler,
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

    bool Server::ProcessCtrlMessage(ConnectionContext& conn_ctx, BytesSpan msg_bytes)
    try {
        switch (*conn_ctx.ctrl_msg_type_received) {
            case messages::ControlMessageType::kSubscribe: {
                auto msg = messages::Subscribe(
                  [](messages::Subscribe& msg) {
                      if (msg.filter_type == messages::FilterType::kAbsoluteStart ||
                          msg.filter_type == messages::FilterType::kAbsoluteRange) {
                          msg.group_0 = std::make_optional<messages::Subscribe::Group_0>();
                      }
                  },
                  [](messages::Subscribe& msg) {
                      if (msg.filter_type == messages::FilterType::kAbsoluteRange) {
                          msg.group_1 = std::make_optional<messages::Subscribe::Group_1>();
                      }
                  });
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, msg.track_name };
                auto th = TrackHash(tfn);

                conn_ctx.recv_req_id[msg.request_id] = { .track_full_name = tfn, .track_hash = th };

                std::uint64_t delivery_timeout = 0;
                std::optional<uint64_t> new_group_request_id;
                for (const auto& param : msg.parameters) {
                    switch (param.type) {
                        case messages::ParameterType::kDeliveryTimeout: {
                            std::memcpy(&delivery_timeout,
                                        param.value.data(),
                                        param.value.size() > sizeof(uint64_t) ? sizeof(uint64_t) : param.value.size());

                            break;
                        }
                        case messages::ParameterType::kNewGroupRequest: {
                            uint64_t ngr_id{ 0 };
                            std::memcpy(&ngr_id,
                                        param.value.data(),
                                        param.value.size() > sizeof(uint64_t) ? sizeof(uint64_t) : param.value.size());
                            new_group_request_id = ngr_id;
                            break;
                        }
                        default:
                            break;
                    }
                }

                // TODO(tievens): add filter type when caching supports it
                SubscribeReceived(conn_ctx.connection_handle,
                                  msg.request_id,
                                  tfn,
                                  { msg.subscriber_priority,
                                    static_cast<messages::GroupOrder>(msg.group_order),
                                    std::chrono::milliseconds{ delivery_timeout },
                                    msg.filter_type,
                                    msg.forward,
                                    new_group_request_id,
                                    false });

                // Handle new group request after subscribe callback
                if (new_group_request_id.has_value()) {
                    NewGroupRequested({ msg.track_namespace, msg.track_name }, *new_group_request_id);
                }

                return true;
            }
            case messages::ControlMessageType::kSubscribeOk: {
                auto msg = messages::SubscribeOk([](messages::SubscribeOk& msg) {
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
                auto msg = messages::SubscribeError{};
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

                sub_it->second->SetStatus(SubscribeTrackHandler::Status::kError);
                RemoveSubscribeTrack(conn_ctx, *sub_it->second);

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
                                    "Received track status request_id: {} for full name hash: {}",
                                    msg.request_id,
                                    th.track_fullname_hash);

                TrackStatusReceived(conn_ctx.connection_handle,
                                    msg.request_id,
                                    tfn,
                                    { msg.subscriber_priority,
                                      static_cast<messages::GroupOrder>(msg.group_order),
                                      std::chrono::milliseconds{ 0 },
                                      msg.filter_type,
                                      msg.forward });
                return true;
            }
            case messages::ControlMessageType::kTrackStatusOk: {
                auto msg = messages::TrackStatusOk([](messages::TrackStatusOk& msg) {
                    if (msg.content_exists) {
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
                                            { .reason_code = SubscribeResponse::ReasonCode::kOk,
                                              .is_publisher_initiated = false,
                                              .error_reason = std::nullopt,
                                              .largest_location = largest_location });

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
            case messages::ControlMessageType::kPublishNamespace: {
                auto msg = messages::PublishNamespace{};
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, {} };

                PublishNamespaceReceived(conn_ctx.connection_handle, tfn.name_space, { msg.request_id });
                return true;
            }
            case messages::ControlMessageType::kSubscribeNamespace: {
                auto msg = messages::SubscribeNamespace{};
                msg_bytes >> msg;

                SubscribeNamespaceReceived(
                  conn_ctx.connection_handle, msg.track_namespace_prefix, { .request_id = msg.request_id });
                return true;
            }
            case messages::ControlMessageType::kUnsubscribeNamespace: {
                auto msg = messages::UnsubscribeNamespace{};
                msg_bytes >> msg;

                UnsubscribeNamespaceReceived(conn_ctx.connection_handle, msg.track_namespace_prefix);
                return true;
            }
            case messages::ControlMessageType::kPublishNamespaceError: {
                auto msg = messages::PublishNamespaceError{};
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
            case messages::ControlMessageType::kPublishNamespaceDone: {
                messages::PublishNamespaceDone msg;
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, {} };
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_INFO(
                  logger_, "Received publish namespace done for namespace_hash: {0}", th.track_namespace_hash);

                auto sub_namespace_conns = PublishNamespaceDoneReceived(conn_ctx.connection_handle, tfn.name_space);

                std::lock_guard<std::mutex> _(state_mutex_);
                for (auto conn_id : sub_namespace_conns) {
                    auto conn_it = connections_.find(conn_id);
                    if (conn_it == connections_.end()) {
                        continue;
                    }

                    SendPublishNamespaceDone(conn_it->second, msg.track_namespace);
                }

                return true;
            }
            case messages::ControlMessageType::kUnsubscribe: {
                messages::Unsubscribe msg;
                msg_bytes >> msg;

                auto& th = conn_ctx.recv_req_id[msg.request_id].track_hash;
                if (auto pdt = GetPubTrackHandler(conn_ctx, th)) {
                    pdt->SetStatus(PublishTrackHandler::Status::kNoSubscribers);
                }

                UnsubscribeReceived(conn_ctx.connection_handle, msg.request_id);
                conn_ctx.recv_req_id.erase(msg.request_id);

                return true;
            }
            case messages::ControlMessageType::kPublishDone: {
                messages::PublishDone msg;
                msg_bytes >> msg;

                auto sub_it = conn_ctx.sub_tracks_by_request_id.find(msg.request_id);
                if (sub_it == conn_ctx.sub_tracks_by_request_id.end()) {
                    SPDLOG_LOGGER_WARN(logger_,
                                       "Received publish done to unknown subscribe conn_id: {0} request_id: {1}",
                                       conn_ctx.connection_handle,
                                       msg.request_id);

                    return true;
                }
                auto tfn = sub_it->second->GetFullTrackName();
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_INFO(logger_,
                                   "Received publish done conn_id: {0} request_id: {1} track namespace hash: {2} "
                                   "name hash: {3} track alias: {4}",
                                   conn_ctx.connection_handle,
                                   msg.request_id,
                                   th.track_namespace_hash,
                                   th.track_name_hash,
                                   th.track_fullname_hash);

                sub_it->second.get()->SetStatus(SubscribeTrackHandler::Status::kNotSubscribed);

                PublishDoneReceived(conn_ctx.connection_handle, msg.request_id);
                conn_ctx.recv_req_id.erase(msg.request_id);

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
                  logger_, "Received announce cancel for namespace_hash: {0}", th.track_namespace_hash);
                return true;
            }
            case messages::ControlMessageType::kGoaway: {
                messages::Goaway msg;
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

                std::string endpoint_id = "Unknown Endpoint ID";
                for (const auto& param : msg.setup_parameters) {
                    if (param.type == messages::SetupParameterType::kEndpointId) {
                        endpoint_id = std::string(param.value.begin(), param.value.end());
                    }
                }

                ClientSetupReceived(conn_ctx.connection_handle, { endpoint_id });

                SPDLOG_LOGGER_INFO(logger_,
                                   "Client setup received conn_id: {} from: {} num_versions: {} version: {}",
                                   conn_ctx.connection_handle,
                                   endpoint_id,
                                   msg.supported_versions.size(),
                                   msg.supported_versions.front());

                conn_ctx.client_version = msg.supported_versions.front();

                // TODO(tievens): Revisit sending sever setup immediately or wait for something else from server
                SendServerSetup(conn_ctx);
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
                        relative_joining = true;
                        [[fallthrough]];
                    }
                    case messages::FetchType::kAbsoluteJoiningFetch: {

                        // Joining fetch needs to look up its joining subscribe.
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

                return true;
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
            case messages::ControlMessageType::kPublish: {
                auto msg = messages::Publish([](messages::Publish& msg) {
                    if (msg.content_exists) {
                        msg.group_0 = std::make_optional<messages::Publish::Group_0>();
                    }
                });
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, msg.track_name };
                auto th = TrackHash(tfn);

                conn_ctx.recv_req_id[msg.request_id] = { .track_full_name = tfn, .track_hash = th };

                // Params.
                std::uint64_t delivery_timeout = 0;
                bool dynamic_groups = false;
                for (const auto& param : msg.parameters) {
                    switch (param.type) {
                        case messages::ParameterType::kDeliveryTimeout: {
                            std::memcpy(&delivery_timeout,
                                        param.value.data(),
                                        param.value.size() > sizeof(uint64_t) ? sizeof(uint64_t) : param.value.size());

                            break;
                        }
                        case messages::ParameterType::kNewGroupRequest: {
                            uint64_t ngr_id{ 0 };
                            std::memcpy(&ngr_id,
                                        param.value.data(),
                                        param.value.size() > sizeof(uint64_t) ? sizeof(uint64_t) : param.value.size());
                            switch (ngr_id) {
                                case 0:
                                    break;
                                case 1:
                                    dynamic_groups = true;
                                    break;
                                default:
                                    throw messages::ProtocolViolationException("DYNAMIC_GROUPS value MUST be <= 1");
                            }
                            break;
                        }
                        default:
                            break;
                    }
                }

                messages::PublishAttributes attributes;
                attributes.track_full_name = tfn;
                attributes.track_alias = msg.track_alias;
                attributes.priority = 0;
                attributes.group_order = msg.group_order;
                attributes.delivery_timeout = std::chrono::milliseconds(delivery_timeout);
                attributes.filter_type = messages::FilterType::kLargestObject;
                attributes.forward = msg.forward;
                attributes.new_group_request_id = 1;
                attributes.is_publisher_initiated = true;
                attributes.dynamic_groups = dynamic_groups;
                PublishReceived(conn_ctx.connection_handle, msg.request_id, attributes);

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

                // Consume originating request.
                const auto& originating_publish = conn_ctx.pub_by_request_id.find(msg.request_id);
                if (originating_publish == conn_ctx.pub_by_request_id.end()) {
                    SPDLOG_LOGGER_WARN(logger_, "Received publish ok for unknown publish: {}", msg.request_id);
                    return true;
                }
                const FullTrackName tfn = originating_publish->second;
                conn_ctx.pub_by_request_id.erase(originating_publish);

                // Continue with subscribe flow.
                auto th = TrackHash(tfn);
                conn_ctx.recv_req_id[msg.request_id] = { .track_full_name = tfn, .track_hash = th };

                // Delivery timeout.
                std::uint64_t delivery_timeout = 0;
                std::optional<uint64_t> new_group_request_id;
                for (const auto& param : msg.parameters) {
                    switch (param.type) {
                        case messages::ParameterType::kDeliveryTimeout: {
                            std::memcpy(&delivery_timeout,
                                        param.value.data(),
                                        param.value.size() > sizeof(uint64_t) ? sizeof(uint64_t) : param.value.size());

                            break;
                        }
                        case messages::ParameterType::kNewGroupRequest: {
                            uint64_t ngr_id{ 0 };
                            std::memcpy(&ngr_id,
                                        param.value.data(),
                                        param.value.size() > sizeof(uint64_t) ? sizeof(uint64_t) : param.value.size());
                            new_group_request_id = ngr_id;
                            break;
                        }
                        default:
                            break;
                    }
                }
                messages::SubscribeAttributes attributes = { .priority = msg.subscriber_priority,
                                                             .group_order = msg.group_order,
                                                             .delivery_timeout =
                                                               std::chrono::milliseconds(delivery_timeout),
                                                             .filter_type = msg.filter_type,
                                                             .forward = msg.forward,
                                                             .new_group_request_id = new_group_request_id,
                                                             .is_publisher_initiated = true };
                SubscribeReceived(conn_ctx.connection_handle, msg.request_id, tfn, attributes);
                return true;
            }

            case messages::ControlMessageType::kSubscribeUpdate: {
                messages::SubscribeUpdate msg;
                msg_bytes >> msg;

                auto sub_ctx_it = conn_ctx.recv_req_id.find(msg.subscription_request_id);
                if (sub_ctx_it == conn_ctx.recv_req_id.end()) {
                    // update for invalid subscription
                    SPDLOG_LOGGER_WARN(
                      logger_,
                      "Received subscribe_update for unknown subscription conn_id: {} request_id: {} / {}",
                      msg.request_id,
                      msg.subscription_request_id,
                      conn_ctx.connection_handle);

                    SendSubscribeError(
                      conn_ctx, msg.request_id, messages::SubscribeErrorCode::kTrackNotExist, "Subscription not found");
                    return true;
                }

                SPDLOG_LOGGER_DEBUG(
                  logger_,
                  "Received subscribe_update to recv request_id: {} subscribe request_id: {} forward: {}",
                  msg.request_id,
                  msg.subscription_request_id,
                  msg.forward);

                bool new_group_request{ false };
                uint64_t new_group_request_id{ 0 };
                for (const auto& param : msg.parameters) {
                    if (param.type == messages::ParameterType::kNewGroupRequest) {
                        std::memcpy(&new_group_request_id,
                                    param.value.data(),
                                    param.value.size() > sizeof(uint64_t) ? sizeof(uint64_t) : param.value.size());

                        new_group_request = true;
                        NewGroupRequested(sub_ctx_it->second.track_full_name, new_group_request_id);
                        break;
                    }
                }

                /*
                 * Unlike client, server supports multi-publisher to the client.
                 *   There is a publish handler per publisher connection
                 */
                for (const auto& pub :
                     conn_ctx.pub_tracks_by_track_alias[sub_ctx_it->second.track_hash.track_fullname_hash]) {

                    // TODO: Follow up on https://github.com/moq-wg/moq-transport/issues/1304
                    if (not msg.forward) {
                        pub.second->SetStatus(PublishTrackHandler::Status::kPaused);

                    } else {
                        pub.second->SetStatus(new_group_request ? PublishTrackHandler::Status::kNewGroupRequested
                                                                : PublishTrackHandler::Status::kSubscriptionUpdated);
                    }
                }
                return true;
            }
            default: {
                SPDLOG_LOGGER_ERROR(logger_,
                                    "Unsupported MOQT message type: {0}, bad stream",
                                    static_cast<uint64_t>(*conn_ctx.ctrl_msg_type_received));
                return false;
            }

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
