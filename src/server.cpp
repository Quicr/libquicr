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

    void Server::AnnounceReceived(ConnectionHandle, const TrackNamespace&, const PublishAnnounceAttributes&) {}

    std::pair<std::optional<messages::SubscribeAnnouncesErrorCode>, std::vector<TrackNamespace>>
    Server::SubscribeAnnouncesReceived(ConnectionHandle, const TrackNamespace&, const PublishAnnounceAttributes&)
    {
        return { std::nullopt, {} };
    }

    void Server::UnsubscribeAnnouncesReceived(ConnectionHandle, const TrackNamespace&) {}

    void Server::ResolveAnnounce(ConnectionHandle connection_handle,
                                 uint64_t request_id,
                                 const TrackNamespace& track_namespace,
                                 const std::vector<ConnectionHandle>& subscribers,
                                 const AnnounceResponse& response)
    {
        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        switch (response.reason_code) {
            case AnnounceResponse::ReasonCode::kOk: {
                SendAnnounceOk(conn_it->second, request_id);

                for (const auto& sub_conn_handle : subscribers) {
                    auto it = connections_.find(sub_conn_handle);
                    if (it == connections_.end()) {
                        continue;
                    }

                    // TODO: what request Id do we send for subscribe announces???
                    SendAnnounce(it->second, request_id, track_namespace);
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
                                   messages::FilterType,
                                   const FullTrackName&,
                                   const messages::SubscribeAttributes&)
    {
    }

    std::optional<messages::Location> Server::GetLargestAvailable([[maybe_unused]] const FullTrackName& track_name)
    {
        return std::nullopt;
    }

    bool Server::OnFetchOk(ConnectionHandle, uint64_t, const FullTrackName&, const messages::FetchAttributes&)
    {
        return false;
    }

    void Server::NewGroupRequested(ConnectionHandle, uint64_t, uint64_t) {}

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
                assert(conn_it->second.recv_sub_id.find(request_id) != conn_it->second.recv_sub_id.end());
                conn_it->second.recv_sub_id[request_id].largest_location = subscribe_response.largest_location;

                // Send the ok.
                SendSubscribeOk(conn_it->second,
                                request_id,
                                track_alias,
                                kSubscribeExpires,
                                subscribe_response.largest_location.has_value(),
                                subscribe_response.largest_location.has_value()
                                  ? subscribe_response.largest_location.value()
                                  : messages::Location());
                break;
            }
            default:
                SendSubscribeError(
                  conn_it->second, request_id, messages::SubscribeErrorCode::kInternalError, "Internal error");
                break;
        }
    }

    void Server::UnbindPublisherTrack(ConnectionHandle connection_handle,
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
        conn_it->second.pub_tracks_by_track_alias.erase(th.track_fullname_hash);

        if (conn_it->second.pub_tracks_by_name.count(th.track_namespace_hash) == 0) {
            SPDLOG_LOGGER_DEBUG(logger_,
                                "Server publish track conn_id: {} full_name_hash: {} namespace_hash: {} unbind",
                                connection_handle,
                                th.track_fullname_hash,
                                th.track_namespace_hash);

            conn_it->second.pub_tracks_by_name.erase(th.track_namespace_hash);
        }

        conn_it->second.pub_tracks_by_data_ctx_id.erase(track_handler->publish_data_ctx_id_);
    }

    void Server::BindPublisherTrack(TransportConnId conn_id,
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

        track_handler->SetRequestId(request_id);
        conn_it->second.pub_tracks_ns_by_request_id[request_id] = th.track_namespace_hash;

        track_handler->connection_handle_ = conn_id;

        track_handler->publish_data_ctx_id_ =
          quic_transport_->CreateDataContext(conn_id,
                                             track_handler->default_track_mode_ == TrackMode::kDatagram ? false : true,
                                             track_handler->default_priority_,
                                             false);

        // Setup the function for the track handler to use to send objects with thread safety
        std::weak_ptr weak_track_handler(track_handler);
        track_handler->publish_object_func_ =
          [&, weak_track_handler](uint8_t priority,
                                  uint32_t ttl,
                                  bool stream_header_needed,
                                  uint64_t group_id,
                                  uint64_t subgroup_id,
                                  uint64_t object_id,
                                  std::optional<Extensions> extensions,
                                  std::span<uint8_t const> data) -> PublishTrackHandler::PublishObjectStatus {
            auto th = weak_track_handler.lock();
            if (!th) {
                return PublishTrackHandler::PublishObjectStatus::kInternalError;
            }

            return SendObject(
              *th, priority, ttl, stream_header_needed, group_id, subgroup_id, object_id, extensions, data);
        };

        track_handler->forward_publish_data_func_ =
          [&, weak_track_handler](
            uint8_t priority,
            uint32_t ttl,
            bool stream_header_needed,
            std::shared_ptr<const std::vector<uint8_t>> data) -> PublishTrackHandler::PublishObjectStatus {
            if (auto handler = weak_track_handler.lock()) {
                return SendData(*handler, priority, ttl, stream_header_needed, data);
            }
            return PublishTrackHandler::PublishObjectStatus::kInternalError;
        };

        if (!ephemeral) {
            // Hold onto track handler
            conn_it->second.pub_tracks_by_name[th.track_namespace_hash][th.track_name_hash] = track_handler;
            conn_it->second.pub_tracks_by_track_alias[th.track_fullname_hash] = track_handler;
            conn_it->second.pub_tracks_by_data_ctx_id[track_handler->publish_data_ctx_id_] = std::move(track_handler);
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

        conn_it->second.pub_fetch_tracks_by_sub_id.erase(request_id);
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

        track_handler->connection_handle_ = conn_id;
        track_handler->publish_data_ctx_id_ =
          quic_transport_->CreateDataContext(conn_id, true, track_handler->GetDefaultPriority(), false);

        // Setup the function for the track handler to use to send objects with thread safety
        std::weak_ptr weak_handler(track_handler);
        track_handler->publish_object_func_ =
          [&, weak_handler](uint8_t priority,
                            uint32_t ttl,
                            bool stream_header_needed,
                            uint64_t group_id,
                            uint64_t subgroup_id,
                            uint64_t object_id,
                            std::optional<Extensions> extensions,
                            std::span<const uint8_t> data) -> PublishTrackHandler::PublishObjectStatus {
            const auto handler = weak_handler.lock();
            if (!handler) {
                return PublishTrackHandler::PublishObjectStatus::kInternalError;
            }
            return SendFetchObject(
              *handler, priority, ttl, stream_header_needed, group_id, subgroup_id, object_id, extensions, data);
        };

        // Hold ref to track handler
        conn_it->second.pub_fetch_tracks_by_sub_id[request_id] = std::move(track_handler);
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
            fetch_header.subscribe_id = request_id;
            track_handler.object_msg_buffer_ << fetch_header;

            quic_transport_->Enqueue(track_handler.connection_handle_,
                                     track_handler.publish_data_ctx_id_,
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

                conn_ctx.recv_sub_id[msg.request_id] = { tfn };

                if (msg.request_id > conn_ctx.next_request_id) {
                    conn_ctx.next_request_id = msg.request_id + 1;
                }

                const auto dt_param =
                  std::find_if(msg.subscribe_parameters.begin(), msg.subscribe_parameters.end(), [](const auto& p) {
                      return static_cast<messages::VersionSpecificParameterType>(p.type) ==
                             messages::VersionSpecificParameterType::kDeliveryTimeout;
                  });

                std::uint64_t delivery_timeout = 0;

                if (dt_param != msg.subscribe_parameters.end()) {
                    std::memcpy(&delivery_timeout, dt_param->value.data(), dt_param->value.size());
                }

                // TODO(tievens): add filter type when caching supports it
                SubscribeReceived(conn_ctx.connection_handle,
                                  msg.request_id,
                                  msg.filter_type,
                                  tfn,
                                  {
                                    msg.subscriber_priority,
                                    static_cast<messages::GroupOrder>(msg.group_order),
                                    delivery_timeout,
                                  });

                return true;
            }
            case messages::ControlMessageType::kSubscribeOk: {
                auto msg = messages::SubscribeOk([](messages::SubscribeOk& msg) {
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

                sub_it->second.get()->SetStatus(SubscribeTrackHandler::Status::kOk);

                return true;
            }
            case messages::ControlMessageType::kSubscribeError: {
                auto msg = messages::SubscribeError{};
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

                sub_it->second->SetStatus(SubscribeTrackHandler::Status::kError);
                RemoveSubscribeTrack(conn_ctx, *sub_it->second);

                return true;
            }
            case messages::ControlMessageType::kAnnounce: {
                auto msg = messages::Announce{};
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, {} };

                AnnounceReceived(conn_ctx.connection_handle, tfn.name_space, { msg.request_id });
                return true;
            }

            case messages::ControlMessageType::kSubscribeAnnounces: {
                auto msg = messages::SubscribeAnnounces{};
                msg_bytes >> msg;

                const auto& [err, matched_ns] =
                  SubscribeAnnouncesReceived(conn_ctx.connection_handle, msg.track_namespace_prefix, {});
                if (err.has_value()) {
                    SendSubscribeAnnouncesError(conn_ctx, msg.request_id, *err, {});
                } else {
                    for (const auto& ns : matched_ns) {
                        SendAnnounce(conn_ctx, msg.request_id, ns);
                    }
                }

                return true;
            }

            case messages::ControlMessageType::kUnsubscribeAnnounces: {
                auto msg = messages::UnsubscribeAnnounces{};
                msg_bytes >> msg;

                UnsubscribeAnnouncesReceived(conn_ctx.connection_handle, msg.track_namespace_prefix);
                return true;
            }

            case messages::ControlMessageType::kAnnounceError: {
                auto msg = messages::AnnounceError{};
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

            case messages::ControlMessageType::kUnannounce: {
                messages::Unannounce msg;
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, {} };
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_INFO(logger_, "Received unannounce for namespace_hash: {0}", th.track_namespace_hash);

                auto sub_anno_conns = UnannounceReceived(conn_ctx.connection_handle, tfn.name_space);

                std::lock_guard<std::mutex> _(state_mutex_);
                for (auto conn_id : sub_anno_conns) {
                    auto conn_it = connections_.find(conn_id);
                    if (conn_it == connections_.end()) {
                        continue;
                    }

                    SendUnannounce(conn_it->second, msg.track_namespace);
                }

                return true;
            }

            case messages::ControlMessageType::kUnsubscribe: {
                messages::Unsubscribe msg;
                msg_bytes >> msg;

                const auto& tfn = conn_ctx.recv_sub_id[msg.request_id].track_full_name;
                TrackHash th(tfn);
                if (auto pdt = GetPubTrackHandler(conn_ctx, th)) {
                    pdt->SetStatus(PublishTrackHandler::Status::kNoSubscribers);
                }

                UnsubscribeReceived(conn_ctx.connection_handle, msg.request_id);
                conn_ctx.recv_sub_id.erase(msg.request_id);

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
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_INFO(logger_,
                                   "Received subscribe done conn_id: {0} request_id: {1} track namespace hash: {2} "
                                   "name hash: {3} track alias: {4}",
                                   conn_ctx.connection_handle,
                                   msg.request_id,
                                   th.track_namespace_hash,
                                   th.track_name_hash,
                                   th.track_fullname_hash);

                sub_it->second.get()->SetStatus(SubscribeTrackHandler::Status::kNotSubscribed);

                UnsubscribeReceived(conn_ctx.connection_handle, msg.request_id);
                conn_ctx.recv_sub_id.erase(msg.request_id);

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

                auto tfn = FullTrackName{ msg.track_namespace, {} };
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_INFO(
                  logger_, "Received announce cancel for namespace_hash: {0}", th.track_namespace_hash);
                return true;
            }
            case messages::ControlMessageType::kTrackStatusRequest: {
                messages::TrackStatusRequest msg;
                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, msg.track_name };
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
                    if (param.type == messages::ParameterType::kEndpointId) {
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
            case messages::ControlMessageType::kFetch: {
                auto msg = messages::Fetch(
                  [](messages::Fetch& msg) {
                      if (msg.fetch_type == messages::FetchType::kStandalone) {
                          msg.group_0 = std::make_optional<messages::Fetch::Group_0>();
                      }
                  },
                  [](messages::Fetch& msg) {
                      if (msg.fetch_type == messages::FetchType::kJoiningFetch) {
                          msg.group_1 = std::make_optional<messages::Fetch::Group_1>();
                      }
                  });
                msg_bytes >> msg;

                // Prepare for fetch lookups, which differ by type.
                FullTrackName tfn;
                messages::FetchAttributes attrs = {
                    msg.subscriber_priority, msg.group_order, { 0, 0 }, 0, std::nullopt
                };
                bool end_of_track = false; // TODO: Need to query this as part of the GetLargestAvailable call.
                messages::Location largest_location;

                switch (msg.fetch_type) {
                    case messages::FetchType::kStandalone: {
                        // Standalone fetch is self-containing.
                        tfn = FullTrackName{ msg.group_0->track_namespace, msg.group_0->track_name };
                        const auto largest_available = GetLargestAvailable(tfn);
                        if (!largest_available.has_value()) {
                            SendFetchError(conn_ctx,
                                           msg.request_id,
                                           messages::FetchErrorCode::kTrackDoesNotExist,
                                           "Track does not exist");
                            return true;
                        }

                        largest_location = largest_available.value();

                        attrs.start_location = msg.group_0->start_location;
                        attrs.end_group = msg.group_0->end_location.group;
                        attrs.end_object = msg.group_0->end_location.object > 0
                                             ? std::optional(msg.group_0->end_location.object - 1)
                                             : std::nullopt;
                        break;
                    }
                    case messages::FetchType::kJoiningFetch: {
                        // Joining fetch needs to look up its joining subscribe.
                        // TODO: Need a new error code for subscribe doesn't exist.
                        const auto subscribe_state = conn_ctx.recv_sub_id.find(msg.group_1->joining_request_id);
                        if (subscribe_state == conn_ctx.recv_sub_id.end()) {
                            SendFetchError(conn_ctx,
                                           msg.request_id,
                                           messages::FetchErrorCode::kTrackDoesNotExist,
                                           "Corresponding subscribe does not exist");
                            return true;
                        }

                        tfn = subscribe_state->second.track_full_name;
                        const auto opt_largest_location = subscribe_state->second.largest_location;
                        if (!opt_largest_location.has_value()) {
                            // We have no data to complete the fetch with.
                            // TODO: Possibly missing "No Objects" code per the draft.
                            SendFetchError(
                              conn_ctx, msg.request_id, messages::FetchErrorCode::kInvalidRange, "Nothing to give");

                            return true;
                        }
                        largest_location = *opt_largest_location;

                        // TODO(RichLogan): Check this when FETCH v11 checked.
                        const auto start_group = msg.group_1->joining_start <= largest_location.group
                                                   ? largest_location.group - msg.group_1->joining_start
                                                   : largest_location.group;
                        attrs.start_location = messages::Location{ start_group, 0 };
                        attrs.end_group = largest_location.group;
                        attrs.end_object = largest_location.object;
                        break;
                    }
                    default: {
                        SendFetchError(
                          conn_ctx, msg.request_id, messages::FetchErrorCode::kNotSupported, "Unknown fetch type");
                        return true;
                    }
                }

                // TODO: This only covers it being below largest, not what's in cache.
                // Availability check.
                bool valid_range = true;
                valid_range &= attrs.start_location.group <= largest_location.group;
                if (largest_location.group == attrs.start_location.group) {
                    valid_range &= attrs.start_location.object <= largest_location.object;
                }
                valid_range &= attrs.end_group <= largest_location.group;
                if (largest_location.group == attrs.end_group && attrs.end_object.has_value()) {
                    valid_range &= attrs.end_object <= largest_location.object;
                }
                if (!valid_range) {
                    SendFetchError(
                      conn_ctx, msg.request_id, messages::FetchErrorCode::kInvalidRange, "Cannot serve this range");
                    return true;
                }

                if (msg.request_id > conn_ctx.next_request_id) {
                    conn_ctx.next_request_id = msg.request_id + 1;
                }

                SendFetchOk(conn_ctx, msg.request_id, msg.group_order, end_of_track, largest_location);

                if (!OnFetchOk(conn_ctx.connection_handle, msg.request_id, tfn, attrs)) {
                    SendFetchError(conn_ctx,
                                   msg.request_id,
                                   messages::FetchErrorCode::kInvalidRange,
                                   "Cache does not have any data for given range");
                }

                return true;
            }
            case messages::ControlMessageType::kFetchCancel: {
                messages::FetchCancel msg;
                msg_bytes >> msg;

                if (conn_ctx.recv_sub_id.find(msg.request_id) == conn_ctx.recv_sub_id.end()) {
                    SPDLOG_LOGGER_WARN(logger_, "Received Fetch Cancel for unknown subscribe ID: {0}", msg.request_id);
                }

                FetchCancelReceived(conn_ctx.connection_handle, msg.request_id);
                conn_ctx.recv_sub_id.erase(msg.request_id);

                return true;
            }
            case messages::ControlMessageType::kNewGroupRequest: {
                messages::NewGroupRequest msg;
                msg_bytes >> msg;

                NewGroupRequested(conn_ctx.connection_handle, msg.request_id, msg.track_alias);

                return true;
            }
            default:
                SPDLOG_LOGGER_ERROR(logger_,
                                    "Unsupported MOQT message type: {0}, bad stream",
                                    static_cast<uint64_t>(*conn_ctx.ctrl_msg_type_received));
                return false;

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
