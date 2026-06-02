#include "quicr/session.h"

#include <chrono>

using namespace std::chrono_literals;

namespace quicr {
    Session::Status Session::Connect()
    {
        return Transport::Start();
    }

    Session::Status Session::Disconnect()
    {
        Transport::Stop();
        return Status::kDisconnecting;
    }

    Session::Status Session::Start()
    {
        return Transport::Start();
    }

    void Session::Stop()
    {
        stop_ = true;
        Transport::Stop();
    }

    void Session::NewGroupRequested(const FullTrackName&, messages::GroupId) {}

    void Session::PublishNamespaceReceived(ConnectionHandle connection_handle,
                                           const TrackNamespace& track_namespace,
                                           const PublishNamespaceAttributes& publish_announce_attributes)
    {
    }

    void Session::PublishNamespaceStatusChanged(messages::RequestID request_id, const PublishNamespaceStatus status) {}

    void Session::ResolveFetch(ConnectionHandle connection_handle,
                               uint64_t request_id,
                               std::uint8_t priority,
                               std::optional<messages::GroupOrder> group_order,
                               const FetchResponse& response)
    {
        auto error_code = messages::ErrorCode::kInternalError;

        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        switch (response.reason_code) {
            case FetchResponse::ReasonCode::kOk:
                SendFetchOk(conn_it->second,
                            request_id,
                            response.publisher_default_group_order,
                            priority,
                            response.largest_location.value());
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

    void Session::ResolveSubscribe(ConnectionHandle connection_handle,
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
                    SendSubscribeOk(conn_it->second,
                                    request_id,
                                    track_alias,
                                    kSubscribeExpires,
                                    subscribe_response.largest_location,
                                    subscribe_response.publisher_default_group_order);
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

    void Session::ResolveSubscribeNamespace(ConnectionHandle connection_handle,
                                            DataContextId data_ctx_id,
                                            uint64_t request_id,
                                            const TrackNamespace& prefix,
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
            const auto match = prefix.IsPrefixOf(name_space);
            if (match == std::partial_ordering::unordered || match == std::partial_ordering::less) {
                SPDLOG_LOGGER_WARN(logger_, "Dropping non prefix match");
                continue;
            }

            auto request_id = conn_it->second.GetNextRequestId();
            SendPublishNamespace(conn_it->second, request_id, name_space);
        }
    }

    void Session::ServerSetupReceived(const ServerSetupAttributes& server_setup_attributes) {}

    void Session::SubscribeReceived(ConnectionHandle connection_handle,
                                    uint64_t request_id,
                                    const FullTrackName& track_full_name,
                                    const messages::SubscribeAttributes& subscribe_attributes)
    {
    }

    void Session::ClientSetupReceived(ConnectionHandle, const ClientSetupAttributes&) {}

    void Session::PublishDoneReceived(ConnectionHandle, uint64_t) {}

    std::vector<ConnectionHandle> Session::PublishNamespaceDoneReceived(ConnectionHandle connection_handle,
                                                                        messages::RequestID request_id)
    {
        return std::vector<ConnectionHandle>();
    }
    void Session::SubscribeNamespaceReceived(ConnectionHandle,
                                             DataContextId,
                                             const TrackNamespace&,
                                             const messages::SubscribeNamespaceAttributes&)
    {
    }

    void Session::UnpublishedSubscribeReceived(const FullTrackName&, const messages::SubscribeAttributes&) {}

    void Session::UnsubscribeNamespaceReceived(ConnectionHandle, const TrackNamespace&) {}

    void Session::UnsubscribeReceived(ConnectionHandle, uint64_t) {}

    void Session::ConnectionStatusChanged(ConnectionHandle connection_handle, ConnectionStatus status) {}

    void Session::FetchCancelReceived(ConnectionHandle connection_handle, uint64_t request_id) {}

    void Session::JoiningFetchReceived(ConnectionHandle connection_handle,
                                       uint64_t request_id,
                                       const FullTrackName& track_full_name,
                                       const quicr::messages::JoiningFetchAttributes& attributes)
    {
    }

    void Session::MetricsSampled(ConnectionHandle, const ConnectionMetrics&) {}

    void Session::MetricsSampled(const ConnectionMetrics&) {}

    void Session::NewConnectionAccepted(ConnectionHandle connection_handle, const ConnectionRemoteInfo& remote)
    {
        SPDLOG_LOGGER_DEBUG(
          logger_, "New connection conn_id: {} remote ip: {} port: {}", connection_handle, remote.ip, remote.port);
    }

    void Session::PublishReceived(ConnectionHandle connection_handle,
                                  uint64_t request_id,
                                  const messages::PublishAttributes& publish_attributes,
                                  std::weak_ptr<SubscribeNamespaceHandler> sub_ns_handler)
    {
    }

    void Session::RequestUpdateReceived(ConnectionHandle connection_handle,
                                        uint64_t request_id,
                                        uint64_t existing_request_id,
                                        const messages::Parameters& params)
    {
    }

    void Session::ResolveRequestUpdate(ConnectionHandle connection_handle,
                                       uint64_t request_id,
                                       uint64_t existing_request_id,
                                       const messages::Parameters& params)
    {
    }

    void Session::StandaloneFetchReceived(ConnectionHandle connection_handle,
                                          uint64_t request_id,
                                          const FullTrackName& track_full_name,
                                          const quicr::messages::StandaloneFetchAttributes& attributes)
    {
    }

    PublishNamespaceStatus Session::GetPublishNamespaceStatus(const TrackNamespace& track_namespace)
    {
        return PublishNamespaceStatus();
    }

    void Session::BindFetchTrack(TransportConnId conn_id, std::shared_ptr<PublishFetchHandler> track_handler)
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

    void Session::BindPublisherTrack(ConnectionHandle connection_handle,
                                     ConnectionHandle src_id,
                                     uint64_t request_id,
                                     const std::shared_ptr<PublishTrackHandler>& track_handler,
                                     bool ephemeral)
    {
        // Generate track alias
        const auto& tfn = track_handler->GetFullTrackName();
        auto th = TrackHash(tfn);

        std::unique_lock<std::mutex> lock(state_mutex_);
        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Subscribe track conn_id: {} does not exist.", connection_handle);
            return;
        }

        if (!track_handler->GetTrackAlias().has_value()) {
            track_handler->SetTrackAlias(th.track_fullname_hash);
        }

        track_handler->SetRequestId(request_id);
        conn_it->second.request_handlers[request_id] = track_handler;

        track_handler->connection_handle_ = connection_handle;

        track_handler->publish_data_ctx_id_ =
          quic_transport_->CreateDataContext(connection_handle,
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

    void Session::PublishNamespaceDone(const std::shared_ptr<PublishNamespaceHandler>& handler) {}

    void Session::ResolvePublishNamespace(ConnectionHandle connection_handle,
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

    void Session::ResolvePublishNamespaceDone(ConnectionHandle connection_handle,
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

    void Session::SubscribeNamespace(std::shared_ptr<SubscribeNamespaceHandler> handler) {}

    void Session::UnbindFetchTrack(ConnectionHandle connection_handle,
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

    void Session::UnbindPublisherTrack(ConnectionHandle connection_handle,
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

    void Session::UnsubscribeNamespace(const std::shared_ptr<SubscribeNamespaceHandler>& handler) {}

    bool Session::ProcessCtrlMessage(ConnectionContext& conn_ctx,
                                     uint64_t data_ctx_id,
                                     messages::ControlMessageType msg_type,
                                     BytesSpan msg_bytes)
    try {
        switch (msg_type) {
            case messages::ControlMessageType::kSubscribe: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto track_namespace = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                const auto track_name = messages::Message::ParseField<Bytes>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                auto tfn = FullTrackName{ track_namespace, track_name };
                auto th = TrackHash(tfn);

                conn_ctx.recv_req_id[request_id] = { .track_full_name = tfn, .track_hash = th };

                auto delivery_timeout = parameters.Get<std::uint64_t>(messages::ParameterType::kDeliveryTimeout);
                auto priority = parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority);
                auto group_order = parameters.GetOptional<messages::GroupOrder>(messages::ParameterType::kGroupOrder);
                const auto publisher_default_group_order = messages::GroupOrder::kAscending;
                auto forward = parameters.Get<bool>(messages::ParameterType::kForward);
                auto new_group_request_id =
                  parameters.GetOptional<std::uint64_t>(messages::ParameterType::kNewGroupRequest);

                messages::Filter filter; // TODO: Support more filters.
                if (parameters.Contains(messages::ParameterType::kLocationFilter)) {
                    filter = parameters.GetFilter(messages::FilterType::kLocationFilter);
                } else if (parameters.Contains(messages::ParameterType::kTrackFilter)) {
                    filter = parameters.GetFilter(messages::FilterType::kTrackFilter);
                }

                // TODO(tievens): add filter type when caching supports it
                SubscribeReceived(conn_ctx.connection_handle,
                                  request_id,
                                  tfn,
                                  {
                                    .priority = priority,
                                    .group_order = group_order,
                                    .publisher_default_group_order = publisher_default_group_order,
                                    .delivery_timeout = std::chrono::milliseconds{ delivery_timeout },
                                    .expires = std::chrono::milliseconds{ delivery_timeout },
                                    .filter = filter,
                                    .forward = forward,
                                    .new_group_request_id = new_group_request_id,
                                    .is_publisher_initiated = false,
                                    .start_location = {},
                                  });

                // Handle new group request after subscribe callback
                if (new_group_request_id.has_value()) {
                    NewGroupRequested({ track_namespace, track_name }, *new_group_request_id);
                }

                return true;
            }
            case messages::ControlMessageType::kSubscribeOk: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto track_alias = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);
                const auto track_extensions = messages::Message::ParseField<messages::TrackExtensions>(msg_bytes);

                auto sub_it = conn_ctx.request_handlers.find(request_id);

                if (sub_it == conn_ctx.request_handlers.end()) {
                    SPDLOG_LOGGER_WARN(
                      logger_,
                      "Received subscribe ok to unknown subscribe track conn_id: {} request_id: {}, ignored",
                      conn_ctx.connection_handle,
                      request_id);

                    // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race
                    // condition
                    return true;
                }

                if (auto handler = sub_it->second.Get<SubscribeTrackHandler>()) {
                    const auto publisher_default_group_order =
                      track_extensions
                        .GetOptional<messages::GroupOrder>(messages::ExtensionType::kDefaultPublisherGroupOrder)
                        .value_or(messages::GroupOrder::kAscending);
                    handler->SetReceivedTrackAlias(track_alias);
                    handler->SetPublisherDefaultGroupOrder(publisher_default_group_order);
                    handler->SetStatus(SubscribeTrackHandler::Status::kOk);
                    conn_ctx.sub_by_recv_track_alias[track_alias] = handler;
                }

                return true;
            }
            case messages::ControlMessageType::kRequestOk: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                auto largest_location =
                  parameters.GetOptional<messages::Location>(messages::ParameterType::kLargestObject);

                RequestOkReceived(conn_ctx.connection_handle, request_id, largest_location);

                return true;
            }
            case messages::ControlMessageType::kRequestError: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto error_code = messages::Message::ParseField<messages::ErrorCode>(msg_bytes);
                [[maybe_unused]] const auto retry_interval = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto error_reason = messages::Message::ParseField<Bytes>(msg_bytes);

                RequestResponse response{};
                response.reason_code = RequestResponse::FromErrorCode(error_code);
                response.error_reason = std::string(error_reason.begin(), error_reason.end());

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received track status error request_id: {} error code: {} reason: {}",
                                    request_id,
                                    static_cast<std::uint64_t>(error_code),
                                    response.error_reason.value());

                RequestErrorReceived(conn_ctx.connection_handle, request_id, response);
                return true;
            }
            case messages::ControlMessageType::kTrackStatus: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto track_namespace = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                const auto track_name = messages::Message::ParseField<Bytes>(msg_bytes);

                auto tfn = FullTrackName{ track_namespace, track_name };
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received track status request_id: {} for full name hash: {}",
                                    request_id,
                                    th.track_fullname_hash);

                TrackStatusReceived(conn_ctx.connection_handle, request_id, tfn);
                return true;
            }
            case messages::ControlMessageType::kPublishNamespace: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto track_namespace = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                [[maybe_unused]] const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                auto tfn = FullTrackName{ track_namespace, {} };

                PublishNamespaceReceived(conn_ctx.connection_handle, tfn.name_space, { request_id });
                return true;
            }
            case messages::ControlMessageType::kSubscribeNamespace: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto track_namespace_prefix = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                const auto subscribe_options = messages::Message::ParseField<messages::SubscribeOptions>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                messages::Filter filter; // TODO: Support more filters.
                if (parameters.Contains(messages::ParameterType::kLocationFilter)) {
                    filter = parameters.GetFilter(messages::FilterType::kLocationFilter);
                } else if (parameters.Contains(messages::ParameterType::kTrackFilter)) {
                    filter = parameters.GetFilter(messages::FilterType::kTrackFilter);
                }

                SubscribeNamespaceReceived(
                  conn_ctx.connection_handle,
                  data_ctx_id,
                  track_namespace_prefix,
                  { .request_id = request_id, .filter_type = messages::FilterType::kTrackFilter, .filter = filter });
                return true;
            }
            case messages::ControlMessageType::kNamespaceDone: {
                const auto track_namespace_suffix = messages::Message::ParseField<TrackNamespace>(msg_bytes);

                UnsubscribeNamespaceReceived(conn_ctx.connection_handle, track_namespace_suffix);
                return true;
            }
            case messages::ControlMessageType::kPublishNamespaceDone: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);

                SPDLOG_LOGGER_INFO(logger_, "Received publish namespace done for request_id: {}", request_id);

                auto sub_namespace_conns = PublishNamespaceDoneReceived(conn_ctx.connection_handle, request_id);

                std::lock_guard<std::mutex> _(state_mutex_);
                for (auto conn_id : sub_namespace_conns) {
                    auto conn_it = connections_.find(conn_id);
                    if (conn_it == connections_.end()) {
                        continue;
                    }

                    SendPublishNamespaceDone(conn_it->second, request_id);
                }

                return true;
            }
            case messages::ControlMessageType::kUnsubscribe: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);

                auto& th = conn_ctx.recv_req_id[request_id].track_hash;
                if (auto pdt = GetPubTrackHandler(conn_ctx, th)) {
                    pdt->SetStatus(PublishTrackHandler::Status::kNoSubscribers);
                }

                UnsubscribeReceived(conn_ctx.connection_handle, request_id);
                conn_ctx.recv_req_id.erase(request_id);

                return true;
            }
            case messages::ControlMessageType::kPublishDone: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto status_code = messages::Message::ParseField<messages::PublishDoneStatusCode>(msg_bytes);
                const auto stream_count = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto error_reason = messages::Message::ParseField<Bytes>(msg_bytes);

                auto pub_it = conn_ctx.request_handlers.find(request_id);
                if (pub_it == conn_ctx.request_handlers.end()) {
                    SPDLOG_LOGGER_WARN(logger_,
                                       "Received publish done to unknown subscribe conn_id: {} request_id: {}",
                                       conn_ctx.connection_handle,
                                       request_id);

                    return true;
                }
                auto tfn = pub_it->second.handler->GetFullTrackName();
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_INFO(logger_,
                                   "Received publish done conn_id: {} request_id: {} track namespace hash: {} "
                                   "name hash: {} track alias: {}",
                                   conn_ctx.connection_handle,
                                   request_id,
                                   th.track_namespace_hash,
                                   th.track_name_hash,
                                   th.track_fullname_hash);

                if (auto h = pub_it->second.Get<SubscribeTrackHandler>()) {
                    h->SetStatus(SubscribeTrackHandler::Status::kNotSubscribed);
                    PublishDoneReceived(conn_ctx.connection_handle, request_id);
                }

                conn_ctx.recv_req_id.erase(request_id);

                return true;
            }
            case messages::ControlMessageType::kRequestsBlocked: {
                const auto maximum_request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);

                SPDLOG_LOGGER_WARN(logger_, "Subscribe was blocked, maximum_request_id: {}", maximum_request_id);

                // TODO: React to this somehow.
                // See https://www.ietf.org/archive/id/draft-ietf-moq-transport-08.html#section-7.21
                // A publisher MAY send a MAX_REQUEST_ID upon receipt of SUBSCRIBES_BLOCKED, but it MUST NOT rely on
                // SUBSCRIBES_BLOCKED to trigger sending a MAX_REQUEST_ID, because sending SUBSCRIBES_BLOCKED is not
                // required.

                return true;
            }
            case messages::ControlMessageType::kPublishNamespaceCancel: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto error_code = messages::Message::ParseField<messages::ErrorCode>(msg_bytes);
                const auto error_reason = messages::Message::ParseField<Bytes>(msg_bytes);

                std::string reason(error_reason.begin(), error_reason.end());
                SPDLOG_LOGGER_INFO(logger_,
                                   "Received publish namespace cancel for request_id: {} (error_code={}, reason={})",
                                   request_id,
                                   static_cast<int>(error_code),
                                   reason);

                return true;
            }
            case messages::ControlMessageType::kGoaway: {
                const auto new_session_uri = messages::Message::ParseField<Bytes>(msg_bytes);

                std::string new_sess_uri(new_session_uri.begin(), new_session_uri.end());
                SPDLOG_LOGGER_INFO(logger_, "Received goaway new session uri: {}", new_sess_uri);
                return true;
            }
            case messages::ControlMessageType::kClientSetup: {
                const auto setup_parameters = messages::Message::ParseField<messages::SetupParameters>(msg_bytes);

                std::string endpoint_id = "Unknown Endpoint ID";
                for (const auto& param : setup_parameters) {
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
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto end_of_track = messages::Message::ParseField<std::uint8_t>(msg_bytes);
                const auto end_location = messages::Message::ParseField<messages::Location>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);
                const auto track_extensions = messages::Message::ParseField<messages::TrackExtensions>(msg_bytes);

                auto fetch_it = conn_ctx.request_handlers.find(request_id);
                if (fetch_it == conn_ctx.request_handlers.end()) {
                    SPDLOG_LOGGER_WARN(logger_,
                                       "Received fetch ok for unknown fetch track conn_id: {} request_id: {}, ignored",
                                       conn_ctx.connection_handle,
                                       request_id);
                    return true;
                }

                if (auto h = fetch_it->second.Get<FetchTrackHandler>()) {
                    const auto publisher_default_group_order =
                      track_extensions
                        .GetOptional<messages::GroupOrder>(messages::ExtensionType::kDefaultPublisherGroupOrder)
                        .value_or(messages::GroupOrder::kAscending);
                    h->SetLatestLocation(end_location);
                    h->SetPublisherDefaultGroupOrder(publisher_default_group_order);
                    h->SetStatus(FetchTrackHandler::Status::kOk);
                }

                return true;
            }
            case messages::ControlMessageType::kFetch: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto fetch_type = messages::Message::ParseField<messages::FetchType>(msg_bytes);

                bool relative_joining{ false };
                switch (fetch_type) {
                    case messages::FetchType::kStandalone: {
                        const auto track_namespace = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                        const auto track_name = messages::Message::ParseField<Bytes>(msg_bytes);
                        const auto start = messages::Message::ParseField<messages::Location>(msg_bytes);
                        const auto end = messages::Message::ParseField<messages::Location>(msg_bytes);
                        const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                        FullTrackName tfn{ track_namespace, track_name };

                        // Unwrap with the end location wire format.
                        messages::FetchEndLocation end_location;
                        end_location.group = end.group;
                        if (end.object == 0) {
                            end_location.object = std::nullopt;
                        } else {
                            end_location.object = end.object - 1;
                        }

                        auto priority = parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority);
                        auto group_order =
                          parameters.GetOptional<messages::GroupOrder>(messages::ParameterType::kGroupOrder);

                        messages::StandaloneFetchAttributes attrs = {
                            .priority = priority,
                            .group_order = group_order,
                            .publisher_default_group_order = messages::GroupOrder::kAscending,
                            .start_location = start,
                            .end_location = end_location,
                        };

                        StandaloneFetchReceived(conn_ctx.connection_handle, request_id, tfn, attrs);
                        return true;
                    }
                    case messages::FetchType::kRelativeJoiningFetch: {
                        relative_joining = true;
                        [[fallthrough]];
                    }
                    case messages::FetchType::kAbsoluteJoiningFetch: {
                        const auto joining_request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                        const auto joining_start = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                        const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                        // Joining fetch needs to look up its joining subscribe.
                        const auto subscribe_state = conn_ctx.recv_req_id.find(joining_request_id);
                        if (subscribe_state == conn_ctx.recv_req_id.end()) {
                            SendRequestError(conn_ctx,
                                             request_id,
                                             messages::ErrorCode::kDoesNotExist,
                                             0ms,
                                             "Corresponding subscribe does not exist");
                            return true;
                        }

                        FullTrackName tfn = subscribe_state->second.track_full_name;
                        auto priority = parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority);
                        auto group_order =
                          parameters.GetOptional<messages::GroupOrder>(messages::ParameterType::kGroupOrder);

                        messages::JoiningFetchAttributes attrs = {
                            .priority = priority,
                            .group_order = group_order,
                            .publisher_default_group_order = messages::GroupOrder::kAscending,
                            .joining_request_id = joining_request_id,
                            .relative = relative_joining,
                            .joining_start = joining_start,
                        };

                        JoiningFetchReceived(conn_ctx.connection_handle, request_id, tfn, attrs);
                        return true;
                    }
                    default: {
                        SendRequestError(
                          conn_ctx, request_id, messages::ErrorCode::kNotSupported, 0ms, "Unknown fetch type");
                        return true;
                    }
                }

                return true;
            }
            case messages::ControlMessageType::kFetchCancel: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);

                if (conn_ctx.recv_req_id.find(request_id) == conn_ctx.recv_req_id.end()) {
                    SPDLOG_LOGGER_WARN(logger_, "Received Fetch Cancel for unknown subscribe ID: {}", request_id);
                }

                const auto fetch_it = conn_ctx.request_handlers.find(request_id);
                if (fetch_it == conn_ctx.request_handlers.end()) {
                    FetchCancelReceived(conn_ctx.connection_handle, request_id);
                    return true;
                }

                if (auto h = fetch_it->second.Get<FetchTrackHandler>()) {
                    h->SetStatus(FetchTrackHandler::Status::kCancelled);
                }

                FetchCancelReceived(conn_ctx.connection_handle, request_id);

                conn_ctx.request_handlers.erase(fetch_it);
                conn_ctx.recv_req_id.erase(request_id);

                return true;
            }
            case messages::ControlMessageType::kPublish: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto track_namespace = messages::Message::ParseField<TrackNamespace>(msg_bytes);
                const auto track_name = messages::Message::ParseField<Bytes>(msg_bytes);
                const auto track_alias = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);
                const auto track_extensions = messages::Message::ParseField<messages::TrackExtensions>(msg_bytes);

                auto tfn = FullTrackName{ track_namespace, track_name };
                auto th = TrackHash(tfn);

                conn_ctx.recv_req_id[request_id] = { .track_full_name = tfn, .track_hash = th };

                bool dynamic_groups = true; // TODO: This has moved to extensions/properties get from there
                auto delivery_timeout = parameters.Get<std::uint64_t>(messages::ParameterType::kDeliveryTimeout);
                auto expires = parameters.Get<std::uint64_t>(messages::ParameterType::kExpires);
                const auto publisher_default_group_order =
                  track_extensions
                    .GetOptional<messages::GroupOrder>(messages::ExtensionType::kDefaultPublisherGroupOrder)
                    .value_or(messages::GroupOrder::kAscending);
                auto forward = parameters.Get<bool>(messages::ParameterType::kForward);

                messages::PublishAttributes attributes;
                attributes.track_full_name = tfn;
                attributes.track_alias = track_alias;
                attributes.priority = 0;
                attributes.group_order = std::nullopt;
                attributes.publisher_default_group_order = publisher_default_group_order;
                attributes.delivery_timeout = std::chrono::milliseconds(delivery_timeout);
                attributes.expires = std::chrono::milliseconds(expires);
                attributes.forward = forward;
                attributes.is_publisher_initiated = true;
                attributes.dynamic_groups = dynamic_groups;

                PublishReceived(conn_ctx.connection_handle, request_id, attributes, {});

                return true;
            }
            case messages::ControlMessageType::kPublishOk: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                // Consume originating request.
                auto pub_it = conn_ctx.request_handlers.find(request_id);
                if (pub_it == conn_ctx.request_handlers.end()) {
                    SPDLOG_LOGGER_WARN(logger_, "Received publish ok for unknown publish: {}", request_id);
                    return true;
                }

                // Continue with subscribe flow.
                if (auto pub_h = pub_it->second.Get<PublishTrackHandler>()) {
                    auto th = TrackHash(pub_h->GetFullTrackName());
                    conn_ctx.recv_req_id[request_id] = { .track_full_name = pub_h->GetFullTrackName(),
                                                         .track_hash = th };

                    auto priority = parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority);
                    auto delivery_timeout =
                      parameters.GetOptional<std::uint64_t>(messages::ParameterType::kDeliveryTimeout);
                    auto forward = parameters.GetOptional<bool>(messages::ParameterType::kForward);
                    auto filter = parameters.GetOptional<messages::Filter>(
                      messages::ParameterType::kTrackFilter); // TODO: Support more filters
                    auto new_group_request_id =
                      parameters.GetOptional<std::uint64_t>(messages::ParameterType::kNewGroupRequest);

                    if (pub_h->GetDefaultPriority() < priority) {
                        pub_h->SetDefaultPriority(priority);
                    }

                    if (delivery_timeout.has_value() && *delivery_timeout) {
                        pub_h->SetDefaultTTL(*delivery_timeout);
                    }

                    if (forward.has_value()) {
                        pub_h->SetStatus(*forward ? PublishTrackHandler::Status::kOk
                                                  : PublishTrackHandler::Status::kPaused);
                    }

                    if (new_group_request_id.has_value()) {
                        pub_h->SetStatus(PublishTrackHandler::Status::kNewGroupRequested);
                    }

                    // TODO: Apply filters for publisher, such as location range filters
                }

                return true;
            }
            case messages::ControlMessageType::kRequestUpdate: {
                const auto request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto existing_request_id = messages::Message::ParseField<std::uint64_t>(msg_bytes);
                const auto parameters = messages::Message::ParseField<messages::Parameters>(msg_bytes);

                auto sub_ctx_it = conn_ctx.recv_req_id.find(existing_request_id);
                if (sub_ctx_it == conn_ctx.recv_req_id.end()) {
                    // update for invalid subscription
                    SPDLOG_LOGGER_WARN(logger_,
                                       "Received subscribe_update for unknown subscription conn_id: {} request_id: {}",
                                       conn_ctx.connection_handle,
                                       request_id);

                    SendRequestError(
                      conn_ctx, request_id, messages::ErrorCode::kDoesNotExist, 0ms, "Subscription not found");
                    return true;
                }

                [[maybe_unused]] auto delivery_timeout =
                  parameters.Get<std::uint64_t>(messages::ParameterType::kDeliveryTimeout);
                [[maybe_unused]] auto priority = parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority);
                auto forward = parameters.Get<bool>(messages::ParameterType::kForward);
                auto new_group_request_id =
                  parameters.GetOptional<std::uint64_t>(messages::ParameterType::kNewGroupRequest);

                messages::Filter filter; // TODO: Support more filters.
                if (parameters.Contains(messages::ParameterType::kLocationFilter)) {
                    filter = parameters.GetFilter(messages::FilterType::kLocationFilter);
                } else if (parameters.Contains(messages::ParameterType::kTrackFilter)) {
                    filter = parameters.GetFilter(messages::FilterType::kTrackFilter);
                }

                if (new_group_request_id.has_value()) {
                    NewGroupRequested(sub_ctx_it->second.track_full_name, new_group_request_id.value());
                }

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received subscribe_update to recv request_id: {} forward: {} ngr: {}",
                                    request_id,
                                    forward,
                                    new_group_request_id.has_value());

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

}
