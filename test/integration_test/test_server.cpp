#include "test_server.h"

#include "quicr/detail/base_track_handler.h"

using namespace quicr;
using namespace quicr_test;

TestServer::TestServer(const ServerConfig& config)
  : Server(config)
{
}

void
TestServer::PublishReceived(const ConnectionHandle connection_handle,
                            const uint64_t request_id,
                            const messages::PublishAttributes& publish_attributes)
{
    std::lock_guard lock(state_mutex_);

    const auto th = TrackHash(publish_attributes.track_full_name);
    const auto track_alias = th.track_fullname_hash;

    // Is anyone interested in this prefix?
    std::vector<ConnectionHandle> namespace_subscribers;
    for (const auto& interested : namespace_subscribers_) {
        if (interested.first.IsPrefixOf(publish_attributes.track_full_name.name_space)) {
            for (const auto& handle : interested.second) {
                namespace_subscribers.push_back(handle);
            }
        }
    }

    // Create a subscribe handler to receive objects from the publisher
    auto sub_track_handler = std::make_shared<TestSubscribeTrackHandler>(publish_attributes.track_full_name, true);

    sub_track_handler->SetRequestId(request_id);
    sub_track_handler->SetReceivedTrackAlias(publish_attributes.track_alias);
    sub_track_handler->SetPriority(publish_attributes.priority);

    // If there are subscribers for this track, link the subscribe handler to forward to them
    auto sub_it = subscribes_.find(track_alias);
    if (sub_it != subscribes_.end() && !sub_it->second.empty()) {
        // Link to first subscriber's publish handler for forwarding
        auto& pub_handler = sub_it->second.begin()->second;
        if (pub_handler) {
            sub_track_handler->SetPublishHandler(pub_handler);
        }
    }

    SubscribeTrack(connection_handle, sub_track_handler);
    pub_subscribes_[track_alias][connection_handle] = sub_track_handler;

    ResolvePublish(connection_handle,
                   request_id,
                   publish_attributes,
                   { .reason_code = PublishResponse::ReasonCode::kOk, .namespace_subscribers = namespace_subscribers });
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

    if (publish_accepted_promise_.has_value()) {
        publish_accepted_promise_->set_value(details);
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
                         { .reason_code = SubscribeResponse::ReasonCode::kOk,
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
                                       const TrackNamespace& prefix_namespace,
                                       const SubscribeNamespaceAttributes& attributes)
{
    if (subscribe_namespace_promise_.has_value()) {
        subscribe_namespace_promise_->set_value({ connection_handle, prefix_namespace, attributes });
    }

    // Deliberately not prefix matching to allow testing bad case. Tests should only add tracks
    // with this in mind.
    const SubscribeNamespaceResponse response = { .reason_code = SubscribeNamespaceResponse::ReasonCode::kOk,
                                                  .tracks = known_published_tracks_,
                                                  .namespaces = known_published_namespaces_ };

    // Store this subscriber's interest in the prefix.
    const auto it = namespace_subscribers_.find(prefix_namespace);
    if (it == namespace_subscribers_.end()) {
        namespace_subscribers_[prefix_namespace].push_back(connection_handle);
    } else {
        it->second.push_back(connection_handle);
    }

    // Blindly accept it.
    ResolveSubscribeNamespace(connection_handle, attributes.request_id, prefix_namespace, response);
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
    known_published_tracks_.emplace_back(track, largest_location, attributes);
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
