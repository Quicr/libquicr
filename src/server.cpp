// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/detail/messages.h>
#include <quicr/server.h>

namespace quicr {
    using namespace std::chrono_literals;

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
          logger_, "New connection conn_id: {} remote ip: {} port: {}", connection_handle, remote.ip, remote.port);
    }

    void Server::ConnectionStatusChanged(ConnectionHandle, ConnectionStatus) {}

    void Server::MetricsSampled(ConnectionHandle, const ConnectionMetrics&) {}

    void Server::PublishNamespaceReceived(ConnectionHandle, const TrackNamespace&, const PublishNamespaceAttributes&) {}

    void Server::UnsubscribeNamespaceReceived(ConnectionHandle, DataContextId, const TrackNamespace&) {}

    void Server::ResolvePublishNamespace(ConnectionHandle connection_handle,
                                         uint64_t request_id,
                                         const TrackNamespace& track_namespace,
                                         const std::vector<ConnectionHandle>& subscribers,
                                         const PublishNamespaceResponse& response)
    {
        auto th = TrackHash({ track_namespace, {} });
        auto fanout_subscribe_namespace_requestors = [&] {
            for (const auto& sub_conn_handle : subscribers) {
                auto it = connections_.find(sub_conn_handle);
                if (it == connections_.end()) {
                    continue;
                }

                auto next_request_id = it->second.GetNextRequestId();
                SendPublishNamespace(it->second, next_request_id, track_namespace);
                // it->second.tracks_by_request_id[request_id] = next_request_id;
            }
        };

        if (!connection_handle) {
            fanout_subscribe_namespace_requestors();
            return;
        }

        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        switch (response.reason_code) {
            case PublishNamespaceResponse::ReasonCode::kOk: {
                SendRequestOk(conn_it->second, request_id);

                fanout_subscribe_namespace_requestors();
                break;
            }
            default: {
                // TODO: Send announce error
            }
        }
    }

    void Server::ResolvePublishNamespaceDone(ConnectionHandle connection_handle,
                                             messages::RequestID request_id,
                                             const std::vector<ConnectionHandle>& subscribers)
    {
        for (const auto& sub_conn_handle : subscribers) {
            auto it = connections_.find(sub_conn_handle);
            if (it == connections_.end()) {
                continue;
            }

            auto req_it = it->second.request_handlers.find(request_id);
            if (req_it != it->second.request_handlers.end()) {
                SendPublishNamespaceDone(it->second, request_id);
                it->second.request_handlers.erase(req_it);
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
                                  const RequestResponse& subscribe_response)
    {
        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        switch (subscribe_response.reason_code) {
            case RequestResponse::ReasonCode::kOk: {
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
                    SendRequestError(
                      conn_it->second, request_id, messages::ErrorCode::kInternalError, 0ms, "Internal error");
                }
                break;
        }
    }

    void Server::ResolveSubscribeNamespace(const ConnectionHandle connection_handle,
                                           const DataContextId data_ctx_id,
                                           const uint64_t request_id,
                                           const messages::TrackNamespacePrefix& prefix,
                                           const SubscribeNamespaceResponse& response)
    {
        const auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        auto th = TrackHash({ prefix, {} });

        if (response.reason_code != SubscribeNamespaceResponse::ReasonCode::kOk) {
            SendRequestError(conn_it->second, request_id, messages::ErrorCode::kInternalError, 0ms, "Internal error");
            return;
        }

        SendRequestOk(conn_it->second, request_id);

        // Fan out PUBLISH_NAMESPACE for matching namespaces.
        for (const auto& name_space : response.namespaces) {
            if (!prefix.IsPrefixOf(name_space)) {
                SPDLOG_LOGGER_WARN(logger_, "Dropping non prefix match");
                continue;
            }

            auto request_id = conn_it->second.GetNextRequestId();
            SendPublishNamespace(conn_it->second, request_id, name_space);
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
                              std::uint8_t priority,
                              messages::GroupOrder group_order,
                              const FetchResponse& response)
    {
        auto error_code = messages::ErrorCode::kInternalError;

        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        switch (response.reason_code) {
            case FetchResponse::ReasonCode::kOk:
                SendFetchOk(conn_it->second, request_id, group_order, priority, response.largest_location.value());
                return;

            case FetchResponse::ReasonCode::kInvalidRange:
                error_code = messages::ErrorCode::kInvalidRange;
                break;

            default:
                break;
        }

        SendRequestError(conn_it->second,
                         request_id,
                         error_code,
                         0ms,
                         response.error_reason.has_value() ? response.error_reason.value() : "Internal error");
    }

    void Server::UnbindPublisherTrack(ConnectionHandle connection_handle,
                                      ConnectionHandle src_id,
                                      const std::shared_ptr<PublishTrackHandler>& track_handler,
                                      bool send_publish_done)
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

        conn_it->second.request_handlers.erase(*track_handler->GetRequestId());
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

        if (send_publish_done) {
            SendPublishDone(conn_it->second,
                            track_handler->GetRequestId().value(),
                            messages::PublishDoneStatusCode::kSubscribtionEnded,
                            "No publishers");
        }
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
            SPDLOG_LOGGER_ERROR(logger_, "Subscribe track conn_id: {} does not exist.", conn_id);
            return;
        }

        if (!track_handler->GetTrackAlias().has_value()) {
            track_handler->SetTrackAlias(th.track_fullname_hash);
        }

        track_handler->SetRequestId(request_id);
        conn_it->second.request_handlers[request_id] = track_handler;

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
        SPDLOG_LOGGER_INFO(logger_, "Publish fetch track conn_id: {} subscribe: {}", conn_id, request_id);

        std::lock_guard lock(state_mutex_);

        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Publish fetch track conn_id: {} does not exist.", conn_id);
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

    bool Server::ProcessCtrlMessage(ConnectionContext& conn_ctx,
                                    uint64_t data_ctx_id,
                                    messages::ControlMessageType msg_type,
                                    BytesSpan msg_bytes)
    try {
        switch (msg_type) {
            case messages::ControlMessageType::kSubscribe: {
                messages::Subscribe msg{};
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, msg.track_name };
                auto th = TrackHash(tfn);

                conn_ctx.recv_req_id[msg.request_id] = { .track_full_name = tfn, .track_hash = th };

                auto delivery_timeout = msg.parameters.Get<std::uint64_t>(messages::ParameterType::kDeliveryTimeout);
                auto priority = msg.parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority);
                auto group_order = msg.parameters.Get<messages::GroupOrder>(messages::ParameterType::kGroupOrder);
                auto filter_type =
                  msg.parameters.Get<messages::FilterType>(messages::ParameterType::kSubscriptionFilter);
                auto forward = msg.parameters.Get<bool>(messages::ParameterType::kForward);

                std::optional<uint64_t> new_group_request_id;
                if (msg.parameters.Contains(messages::ParameterType::kNewGroupRequest)) {
                    new_group_request_id = msg.parameters.Get<std::uint64_t>(messages::ParameterType::kNewGroupRequest);
                }

                // TODO(tievens): add filter type when caching supports it
                SubscribeReceived(conn_ctx.connection_handle,
                                  msg.request_id,
                                  tfn,
                                  {
                                    .priority = priority,
                                    .group_order = group_order,
                                    .delivery_timeout = std::chrono::milliseconds{ delivery_timeout },
                                    .expires = std::chrono::milliseconds{ delivery_timeout },
                                    .filter_type = filter_type,
                                    .forward = forward,
                                    .new_group_request_id = new_group_request_id,
                                    .is_publisher_initiated = false,
                                    .start_location = {},
                                  });

                // Handle new group request after subscribe callback
                if (new_group_request_id.has_value()) {
                    NewGroupRequested({ msg.track_namespace, msg.track_name }, *new_group_request_id);
                }

                return true;
            }
            case messages::ControlMessageType::kSubscribeOk: {
                messages::SubscribeOk msg{};
                msg_bytes >> msg;

                auto sub_it = conn_ctx.request_handlers.find(msg.request_id);

                if (sub_it == conn_ctx.request_handlers.end()) {
                    SPDLOG_LOGGER_WARN(
                      logger_,
                      "Received subscribe ok to unknown subscribe track conn_id: {} request_id: {}, ignored",
                      conn_ctx.connection_handle,
                      msg.request_id);

                    // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race
                    // condition
                    return true;
                }

                if (auto handler = sub_it->second.Get<SubscribeTrackHandler>(); handler) {
                    handler->SetReceivedTrackAlias(msg.track_alias);
                    handler->SetStatus(SubscribeTrackHandler::Status::kOk);
                }

                return true;
            }
            case messages::ControlMessageType::kRequestOk: {
                auto msg = messages::RequestOk{};
                msg_bytes >> msg;

                auto largest_location =
                  msg.parameters.GetOptional<messages::Location>(messages::ParameterType::kLargestObject);

                RequestOkReceived(conn_ctx.connection_handle, msg.request_id, largest_location);

                return true;
            }
            case messages::ControlMessageType::kRequestError: {
                auto msg = messages::RequestError{};
                msg_bytes >> msg;

                RequestResponse response{};
                response.reason_code = RequestResponse::FromErrorCode(msg.error_code);
                response.error_reason = std::string(msg.error_reason.begin(), msg.error_reason.end());

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received track status error request_id: {} error code: {} reason: {}",
                                    msg.request_id,
                                    static_cast<std::uint64_t>(msg.error_code),
                                    response.error_reason.value());

                RequestErrorReceived(conn_ctx.connection_handle, msg.request_id, response);
                return true;
            }
            case messages::ControlMessageType::kTrackStatus: {
                auto msg = messages::TrackStatus{};
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, msg.track_name };
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received track status request_id: {} for full name hash: {}",
                                    msg.request_id,
                                    th.track_fullname_hash);

                TrackStatusReceived(conn_ctx.connection_handle, msg.request_id, tfn);
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

                SubscribeNamespaceReceived(conn_ctx.connection_handle,
                                           data_ctx_id,
                                           msg.track_namespace_prefix,
                                           { .request_id = msg.request_id });
                return true;
            }
            case messages::ControlMessageType::kNamespaceDone: {
                auto msg = messages::NamespaceDone{};
                msg_bytes >> msg;

                UnsubscribeNamespaceReceived(conn_ctx.connection_handle, data_ctx_id, msg.track_namespace_suffix);
                return true;
            }
            case messages::ControlMessageType::kPublishNamespaceDone: {
                messages::PublishNamespaceDone msg;
                msg_bytes >> msg;

                SPDLOG_LOGGER_INFO(logger_, "Received publish namespace done for request_id: {}", msg.request_id);

                auto sub_namespace_conns = PublishNamespaceDoneReceived(conn_ctx.connection_handle, msg.request_id);

                std::lock_guard<std::mutex> _(state_mutex_);
                for (auto conn_id : sub_namespace_conns) {
                    auto conn_it = connections_.find(conn_id);
                    if (conn_it == connections_.end()) {
                        continue;
                    }

                    SendPublishNamespaceDone(conn_it->second, msg.request_id);
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

                auto pub_it = conn_ctx.request_handlers.find(msg.request_id);
                if (pub_it == conn_ctx.request_handlers.end()) {
                    SPDLOG_LOGGER_WARN(logger_,
                                       "Received publish done to unknown subscribe conn_id: {} request_id: {}",
                                       conn_ctx.connection_handle,
                                       msg.request_id);

                    return true;
                }
                auto tfn = pub_it->second.handler->GetFullTrackName();
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_INFO(logger_,
                                   "Received publish done conn_id: {} request_id: {} track namespace hash: {} "
                                   "name hash: {} track alias: {}",
                                   conn_ctx.connection_handle,
                                   msg.request_id,
                                   th.track_namespace_hash,
                                   th.track_name_hash,
                                   th.track_fullname_hash);

                if (auto h = pub_it->second.Get<SubscribeTrackHandler>(); h) {
                    h->SetStatus(SubscribeTrackHandler::Status::kNotSubscribed);
                    PublishDoneReceived(conn_ctx.connection_handle, msg.request_id);
                }

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

                std::string reason(msg.error_reason.begin(), msg.error_reason.end());
                SPDLOG_LOGGER_INFO(logger_,
                                   "Received announce cancel for request_id: {} (error_code={}, reason={})",
                                   msg.request_id,
                                   static_cast<int>(msg.error_code),
                                   reason);

                return true;
            }
            case messages::ControlMessageType::kGoaway: {
                messages::Goaway msg;
                msg_bytes >> msg;

                std::string new_sess_uri(msg.new_session_uri.begin(), msg.new_session_uri.end());
                SPDLOG_LOGGER_INFO(logger_, "Received goaway new session uri: {}", new_sess_uri);
                return true;
            }
            case messages::ControlMessageType::kClientSetup: {
                messages::ClientSetup msg;

                msg_bytes >> msg;

                std::string endpoint_id = "Unknown Endpoint ID";
                for (const auto& param : msg.setup_parameters) {
                    if (param.type == messages::SetupParameterType::kEndpointId) {
                        endpoint_id = std::string(param.value.begin(), param.value.end());
                    }
                }

                ClientSetupReceived(conn_ctx.connection_handle, { endpoint_id });

                SPDLOG_LOGGER_INFO(
                  logger_, "Client setup received conn_id: {} from: {}", conn_ctx.connection_handle, endpoint_id);

                // TODO(tievens): Revisit sending sever setup immediately or wait for something else from server
                SendServerSetup(conn_ctx);
                conn_ctx.setup_complete = true;

                return true;
            }

            case messages::ControlMessageType::kFetchOk: {
                messages::FetchOk msg;
                msg_bytes >> msg;

                auto fetch_it = conn_ctx.request_handlers.find(msg.request_id);
                if (fetch_it == conn_ctx.request_handlers.end()) {
                    SPDLOG_LOGGER_WARN(logger_,
                                       "Received fetch ok for unknown fetch track conn_id: {} request_id: {}, ignored",
                                       conn_ctx.connection_handle,
                                       msg.request_id);
                    return true;
                }

                if (auto h = fetch_it->second.Get<FetchTrackHandler>(); h) {
                    h->SetLatestLocation(msg.end_location);
                    h->SetStatus(FetchTrackHandler::Status::kOk);
                }

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

                        // Unwrap with the end location wire format.
                        messages::FetchEndLocation end_location;
                        end_location.group = msg.group_0->standalone.end.group;
                        if (msg.group_0->standalone.end.object == 0) {
                            end_location.object = std::nullopt;
                        } else {
                            end_location.object = msg.group_0->standalone.end.object - 1;
                        }

                        auto priority = msg.parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority);
                        auto group_order =
                          msg.parameters.Get<messages::GroupOrder>(messages::ParameterType::kGroupOrder);

                        messages::StandaloneFetchAttributes attrs = {
                            .priority = priority,
                            .group_order = group_order,
                            .start_location = msg.group_0->standalone.start,
                            .end_location = end_location,
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
                            SendRequestError(conn_ctx,
                                             msg.request_id,
                                             messages::ErrorCode::kDoesNotExist,
                                             0ms,
                                             "Corresponding subscribe does not exist");
                            return true;
                        }

                        FullTrackName tfn = subscribe_state->second.track_full_name;
                        auto priority = msg.parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority);
                        auto group_order =
                          msg.parameters.Get<messages::GroupOrder>(messages::ParameterType::kGroupOrder);

                        messages::JoiningFetchAttributes attrs = {
                            .priority = priority,
                            .group_order = group_order,
                            .joining_request_id = msg.group_1->joining.request_id,
                            .relative = relative_joining,
                            .joining_start = msg.group_1->joining.joining_start,
                        };

                        JoiningFetchReceived(conn_ctx.connection_handle, msg.request_id, tfn, attrs);
                        return true;
                    }
                    default: {
                        SendRequestError(
                          conn_ctx, msg.request_id, messages::ErrorCode::kNotSupported, 0ms, "Unknown fetch type");
                        return true;
                    }
                }

                return true;
            }
            case messages::ControlMessageType::kFetchCancel: {
                messages::FetchCancel msg;
                msg_bytes >> msg;

                if (conn_ctx.recv_req_id.find(msg.request_id) == conn_ctx.recv_req_id.end()) {
                    SPDLOG_LOGGER_WARN(logger_, "Received Fetch Cancel for unknown subscribe ID: {}", msg.request_id);
                }

                const auto fetch_it = conn_ctx.request_handlers.find(msg.request_id);
                if (fetch_it == conn_ctx.request_handlers.end()) {
                    FetchCancelReceived(conn_ctx.connection_handle, msg.request_id);
                    return true;
                }

                if (auto h = fetch_it->second.Get<FetchTrackHandler>(); h) {
                    h->SetStatus(FetchTrackHandler::Status::kCancelled);
                }

                FetchCancelReceived(conn_ctx.connection_handle, msg.request_id);

                conn_ctx.request_handlers.erase(fetch_it);
                conn_ctx.recv_req_id.erase(msg.request_id);

                return true;
            }
            case messages::ControlMessageType::kPublish: {
                auto msg = messages::Publish{};
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, msg.track_name };
                auto th = TrackHash(tfn);

                conn_ctx.recv_req_id[msg.request_id] = { .track_full_name = tfn, .track_hash = th };

                bool dynamic_groups = false; // TODO: Figure this out
                auto delivery_timeout = msg.parameters.Get<std::uint64_t>(messages::ParameterType::kDeliveryTimeout);
                auto expires = msg.parameters.Get<std::uint64_t>(messages::ParameterType::kExpires);
                auto group_order = msg.parameters.Get<messages::GroupOrder>(messages::ParameterType::kGroupOrder);
                auto forward = msg.parameters.Get<bool>(messages::ParameterType::kForward);

                messages::PublishAttributes attributes;
                attributes.track_full_name = tfn;
                attributes.track_alias = msg.track_alias;
                attributes.priority = 0;
                attributes.group_order = group_order;
                attributes.delivery_timeout = std::chrono::milliseconds(delivery_timeout);
                attributes.expires = std::chrono::milliseconds(expires);
                attributes.filter_type = messages::FilterType::kLargestObject;
                attributes.forward = forward;
                attributes.new_group_request_id = 1;
                attributes.is_publisher_initiated = true;
                attributes.dynamic_groups = dynamic_groups;

                PublishReceived(conn_ctx.connection_handle, msg.request_id, attributes);

                return true;
            }

            case messages::ControlMessageType::kPublishOk: {
                messages::PublishOk msg{};
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

                auto priority = msg.parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority);
                auto delivery_timeout = msg.parameters.Get<std::uint64_t>(messages::ParameterType::kDeliveryTimeout);
                auto expires = msg.parameters.Get<std::uint64_t>(messages::ParameterType::kExpires);
                auto group_order = msg.parameters.Get<messages::GroupOrder>(messages::ParameterType::kGroupOrder);
                auto forward = msg.parameters.Get<bool>(messages::ParameterType::kForward);
                auto filter_type =
                  msg.parameters.Get<messages::FilterType>(messages::ParameterType::kSubscriptionFilter);
                auto new_group_request_id =
                  msg.parameters.GetOptional<std::uint64_t>(messages::ParameterType::kNewGroupRequest);

                messages::SubscribeAttributes attributes = {
                    .priority = priority,
                    .group_order = group_order,
                    .delivery_timeout = std::chrono::milliseconds(delivery_timeout),
                    .expires = std::chrono::milliseconds(expires),
                    .filter_type = filter_type,
                    .forward = forward,
                    .new_group_request_id = new_group_request_id,
                    .is_publisher_initiated = true,
                    .start_location = { 0, 0 },
                };

                SubscribeReceived(conn_ctx.connection_handle, msg.request_id, tfn, attributes);
                return true;
            }

            case messages::ControlMessageType::kRequestUpdate: {
                messages::RequestUpdate msg;
                msg_bytes >> msg;

                auto sub_ctx_it = conn_ctx.recv_req_id.find(msg.request_id);
                if (sub_ctx_it == conn_ctx.recv_req_id.end()) {
                    // update for invalid subscription
                    SPDLOG_LOGGER_WARN(logger_,
                                       "Received subscribe_update for unknown subscription conn_id: {} request_id: {}",
                                       conn_ctx.connection_handle,
                                       msg.request_id);

                    SendRequestError(
                      conn_ctx, msg.request_id, messages::ErrorCode::kDoesNotExist, 0ms, "Subscription not found");
                    return true;
                }

                auto delivery_timeout = msg.parameters.Get<std::uint64_t>(messages::ParameterType::kDeliveryTimeout);
                auto priority = msg.parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority);
                auto filter_type =
                  msg.parameters.Get<messages::FilterType>(messages::ParameterType::kSubscriptionFilter);
                auto forward = msg.parameters.Get<bool>(messages::ParameterType::kForward);
                auto new_group_request_id =
                  msg.parameters.GetOptional<std::uint64_t>(messages::ParameterType::kNewGroupRequest);

                if (new_group_request_id.has_value()) {
                    NewGroupRequested(sub_ctx_it->second.track_full_name, new_group_request_id.value());
                }

                SPDLOG_LOGGER_DEBUG(
                  logger_, "Received subscribe_update to recv request_id: {} forward: {}", msg.request_id, forward);

                /*
                 * Unlike client, server supports multi-publisher to the client.
                 *   There is a publish handler per publisher connection
                 */
                for (const auto& pub :
                     conn_ctx.pub_tracks_by_track_alias[sub_ctx_it->second.track_hash.track_fullname_hash]) {

                    // TODO: Follow up on https://github.com/moq-wg/moq-transport/issues/1304
                    if (not forward) {
                        pub.second->SetStatus(PublishTrackHandler::Status::kPaused);

                    } else {
                        pub.second->SetStatus(PublishTrackHandler::Status::kNewGroupRequested);
                    }
                }
                return true;
            }
            default: {
                SPDLOG_LOGGER_ERROR(
                  logger_, "Unsupported MOQT message type: {}, bad stream", static_cast<uint64_t>(msg_type));
                return false;
            }

        } // End of switch(msg type)

    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(
          logger_, "Unable to parse {} control message: {}", static_cast<uint64_t>(msg_type), e.what());
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
