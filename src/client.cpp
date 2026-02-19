// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/detail/transport.h"

#include <optional>
#include <quicr/client.h>

#include "quicr/detail/transport.h"

namespace quicr {
    using namespace std::chrono_literals;

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

    void Client::PublishNamespaceStatusChanged(messages::RequestID, const PublishNamespaceStatus) {}
    void Client::PublishNamespaceReceived(const TrackNamespace&, const PublishNamespaceAttributes&) {}
    void Client::PublishNamespaceDoneReceived(messages::RequestID) {}

    void Client::PublishReceived(const ConnectionHandle connection_handle,
                                 const uint64_t request_id,
                                 const messages::PublishAttributes& publish_attributes)
    {
        ResolvePublish(connection_handle,
                       request_id,
                       publish_attributes,
                       { .reason_code = PublishResponse::ReasonCode::kNotSupported });
    }

    void Client::UnpublishedSubscribeReceived(const FullTrackName&, const messages::SubscribeAttributes&)
    {
        // TODO: add the default response
    }

    void Client::ResolveSubscribe(ConnectionHandle connection_handle,
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
                SendSubscribeOk(
                  conn_it->second, request_id, track_alias, kSubscribeExpires, subscribe_response.largest_location);
                break;
            }

            default:
                SendRequestError(
                  conn_it->second, request_id, messages::ErrorCode::kInternalError, 0ms, "Internal error");
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
                              std::uint8_t priority,
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
                SendRequestError(conn_it->second,
                                 request_id,
                                 messages::ErrorCode::kInternalError,
                                 0ms,
                                 response.error_reason.has_value() ? response.error_reason.value() : "Internal error");
                break;
        }
    }

    void Client::RequestUpdateReceived(ConnectionHandle connection_handle,
                                       uint64_t request_id,
                                       uint64_t existing_request_id,
                                       const messages::Parameters& params)
    {
        ResolveRequestUpdate(connection_handle, request_id, existing_request_id, params);
    }

    void Client::ResolveRequestUpdate(ConnectionHandle connection_handle,
                                      uint64_t request_id,
                                      uint64_t existing_request_id,
                                      const messages::Parameters& params)
    {
        auto conn_it = connections_.find(connection_handle);
        if (conn_it == connections_.end()) {
            return;
        }

        auto track_it = conn_it->second.request_handlers.find(existing_request_id);
        if (track_it == conn_it->second.request_handlers.end()) {

            return SendRequestError(conn_it->second,
                                    request_id,
                                    messages::ErrorCode::kDoesNotExist,
                                    0ms,
                                    "Found no track for existing request ID");
        }

        SPDLOG_LOGGER_DEBUG(logger_,
                            "Request Updated resolve req_id: {} existing_id: {} track handler type: {}",
                            request_id,
                            existing_request_id,
                            static_cast<int>(track_it->second.GetType()));

        track_it->second.handler->RequestUpdate(request_id, existing_request_id, params);

        SendRequestOk(conn_it->second, request_id);
    }

    void Client::BindFetchTrack(TransportConnId conn_id, std::shared_ptr<PublishFetchHandler> track_handler)
    {
        const std::uint64_t request_id = *track_handler->GetRequestId();

        SPDLOG_LOGGER_INFO(logger_, "Publish fetch track conn_id: {} subscribe: {}", conn_id, request_id);

        std::lock_guard lock(state_mutex_);

        auto conn_it = connections_.find(conn_id);
        if (conn_it == connections_.end()) {
            SPDLOG_LOGGER_ERROR(logger_, "Publish fetch track conn_id: {} does not exist.", conn_id);
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

    void Client::MetricsSampled(const ConnectionMetrics&) {}

    PublishNamespaceStatus Client::GetPublishNamespaceStatus(const TrackNamespace&)
    {
        return PublishNamespaceStatus();
    }

    void Client::PublishNamespace(std::shared_ptr<PublishNamespaceHandler> handler)
    {
        if (!connection_handle_) {
            return;
        }

        Transport::PublishNamespace(connection_handle_.value(), std::move(handler));
    }

    void Client::PublishNamespaceDone(const std::shared_ptr<PublishNamespaceHandler>& handler)
    {
        if (!connection_handle_) {
            return;
        }

        Transport::PublishNamespaceDone(connection_handle_.value(), handler);
    }

    void Client::SubscribeNamespace(std::shared_ptr<SubscribeNamespaceHandler> handler)
    {
        if (!connection_handle_) {
            return;
        }

        SendSubscribeNamespace(*connection_handle_, std::move(handler));
    }

    void Client::UnsubscribeNamespace(const std::shared_ptr<SubscribeNamespaceHandler>& handler)
    {
        if (!connection_handle_) {
            return;
        }

        SendUnsubscribeNamespace(*connection_handle_, handler);
    }

    bool Client::ProcessCtrlMessage(ConnectionContext& conn_ctx,
                                    [[maybe_unused]] uint64_t data_ctx_id,
                                    messages::ControlMessageType msg_type,
                                    BytesSpan msg_bytes)
    try {
        switch (msg_type) {
            case messages::ControlMessageType::kRequestOk: {
                messages::RequestOk msg;
                msg_bytes >> msg;

                auto track_it = conn_ctx.request_handlers.find(msg.request_id);
                if (track_it == conn_ctx.request_handlers.end()) {
                    SPDLOG_LOGGER_WARN(logger_,
                                       "Received REQUEST_OK to unknown track conn_id: {} request_id: {}, ignored",
                                       conn_ctx.connection_handle,
                                       msg.request_id);
                    return true;
                }

                auto& track_handler = track_it->second.handler;

                track_handler->RequestOk(msg.request_id, msg.parameters);

                RequestOkReceived(connection_handle_.value(), msg.request_id);
                return true;
            }
            case messages::ControlMessageType::kRequestError: {
                messages::RequestError msg;
                msg_bytes >> msg;

                auto track_it = conn_ctx.request_handlers.find(msg.request_id);
                if (track_it == conn_ctx.request_handlers.end()) {
                    SPDLOG_LOGGER_WARN(logger_,
                                       "Received REQUEST_ERROR to unknown track conn_id: {} request_id: {}, ignored",
                                       conn_ctx.connection_handle,
                                       msg.request_id);
                    return true;
                }

                auto& track_handler = track_it->second.handler;

                track_handler->RequestError(msg.error_code,
                                            std::string(msg.error_reason.begin(), msg.error_reason.end()));

                RequestErrorReceived(connection_handle_.value(),
                                     msg.request_id,
                                     {
                                       .reason_code = RequestResponse::FromErrorCode(msg.error_code),
                                       .error_reason = std::string(msg.error_reason.begin(), msg.error_reason.end()),
                                     });

                return true;
            }
            case messages::ControlMessageType::kSubscribe: {
                messages::Subscribe msg;

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

                    SendRequestError(
                      conn_ctx, msg.request_id, messages::ErrorCode::kDoesNotExist, 0ms, "Published track not found");
                    return true;
                }

                ResolveSubscribe(conn_ctx.connection_handle,
                                 msg.request_id,
                                 ptd->GetTrackAlias().value(),
                                 { RequestResponse::ReasonCode::kOk });

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received subscribe to announced track alias: {} recv request_id: {}, setting "
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
            case messages::ControlMessageType::kRequestUpdate: {
                messages::RequestUpdate msg;
                msg_bytes >> msg;

                /*
                auto delivery_timeout = msg.parameters.Get<std::uint64_t>(messages::ParameterType::kDeliveryTimeout);
                auto priority = msg.parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority);
                auto filter_type =
                  msg.parameters.Get<messages::FilterType>(messages::ParameterType::kSubscriptionFilter);
                auto forward = msg.parameters.Get<bool>(messages::ParameterType::kForward);
                auto new_group_request_id =
                  msg.parameters.GetOptional<std::uint64_t>(messages::ParameterType::kNewGroupRequest);
                */
                RequestUpdateReceived(
                  conn_ctx.connection_handle, msg.request_id, msg.existing_request_id, msg.parameters);

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

                auto sub_handler = static_cast<SubscribeTrackHandler*>(sub_it->second.handler.get());

                auto expires = msg.parameters.Get<std::uint64_t>(messages::ParameterType::kExpires);

                std::optional<messages::Location> largest_location;
                if (msg.parameters.Contains(messages::ParameterType::kLargestObject)) {
                    largest_location = msg.parameters.Get<messages::Location>(messages::ParameterType::kLargestObject);
                }

                if (largest_location.has_value()) {
                    SPDLOG_LOGGER_DEBUG(
                      logger_,
                      "Received subscribe ok conn_id: {} request_id: {} latest_group: {} latest_object: {}",
                      conn_ctx.connection_handle,
                      msg.request_id,
                      largest_location.value().group,
                      largest_location.value().object);

                    sub_handler->SetLatestLocation(largest_location.value());
                }

                sub_handler->SetReceivedTrackAlias(msg.track_alias);
                conn_ctx.sub_by_recv_track_alias[msg.track_alias] = sub_it->second.handler;
                sub_handler->SetStatus(SubscribeTrackHandler::Status::kOk);

                return true;
            }
            case messages::ControlMessageType::kPublishNamespace: {
                messages::PublishNamespace msg;
                msg_bytes >> msg;

                // TODO: Use params.

                PublishNamespaceReceived(msg.track_namespace, { .request_id = msg.request_id });
                return true;
            }
            case messages::ControlMessageType::kPublishNamespaceDone: {
                messages::PublishNamespaceDone msg;
                msg_bytes >> msg;

                PublishNamespaceDoneReceived(msg.request_id);
                return true;
            }
            case messages::ControlMessageType::kUnsubscribe: {
                messages::Unsubscribe msg;
                msg_bytes >> msg;

                const auto& th_it = conn_ctx.recv_req_id.find(msg.request_id);

                if (th_it == conn_ctx.recv_req_id.end()) {
                    SPDLOG_LOGGER_WARN(logger_,
                                       "Received unsubscribe to unknown request_id conn_id: {} request_id: {}, ignored",
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
                                    "Received unsubscribe conn_id: {} request_id: {}",
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
            case messages::ControlMessageType::kPublish: {
                messages::Publish msg{};
                msg_bytes >> msg;

                const FullTrackName tfn{ .name_space = msg.track_namespace, .name = msg.track_name };
                const TrackHash th(tfn);
                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received publish conn_id: {} request_id: {} track namespace hash: {} "
                                    "name hash: {} track alias: {}",
                                    conn_ctx.connection_handle,
                                    msg.request_id,
                                    th.track_namespace_hash,
                                    th.track_name_hash,
                                    msg.track_alias);

                auto delivery_timeout = msg.parameters.Get<std::uint64_t>(messages::ParameterType::kDeliveryTimeout);
                auto expires = msg.parameters.Get<std::uint64_t>(messages::ParameterType::kExpires);
                auto group_order = msg.parameters.Get<messages::GroupOrder>(messages::ParameterType::kGroupOrder);
                auto forward = msg.parameters.Get<bool>(messages::ParameterType::kForward);
                auto new_group_request_id =
                  msg.parameters.GetOptional<std::uint64_t>(messages::ParameterType::kNewGroupRequest);

                messages::PublishAttributes attrs;
                attrs.track_full_name = tfn;
                attrs.track_alias = msg.track_alias;
                attrs.forward = forward;
                attrs.group_order = group_order;
                attrs.expires = std::chrono::milliseconds(delivery_timeout);
                attrs.is_publisher_initiated = true;
                attrs.new_group_request_id = new_group_request_id;
                PublishReceived(conn_ctx.connection_handle, msg.request_id, attrs);
                return true;
            }
            case messages::ControlMessageType::kPublishDone: {
                messages::PublishDone msg;
                msg_bytes >> msg;

                auto sub_it = conn_ctx.request_handlers.find(msg.request_id);
                if (sub_it == conn_ctx.request_handlers.end()) {
                    SPDLOG_LOGGER_WARN(logger_,
                                       "Received subscribe done to unknown request_id conn_id: {} request_id: {}",
                                       conn_ctx.connection_handle,
                                       msg.request_id);

                    // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race
                    // condition
                    return true;
                }

                auto tfn = sub_it->second.handler.get()->GetFullTrackName();

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received publish done conn_id: {} request_id: {} track namespace hash: {} "
                                    "name hash: {} track alias: {}",
                                    conn_ctx.connection_handle,
                                    msg.request_id,
                                    TrackHash(tfn).track_namespace_hash,
                                    TrackHash(tfn).track_name_hash,
                                    TrackHash(tfn).track_fullname_hash);

                static_cast<SubscribeTrackHandler*>(sub_it->second.handler.get())
                  ->SetStatus(SubscribeTrackHandler::Status::kNotSubscribed);
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

                SPDLOG_LOGGER_INFO(logger_, "Received publish namespace cancel for request_id: {}", msg.request_id);
                PublishNamespaceStatusChanged(msg.request_id, PublishNamespaceStatus::kNotPublished);
                return true;
            }
            case messages::ControlMessageType::kTrackStatus: {
                auto msg = messages::TrackStatus{};

                msg_bytes >> msg;

                auto tfn = FullTrackName{ msg.track_namespace, msg.track_name };
                auto th = TrackHash(tfn);

                SPDLOG_LOGGER_DEBUG(logger_,
                                    "Received track status request for namespace_hash: {} name_hash: {}",
                                    th.track_namespace_hash,
                                    th.track_name_hash);

                TrackStatusReceived(conn_ctx.connection_handle, msg.request_id, tfn);
                return true;
            }
            case messages::ControlMessageType::kGoaway: {
                messages::Goaway msg;
                msg_bytes >> msg;

                std::string new_sess_uri(msg.new_session_uri.begin(), msg.new_session_uri.end());
                SPDLOG_LOGGER_INFO(logger_, "Received goaway new session uri: {}", new_sess_uri);
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

                ServerSetupReceived({ 0, endpoint_id });
                SetStatus(Status::kReady);

                SPDLOG_LOGGER_INFO(
                  logger_, "Server setup received conn_id: {} from: {}", conn_ctx.connection_handle, endpoint_id);

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

                static_cast<FetchTrackHandler*>(fetch_it->second.handler.get())->SetLatestLocation(msg.end_location);
                static_cast<FetchTrackHandler*>(fetch_it->second.handler.get())
                  ->SetStatus(FetchTrackHandler::Status::kOk);

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

                        messages::StandaloneFetchAttributes attrs = { .priority = priority,
                                                                      .group_order = group_order,
                                                                      .start_location = msg.group_0->standalone.start,
                                                                      .end_location = end_location };
                        StandaloneFetchReceived(conn_ctx.connection_handle, msg.request_id, tfn, attrs);
                        return true;
                    }
                    case messages::FetchType::kRelativeJoiningFetch: {
                        [[fallthrough]];
                    }
                    case messages::FetchType::kAbsoluteJoiningFetch: {
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

                static_cast<FetchTrackHandler*>(fetch_it->second.handler.get())
                  ->SetStatus(FetchTrackHandler::Status::kCancelled);

                FetchCancelReceived(conn_ctx.connection_handle, msg.request_id);

                conn_ctx.request_handlers.erase(fetch_it);
                conn_ctx.recv_req_id.erase(msg.request_id);

                return true;
            }
            case messages::ControlMessageType::kPublishOk: {
                messages::PublishOk msg{};
                msg_bytes >> msg;

                auto pub_it = conn_ctx.request_handlers.find(msg.request_id);

                if (pub_it == conn_ctx.request_handlers.end()) {
                    SPDLOG_LOGGER_WARN(
                      logger_,
                      "Received publish ok to unknown publish track conn_id: {} request_id: {}, ignored",
                      conn_ctx.connection_handle,
                      msg.request_id);

                    return true;
                }

                auto priority = msg.parameters.Get<uint8_t>(messages::ParameterType::kSubscriberPriority);
                auto delivery_timeout = msg.parameters.Get<std::uint64_t>(messages::ParameterType::kDeliveryTimeout);
                auto expires = msg.parameters.Get<std::uint64_t>(messages::ParameterType::kExpires);
                auto group_order = msg.parameters.Get<messages::GroupOrder>(messages::ParameterType::kGroupOrder);
                auto forward = msg.parameters.Get<bool>(messages::ParameterType::kForward);
                auto filter_type =
                  msg.parameters.Get<messages::FilterType>(messages::ParameterType::kSubscriptionFilter);

                std::optional<uint64_t> new_group_request_id;
                if (msg.parameters.Contains(messages::ParameterType::kNewGroupRequest)) {
                    new_group_request_id = msg.parameters.Get<std::uint64_t>(messages::ParameterType::kNewGroupRequest);
                }

                if (pub_it->second.GetType() == TrackHandler::Type::kPublish) {
                    static_cast<PublishTrackHandler*>(pub_it->second.handler.get())
                      ->SetStatus(forward ? PublishTrackHandler::Status::kOk : PublishTrackHandler::Status::kPaused);
                }

                return true;
            }
            default: {
                SPDLOG_LOGGER_ERROR(
                  logger_, "Unsupported MOQT message type: {}, bad stream", static_cast<uint64_t>(msg_type));
                return false;
            }
        }

        return false;

    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger_,
                            "Caught exception trying to process control message. (type={}, error={})",
                            static_cast<int>(msg_type),
                            e.what());
        return false;
    }
} // namespace quicr
