#include "test_server.h"

#include "quicr/detail/base_track_handler.h"

using namespace quicr;
using namespace quicr_test;

TestServer::TestServer(const ServerConfig& config)
  : Server(config)
{
}

void
PublishDoneReceived([[maybe_unused]] quicr::ConnectionHandle connection_handle, [[maybe_unused]] uint64_t request_id)
{
}

void
PublishReceived([[maybe_unused]] quicr::ConnectionHandle connection_handle, [[maybe_unused]] uint64_t request_id)
{
}

void
TestServer::PublishReceived([[maybe_unused]] quicr::ConnectionHandle connection_handle,
                            [[maybe_unused]] uint64_t request_id,
                            [[maybe_unused]] const quicr::FullTrackName& track_full_name,
                            [[maybe_unused]] const quicr::messages::PublishAttributes& publish_attributes)
{
    // Is anyone interested in this prefix?
    std::vector<ConnectionHandle> namespace_subscribers;
    for (const auto& interested : namespace_subscribers_) {
        if (interested.first.IsPrefixOf(track_full_name.name_space)) {
            for (const auto& handle : interested.second) {
                namespace_subscribers.push_back(handle);
            }
        }
    }

    ResolvePublish(connection_handle,
                   request_id,
                   track_full_name,
                   publish_attributes,
                   { .namespace_subscribers = namespace_subscribers, .reason_code = PublishResponse::ReasonCode::kOk });
}

void
TestServer::PublishDoneReceived([[maybe_unused]] quicr::ConnectionHandle connection_handle,
                                [[maybe_unused]] uint64_t request_id)
{
}

void
TestServer::SubscribeReceived(ConnectionHandle connection_handle,
                              uint64_t request_id,
                              messages::FilterType filter_type,
                              const FullTrackName& track_full_name,
                              const messages::SubscribeAttributes& subscribe_attributes)
{
    const SubscribeDetails details = {
        connection_handle, request_id, filter_type, track_full_name, subscribe_attributes
    };
    if (subscribe_promise_.has_value()) {
        subscribe_promise_->set_value(details);
    }

    if (publish_accepted_promise_.has_value()) {
        publish_accepted_promise_->set_value(details);
    }
    const auto th = TrackHash(track_full_name);
    ResolveSubscribe(connection_handle,
                     request_id,
                     th.track_fullname_hash,
                     { .reason_code = SubscribeResponse::ReasonCode::kOk,
                       .is_publisher_initiated = subscribe_attributes.is_publisher_initiated });
}

void
TestServer::SubscribeNamespaceReceived(const ConnectionHandle connection_handle,
                                       const TrackNamespace& prefix_namespace,
                                       const SubscribeNamespaceAttributes& attributes)
{
    if (subscribe_namespace_promise_.has_value()) {
        subscribe_namespace_promise_->set_value({ connection_handle, prefix_namespace, attributes });
    }

    const SubscribeNamespaceResponse response = { .reason_code = SubscribeNamespaceResponse::ReasonCode::kOk,
                                                  .namespaces = known_published_namespaces_,
                                                  .tracks = known_published_tracks_ };

    // Store this subscriber's interest in the prefix.
    const auto it = namespace_subscribers_.find(prefix_namespace);
    if (it == namespace_subscribers_.end()) {
        namespace_subscribers_[prefix_namespace].push_back(connection_handle);
    } else {
        it->second.push_back(connection_handle);
    }

    // Blindly accept it.
    ResolveSubscribeNamespace(connection_handle, attributes.request_id, response);
}

void
TestServer::AddKnownPublishedNamespace(const TrackNamespace& track_namespace)
{
    known_published_namespaces_.push_back(track_namespace);
}

void
TestServer::AddKnownPublishedTrack(const FullTrackName& track)
{
    known_published_tracks_.push_back({ .track_full_name = track, .largest_location = std::nullopt });
}
