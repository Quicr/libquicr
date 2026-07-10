#include "test_server.h"

#include "quicr/handlers/publish_fetch_handler.h"
#include "quicr/handlers/publish_namespace_handler.h"
#include "quicr/handlers/track_handler.h"

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
                    svr->publish_accepted_promise_->set_value({ GetRequestId().value(), GetFullTrackName(), {} });
                }
            }
            break;
        }
        case Status::kUnsubscribed: {
            if (const auto svr = server_.lock()) {
                if (svr->unsubscribe_promise_.has_value()) {
                    svr->unsubscribe_promise_->set_value(GetRequestId().value());
                    svr->unsubscribe_promise_.reset();
                }
            }
            break;
        }
        default:
            break;
    }
}

TestServer::TestServer(const ServerConfig& config,
                       std::shared_ptr<Transport> transport,
                       std::shared_ptr<Connection> connection,
                       std::shared_ptr<timeq::tick_service> tick_service)
  : Session(config, std::move(transport), std::move(connection), std::move(tick_service))
{
}

void
TestServer::PublishReceived(const std::uint64_t request_id,
                            const PublishAttributes& publish_attributes,
                            [[maybe_unused]] std::weak_ptr<quicr::SubscribeNamespaceHandler> ns_handler)
{
    std::lock_guard lock(state_mutex_);

    const auto th = TrackHash(publish_attributes.track_full_name);
    const auto track_alias = th.track_fullname_hash;

    // Is anyone interested in this prefix?
    std::vector<std::uint64_t> namespace_subscribers;

    for (const auto& [_, ns_handler] : namespace_subscribers_) {
        if (ns_handler->GetFullTrackName().name_space.HasSamePrefix(publish_attributes.track_full_name.name_space)) {
            const auto delivery_timeout = publish_attributes.delivery_timeout.value_or(0);
            auto handler =
              std::make_shared<TestPublishTrackHandler>(publish_attributes.track_full_name,
                                                        quicr::TrackMode::kStream,
                                                        publish_attributes.default_publisher_priority,
                                                        delivery_timeout,
                                                        std::static_pointer_cast<TestServer>(shared_from_this()));
            ns_handler->PublishTrack(handler);
        }
    }

    // Create a subscribe handler to receive objects from the publisher
    auto sub_track_handler = std::make_shared<TestSubscribeTrackHandler>(publish_attributes.track_full_name, true);

    // If there are subscribers for this track, link the subscribe handler to forward to them
    auto sub_it = subscribes_.find(track_alias);
    if (sub_it != subscribes_.end()) {
        // Link to first subscriber's publish handler for forwarding
        auto& pub_handler = sub_it->second;
        if (pub_handler) {
            sub_track_handler->SetPublishHandler(pub_handler);
            sub_track_handler->Resume();
        }
    }

    pub_subscribes_[track_alias] = sub_track_handler;

    ResolvePublish(
      request_id, publish_attributes, { .reason_code = PublishResponse::ReasonCode::kOk }, sub_track_handler);
}

void
TestServer::PublishDoneReceived(std::uint64_t request_id)
{
    std::lock_guard lock(state_mutex_);
}

void
TestServer::SubscribeReceived(std::uint64_t request_id,
                              const FullTrackName& track_full_name,
                              const SubscribeAttributes& subscribe_attributes)
{
    std::lock_guard lock(state_mutex_);

    const SubscribeDetails details = { request_id, track_full_name, subscribe_attributes };
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
    auto pub_track_handler =
      std::make_shared<TestPublishTrackHandler>(track_full_name,
                                                TrackMode::kStream,
                                                subscribe_attributes.priority,
                                                ttl,
                                                std::static_pointer_cast<TestServer>(shared_from_this()));

    if (!subscribe_attributes.is_publisher_initiated) {
        ResolveSubscribe(request_id,
                         track_alias,
                         { .reason_code = RequestResponse::ReasonCode::kOk,
                           .is_publisher_initiated = subscribe_attributes.is_publisher_initiated });
    }

    // Store the publish handler for this subscriber
    subscribes_[track_alias] = pub_track_handler;

    // Bind the publish track handler to send data to the subscriber
    BindPublisherTrack(GetConnection()->GetID(), request_id, pub_track_handler, false);

    // Link any existing publisher subscribe handlers to forward to this subscriber
    auto pub_sub_it = pub_subscribes_.find(track_alias);
    if (pub_sub_it != pub_subscribes_.end()) {
        auto& [pub_conn, sub_handler] = *pub_sub_it;
        if (sub_handler) {
            sub_handler->SetPublishHandler(pub_track_handler);
            sub_handler->Resume();
        }
    }
}

void
TestServer::SubscribeTracksReceived(const std::uint64_t data_ctx_id,
                                    const TrackNamespace& prefix_namespace,
                                    const SubscribeNamespaceAttributes& attributes)
{
    if (subscribe_namespace_promise_.has_value()) {
        subscribe_namespace_promise_->set_value({ data_ctx_id, prefix_namespace, attributes });
    }

    // Deliberately not prefix matching to allow testing bad case. Tests should only add tracks
    // with this in mind.
    const SubscribeNamespaceResponse response = { .reason_code = SubscribeNamespaceResponse::ReasonCode::kOk,
                                                  .namespaces = known_published_namespaces_ };

    // Blindly accept it.
    ResolveSubscribeTracks(data_ctx_id, attributes.request_id, prefix_namespace, response);

    auto ns_handler = PublishNamespaceHandler::Create(prefix_namespace);
    PublishNamespace(ns_handler, true);

    for (const auto track : known_published_tracks_) {
        auto handler =
          std::make_shared<TestPublishTrackHandler>(track.full_track_name,
                                                    quicr::TrackMode::kStream,
                                                    track.attributes.default_publisher_priority,
                                                    track.attributes.delivery_timeout.value_or(0),
                                                    std::static_pointer_cast<TestServer>(shared_from_this()));
        ns_handler->PublishTrack(handler);
    }

    namespace_subscribers_[prefix_namespace] = ns_handler;
}

void
TestServer::SubscribeNamespaceReceived(const std::uint64_t data_ctx_id,
                                       const TrackNamespace& prefix_namespace,
                                       const SubscribeNamespaceAttributes& attributes)
{
    // TODO: Implement.
}

void
TestServer::AddKnownPublishedNamespace(const TrackNamespace& track_namespace)
{
    known_published_namespaces_.push_back(track_namespace);
}

void
TestServer::AddKnownPublishedTrack(const FullTrackName& track,
                                   const std::optional<messages::Location>& largest_location,
                                   const PublishAttributes& attributes)
{
    known_published_tracks_.emplace_back(
      AvailableTrack{ track, largest_location.value_or(messages::Location{ 0, 0 }), attributes });
}

void
TestServer::PublishNamespaceReceived(const TrackNamespace& track_namespace,
                                     const PublishNamespaceAttributes& publish_announce_attributes)
{
    if (publish_namespace_promise_.has_value()) {
        publish_namespace_promise_->set_value({ track_namespace, publish_announce_attributes });
    }

    // Accept the publish namespace by responding with OK
    const PublishNamespaceResponse response = { .reason_code = PublishNamespaceResponse::ReasonCode::kOk };
    ResolvePublishNamespace(publish_announce_attributes.request_id, track_namespace, {}, response);
}

void
TestServer::StandaloneFetchReceived(const std::uint64_t request_id,
                                    const FullTrackName& track_full_name,
                                    const StandaloneFetchAttributes& attrs)
{
    if (fetch_response_data_.empty()) {
        // No response data configured
        ResolveFetch(request_id,
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
    ResolveFetch(request_id,
                 attrs.priority,
                 attrs.group_order,
                 { .reason_code = FetchResponse::ReasonCode::kOk, .largest_location = largest_location });

    // Publish the response
    auto pub_fetch_handler =
      PublishFetchHandler::Create(track_full_name,
                                  attrs.priority,
                                  request_id,
                                  attrs.group_order.value_or(attrs.publisher_default_group_order),
                                  500);
    BindFetchTrack(pub_fetch_handler);
    for (size_t i = 0; i < fetch_response_data_.size(); ++i) {
        pub_fetch_handler->PublishObject(fetch_response_data_[i].headers, fetch_response_data_[i].payload);
    }
}

void
TestServer::JoiningFetchReceived(const uint64_t request_id,
                                 const FullTrackName& track_full_name,
                                 const JoiningFetchAttributes& attrs)
{
    if (joining_fetch_promise_.has_value()) {
        joining_fetch_promise_->set_value({ GetConnection()->GetID(), request_id, track_full_name, attrs });
        joining_fetch_promise_.reset();
    }

    ResolveFetch(request_id,
                 attrs.priority,
                 attrs.group_order,
                 { .reason_code = FetchResponse::ReasonCode::kInternalError,
                   .error_reason = "No joining fetch test response configured" });
}

void
TestServer::JoiningFetchReceived(const uint64_t request_id,
                                 const FullTrackName& track_full_name,
                                 const JoiningFetchAttributes& attrs)
{
    if (joining_fetch_promise_.has_value()) {
        joining_fetch_promise_->set_value({ GetConnection()->GetID(), request_id, track_full_name, attrs });
        joining_fetch_promise_.reset();
    }

    if (!fetch_response_data_.empty()) {
        messages::Location largest_location{};
        for (const auto& response : fetch_response_data_) {
            largest_location =
              std::max(largest_location, messages::Location{ response.headers.group_id, response.headers.object_id });
        }

        ResolveFetch(request_id,
                     attrs.priority,
                     attrs.group_order,
                     { .reason_code = FetchResponse::ReasonCode::kOk, .largest_location = largest_location });

        auto pub_fetch_handler =
          PublishFetchHandler::Create(track_full_name,
                                      attrs.priority,
                                      request_id,
                                      attrs.group_order.value_or(attrs.publisher_default_group_order),
                                      500);
        BindFetchTrack(pub_fetch_handler);
        for (const auto& response : fetch_response_data_) {
            pub_fetch_handler->PublishObject(response.headers, response.payload);
        }
        return;
    }

    if (!fetch_response_data_.empty()) {
        messages::Location largest_location{};
        for (const auto& response : fetch_response_data_) {
            largest_location =
              std::max(largest_location, messages::Location{ response.headers.group_id, response.headers.object_id });
        }

        ResolveFetch(request_id,
                     attrs.priority,
                     attrs.group_order,
                     { .reason_code = FetchResponse::ReasonCode::kOk, .largest_location = largest_location });

        auto pub_fetch_handler =
          PublishFetchHandler::Create(track_full_name,
                                      attrs.priority,
                                      request_id,
                                      attrs.group_order.value_or(attrs.publisher_default_group_order),
                                      500);
        BindFetchTrack(pub_fetch_handler);
        for (const auto& response : fetch_response_data_) {
            pub_fetch_handler->PublishObject(response.headers, response.payload);
        }
        return;
    }

    ResolveFetch(request_id,
                 attrs.priority,
                 attrs.group_order,
                 { .reason_code = FetchResponse::ReasonCode::kInternalError,
                   .error_reason = "No joining fetch test response configured" });
}

void
TestServer::UnsubscribeReceived(const uint64_t request_id)
{
    std::lock_guard lock(state_mutex_);
    if (unsubscribe_received_promise_.has_value()) {
        const auto handler_type =
          expected_unsubscribe_handler_type_.value_or(UnsubscribeReceivedDetails::HandlerType::kSubscribeTrack);
        unsubscribe_received_promise_->set_value({ .request_id = request_id, .handler_type = handler_type });
        unsubscribe_received_promise_.reset();
        expected_unsubscribe_handler_type_.reset();
    }
}

void
TestServer::NewGroupRequested(const quicr::FullTrackName& track_full_name, std::uint64_t group_id)
{
    std::lock_guard lock(state_mutex_);
    const auto th = quicr::TrackHash(track_full_name);

    auto it = pub_subscribes_.find(th.track_fullname_hash);
    if (it == pub_subscribes_.end()) {
        return;
    }

    auto& [conn_id, sub_handler] = *it;
    if (sub_handler) {
        sub_handler->RequestNewGroup(group_id);
    }
}
