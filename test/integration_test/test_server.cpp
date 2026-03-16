#include "test_server.h"

#include "quicr/detail/base_track_handler.h"
#include "quicr/publish_fetch_handler.h"
#include "quicr/publish_namespace_handler.h"

#include <ranges>

using namespace quicr;
using namespace quicr_test;

void
TestPublishTrackHandler::StatusChanged(Status status)
{
    switch (status) {
        case Status::kOk: {
            if (auto svr = server_.lock()) {
                if (svr->publish_accepted_promise_.has_value()) {
                    svr->publish_accepted_promise_->set_value(
                      { GetConnectionId(), GetRequestId().value(), GetFullTrackName(), {} });
                }
            }
            break;
        }
        default:
            break;
    }
}

TestServer::TestServer(const ServerConfig& config)
  : Server(config)
{
}

void
TestServer::PublishReceived(const ConnectionHandle connection_handle,
                            const uint64_t request_id,
                            const messages::PublishAttributes& publish_attributes,
                            [[maybe_unused]] std::weak_ptr<quicr::SubscribeNamespaceHandler> ns_handler)
{
    std::lock_guard lock(state_mutex_);

    const auto th = TrackHash(publish_attributes.track_full_name);
    const auto track_alias = th.track_fullname_hash;

    // Is anyone interested in this prefix?
    std::vector<ConnectionHandle> namespace_subscribers;

    for (const auto& [_, interested] : namespace_subscribers_) {
        for (const auto& [conn_id, ns_handler] : interested) {
            if (ns_handler->GetFullTrackName().name_space.HasSamePrefix(
                  publish_attributes.track_full_name.name_space)) {
                auto handler =
                  std::make_shared<TestPublishTrackHandler>(publish_attributes.track_full_name,
                                                            quicr::TrackMode::kStream,
                                                            publish_attributes.priority,
                                                            publish_attributes.delivery_timeout.count(),
                                                            std::static_pointer_cast<TestServer>(shared_from_this()));
                ns_handler->PublishTrack(handler);
            }
        }
    }

    // Create a subscribe handler to receive objects from the publisher
    auto sub_track_handler = std::make_shared<TestSubscribeTrackHandler>(publish_attributes.track_full_name);

    // If there are subscribers for this track, link the subscribe handler to forward to them
    auto sub_it = subscribes_.find(track_alias);
    if (sub_it != subscribes_.end() && !sub_it->second.empty()) {
        // Link to first subscriber's publish handler for forwarding
        auto& pub_handler = sub_it->second.begin()->second;
        if (pub_handler) {
            sub_track_handler->SetPublishHandler(pub_handler);
            sub_track_handler->StatusChanged(SubscribeTrackHandler::Status::kOk);
        }
    }

    pub_subscribes_[track_alias][connection_handle] = sub_track_handler;

    ResolvePublish(connection_handle,
                   request_id,
                   publish_attributes,
                   { .reason_code = PublishResponse::ReasonCode::kOk },
                   sub_track_handler);
}

void
TestServer::PublishDoneReceived(quicr::ConnectionHandle connection_handle, uint64_t request_id)
{
    std::lock_guard lock(state_mutex_);

    // Clean up publisher subscribe handlers
    for (auto& [track_alias, conn_map] : pub_subscribes_) {
        auto it = conn_map.find(connection_handle);
        if (it != conn_map.end() && it->second && it->second->GetRequestId() == request_id) {
            conn_map.erase(it);
            break;
        }
    }
}

void
TestServer::SubscribeReceived(ConnectionHandle connection_handle,
                              uint64_t request_id,
                              const FullTrackName& track_full_name,
                              const messages::SubscribeAttributes& subscribe_attributes)
{
    std::lock_guard lock(state_mutex_);

    const SubscribeDetails details = { connection_handle, request_id, track_full_name, subscribe_attributes };
    if (subscribe_promise_.has_value()) {
        subscribe_promise_->set_value(details);
    }

    const auto th = TrackHash(track_full_name);
    const auto track_alias = th.track_fullname_hash;

    // Calculate TTL from delivery timeout
    const std::uint32_t ttl = subscribe_attributes.delivery_timeout != std::chrono::milliseconds::zero()
                                ? static_cast<std::uint32_t>(subscribe_attributes.delivery_timeout.count())
                                : 5000;

    // Create a publish track handler to send objects to this subscriber
    auto pub_track_handler = std::make_shared<TestPublishTrackHandler>(
      track_full_name, TrackMode::kStream, subscribe_attributes.priority, ttl);

    if (!subscribe_attributes.is_publisher_initiated) {
        ResolveSubscribe(connection_handle,
                         request_id,
                         track_alias,
                         { .reason_code = RequestResponse::ReasonCode::kOk,
                           .is_publisher_initiated = subscribe_attributes.is_publisher_initiated });
    }

    // Store the publish handler for this subscriber
    subscribes_[track_alias][connection_handle] = pub_track_handler;

    // Bind the publish track handler to send data to the subscriber
    BindPublisherTrack(connection_handle, connection_handle, request_id, pub_track_handler, false);

    // Link any existing publisher subscribe handlers to forward to this subscriber
    auto pub_sub_it = pub_subscribes_.find(track_alias);
    if (pub_sub_it != pub_subscribes_.end()) {
        for (auto& [pub_conn, sub_handler] : pub_sub_it->second) {
            if (sub_handler) {
                sub_handler->SetPublishHandler(pub_track_handler);
            }
        }
    }
}

void
TestServer::SubscribeNamespaceReceived(const ConnectionHandle connection_handle,
                                       const DataContextId data_ctx_id,
                                       const TrackNamespace& prefix_namespace,
                                       const messages::SubscribeNamespaceAttributes& attributes)
{
    if (subscribe_namespace_promise_.has_value()) {
        subscribe_namespace_promise_->set_value({ connection_handle, prefix_namespace, attributes });
    }

    // Deliberately not prefix matching to allow testing bad case. Tests should only add tracks
    // with this in mind.
    const SubscribeNamespaceResponse response = { .reason_code = SubscribeNamespaceResponse::ReasonCode::kOk,
                                                  .namespaces = known_published_namespaces_ };

    // Blindly accept it.
    ResolveSubscribeNamespace(connection_handle, data_ctx_id, attributes.request_id, prefix_namespace, response);

    auto ns_handler = PublishNamespaceHandler::Create(prefix_namespace);
    PublishNamespace(connection_handle, ns_handler, true);

    for (const auto track : known_published_tracks_) {
        auto handler =
          std::make_shared<TestPublishTrackHandler>(track.full_track_name,
                                                    quicr::TrackMode::kStream,
                                                    track.attributes.priority,
                                                    track.attributes.delivery_timeout.count(),
                                                    std::static_pointer_cast<TestServer>(shared_from_this()));
        ns_handler->PublishTrack(handler);
    }

    namespace_subscribers_[prefix_namespace][connection_handle] = ns_handler;
}

void
TestServer::AddKnownPublishedNamespace(const TrackNamespace& track_namespace)
{
    known_published_namespaces_.push_back(track_namespace);
}

void
TestServer::AddKnownPublishedTrack(const FullTrackName& track,
                                   const std::optional<messages::Location>& largest_location,
                                   const messages::PublishAttributes& attributes)
{
    known_published_tracks_.emplace_back(
      AvailableTrack{ track, largest_location.value_or(messages::Location{ 0, 0 }), attributes });
}

void
TestServer::PublishNamespaceReceived(const ConnectionHandle connection_handle,
                                     const TrackNamespace& track_namespace,
                                     const PublishNamespaceAttributes& publish_announce_attributes)
{
    if (publish_namespace_promise_.has_value()) {
        publish_namespace_promise_->set_value({ connection_handle, track_namespace, publish_announce_attributes });
    }

    // Accept the publish namespace by responding with OK
    const PublishNamespaceResponse response = { .reason_code = PublishNamespaceResponse::ReasonCode::kOk };
    ResolvePublishNamespace(connection_handle, publish_announce_attributes.request_id, track_namespace, {}, response);
}

void
TestServer::StandaloneFetchReceived(const ConnectionHandle connection_handle,
                                    const uint64_t request_id,
                                    const FullTrackName& track_full_name,
                                    const messages::StandaloneFetchAttributes& attrs)
{
    if (fetch_response_data_.empty()) {
        // No response data configured
        ResolveFetch(connection_handle,
                     request_id,
                     attrs.priority,
                     attrs.group_order,
                     { .reason_code = FetchResponse::ReasonCode::kInternalError,
                       .error_reason = "No fetch test response configured" });
        return;
    }

    // Create location for the response
    const messages::Location largest_location = { .group = fetch_response_data_.back().headers.group_id,
                                                  .object = fetch_response_data_.back().headers.object_id };

    // Accept the fetch
    ResolveFetch(connection_handle,
                 request_id,
                 attrs.priority,
                 attrs.group_order,
                 { .reason_code = FetchResponse::ReasonCode::kOk, .largest_location = largest_location });

    // Publish the response
    auto pub_fetch_handler =
      PublishFetchHandler::Create(track_full_name, attrs.priority, request_id, attrs.group_order, 500);
    BindFetchTrack(connection_handle, pub_fetch_handler);
    for (size_t i = 0; i < fetch_response_data_.size(); ++i) {
        pub_fetch_handler->PublishObject(fetch_response_data_[i].headers, fetch_response_data_[i].payload);
    }
}

void
TestServer::NewGroupRequested(const quicr::FullTrackName& track_full_name, quicr::messages::GroupId group_id)
{
    std::lock_guard lock(state_mutex_);
    const auto th = quicr::TrackHash(track_full_name);

    auto it = pub_subscribes_.find(th.track_fullname_hash);
    if (it == pub_subscribes_.end()) {
        return;
    }

    for (auto& [conn_id, sub_handler] : it->second) {
        if (sub_handler) {
            sub_handler->RequestNewGroup(group_id);
        }
    }
}
