#include "test_server.h"

#include "quicr/handlers/publish_fetch_handler.h"
#include "quicr/handlers/publish_namespace_handler.h"
#include "quicr/handlers/track_handler.h"
#include "quicr/session.h"

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

TestServer::TestServer(std::shared_ptr<SharedState> shared_state)
  : shared_state_(shared_state ? std::move(shared_state) : std::make_shared<SharedState>())
{
}

quicr::Expected<const quicr::PublishResponse, quicr::Error<quicr::PublishErrorCode>>
TestServer::PublishReceived(const std::shared_ptr<quicr::Session>& session,
                            std::uint64_t request_id,
                            const PublishAttributes& publish_attributes,
                            [[maybe_unused]] std::weak_ptr<quicr::SubscribeNamespaceHandler> ns_handler)
{
    std::lock_guard lock(shared_state_->mutex);

    const auto th = TrackHash(publish_attributes.track_full_name);
    const auto track_alias = th.track_fullname_hash;

    // Is anyone interested in this prefix?
    for (const auto& [_, ns_handler] : shared_state_->namespace_subscribers) {
        if (ns_handler->GetFullTrackName().name_space.HasSamePrefix(publish_attributes.track_full_name.name_space)) {
            const auto delivery_timeout = publish_attributes.delivery_timeout.value_or(0);
            auto handler = std::make_shared<TestPublishTrackHandler>(publish_attributes.track_full_name,
                                                                     quicr::TrackMode::kStream,
                                                                     publish_attributes.default_publisher_priority,
                                                                     delivery_timeout,
                                                                     shared_from_this());
            ns_handler->PublishTrack(handler);
        }
    }

    // Create a subscribe handler to receive objects from the publisher
    auto sub_track_handler = std::make_shared<TestSubscribeTrackHandler>(publish_attributes.track_full_name, true);

    // If there are subscribers for this track (possibly on a different connection/session),
    // link the subscribe handler to forward to them.
    auto sub_it = shared_state_->subscribes.find(track_alias);
    if (sub_it != shared_state_->subscribes.end()) {
        // Link to first subscriber's publish handler for forwarding
        auto& pub_handler = sub_it->second;
        if (pub_handler) {
            sub_track_handler->SetPublishHandler(pub_handler);
            sub_track_handler->Resume();
        }
    }

    shared_state_->pub_subscribes[track_alias] = sub_track_handler;

    return quicr::PublishResponse{ {}, sub_track_handler };
}

quicr::Expected<void, quicr::Error<int>>
TestServer::PublishDoneReceived(const std::shared_ptr<quicr::Session>& session,
                                [[maybe_unused]] std::uint64_t request_id)
{
    std::lock_guard lock(state_mutex_);
    return {};
}

quicr::Expected<quicr::RequestResponse, quicr::Error<quicr::RequestErrorCode>>
TestServer::SubscribeReceived(const std::shared_ptr<quicr::Session>& session,
                              std::uint64_t request_id,
                              const FullTrackName& track_full_name,
                              const SubscribeAttributes& subscribe_attributes)
{
    {
        std::lock_guard lock(state_mutex_);
        const SubscribeDetails details = { request_id, track_full_name, subscribe_attributes };
        if (subscribe_promise_.has_value()) {
            subscribe_promise_->set_value(details);
        }
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

    std::lock_guard shared_lock(shared_state_->mutex);

    // Store the publish handler for this subscriber (visible to other connections/sessions
    // via the shared relay state).
    shared_state_->subscribes[track_alias] = pub_track_handler;

    // Bind the publish track handler to send data to the subscriber
    session->BindPublisherTrack(session->GetConnection()->GetID(), request_id, pub_track_handler, false);

    // Link any existing publisher subscribe handlers (possibly from a different
    // connection/session) to forward to this subscriber.
    auto pub_sub_it = shared_state_->pub_subscribes.find(track_alias);
    if (pub_sub_it != shared_state_->pub_subscribes.end()) {
        auto& [pub_conn, sub_handler] = *pub_sub_it;
        if (sub_handler) {
            sub_handler->SetPublishHandler(pub_track_handler);
            sub_handler->Resume();
        }
    }

    return RequestResponse{ subscribe_attributes.is_publisher_initiated };
}

quicr::Expected<std::vector<quicr::TrackNamespace>, quicr::Error<quicr::RequestErrorCode>>
TestServer::SubscribeTracksReceived(const std::shared_ptr<quicr::Session>& session,
                                    const std::uint64_t data_ctx_id,
                                    const TrackNamespace& prefix_namespace,
                                    const SubscribeNamespaceAttributes& attributes)
{
    if (subscribe_namespace_promise_.has_value()) {
        subscribe_namespace_promise_->set_value({ data_ctx_id, prefix_namespace, attributes });
    }

    std::lock_guard shared_lock(shared_state_->mutex);

    auto ns_handler = PublishNamespaceHandler::Create(prefix_namespace);
    session->PublishNamespace(ns_handler, true);

    for (const auto& track : shared_state_->known_published_tracks) {
        auto handler =
          std::make_shared<TestPublishTrackHandler>(track.full_track_name,
                                                    quicr::TrackMode::kStream,
                                                    track.attributes.default_publisher_priority,
                                                    track.attributes.delivery_timeout.value_or(0),
                                                    std::static_pointer_cast<TestServer>(shared_from_this()));
        ns_handler->PublishTrack(handler);
    }

    // Registered under the shared relay state so publishes arriving on a different
    // connection/session can be matched against this namespace subscription.
    shared_state_->namespace_subscribers[prefix_namespace] = ns_handler;

    // Deliberately not prefix matching to allow testing bad case. Tests should only add tracks
    // with this in mind. Blindly accept it.
    return shared_state_->known_published_namespaces;
}

quicr::Expected<std::vector<quicr::TrackNamespace>, quicr::Error<quicr::RequestErrorCode>>
TestServer::SubscribeNamespaceReceived([[maybe_unused]] const std::shared_ptr<quicr::Session>& session,
                                       [[maybe_unused]] const std::uint64_t data_ctx_id,
                                       [[maybe_unused]] const TrackNamespace& prefix_namespace,
                                       [[maybe_unused]] const SubscribeNamespaceAttributes& attributes)
{
    // TODO: Implement.
    return std::vector<quicr::TrackNamespace>{};
}

void
TestServer::AddKnownPublishedNamespace(const TrackNamespace& track_namespace)
{
    std::lock_guard shared_lock(shared_state_->mutex);
    shared_state_->known_published_namespaces.push_back(track_namespace);
}

void
TestServer::AddKnownPublishedTrack(const FullTrackName& track,
                                   const std::optional<messages::Location>& largest_location,
                                   const PublishAttributes& attributes)
{
    std::lock_guard shared_lock(shared_state_->mutex);
    shared_state_->known_published_tracks.emplace_back(
      AvailableTrack{ track, largest_location.value_or(messages::Location{ 0, 0 }), attributes });
}

quicr::Expected<void, quicr::Error<quicr::PublishNamespaceErrorCode>>
TestServer::PublishNamespaceReceived(const std::shared_ptr<quicr::Session>& session,
                                     const TrackNamespace& track_namespace,
                                     const PublishNamespaceAttributes& publish_announce_attributes)
{
    if (publish_namespace_promise_.has_value()) {
        publish_namespace_promise_->set_value({ track_namespace, publish_announce_attributes });
    }

    return {};
}

quicr::Expected<const quicr::FetchResponse, quicr::Error<quicr::FetchErrorCode>>
TestServer::StandaloneFetchReceived(const std::shared_ptr<quicr::Session>& session,
                                    const std::uint64_t request_id,
                                    const FullTrackName& track_full_name,
                                    const StandaloneFetchAttributes& attrs)
{
    if (fetch_response_data_.empty()) {
        // No response data configured
        return quicr::Unexpected<quicr::Error<quicr::FetchErrorCode>>(FetchErrorCode::kInternalError,
                                                                      "No fetch test response configured");
    }

    // Create location for the response
    const messages::Location largest_location = { .group = fetch_response_data_.back().headers.group_id,
                                                  .object = fetch_response_data_.back().headers.object_id };

    // Publish the response
    auto pub_fetch_handler =
      PublishFetchHandler::Create(track_full_name,
                                  attrs.priority,
                                  request_id,
                                  attrs.group_order.value_or(attrs.publisher_default_group_order),
                                  500);

    session->BindFetchTrack(pub_fetch_handler);
    for (size_t i = 0; i < fetch_response_data_.size(); ++i) {
        pub_fetch_handler->PublishObject(fetch_response_data_[i].headers, fetch_response_data_[i].payload);
    }

    return FetchResponse{ largest_location };
}

quicr::Expected<const quicr::FetchResponse, quicr::Error<quicr::FetchErrorCode>>
TestServer::JoiningFetchReceived(const std::shared_ptr<quicr::Session>& session,
                                 const uint64_t request_id,
                                 const FullTrackName& track_full_name,
                                 const JoiningFetchAttributes& attrs)
{
    if (joining_fetch_promise_.has_value()) {
        joining_fetch_promise_->set_value({ session->GetConnection()->GetID(), request_id, track_full_name, attrs });
        joining_fetch_promise_.reset();
    }

    if (!fetch_response_data_.empty()) {
        messages::Location largest_location{};
        for (const auto& response : fetch_response_data_) {
            largest_location =
              std::max(largest_location, messages::Location{ response.headers.group_id, response.headers.object_id });
        }

        auto pub_fetch_handler =
          PublishFetchHandler::Create(track_full_name,
                                      attrs.priority,
                                      request_id,
                                      attrs.group_order.value_or(attrs.publisher_default_group_order),
                                      500);
        session->BindFetchTrack(pub_fetch_handler);
        for (const auto& response : fetch_response_data_) {
            pub_fetch_handler->PublishObject(response.headers, response.payload);
        }

        return FetchResponse{ .largest_location = largest_location };
    }

    return quicr::Unexpected<quicr::Error<quicr::FetchErrorCode>>(FetchErrorCode::kInternalError,
                                                                  "No joining fetch test response configured");
}

quicr::Expected<void, quicr::Error<int>>
TestServer::UnsubscribeReceived(const std::shared_ptr<quicr::Session>& session, const uint64_t request_id)
{
    std::lock_guard lock(state_mutex_);
    if (unsubscribe_received_promise_.has_value()) {
        const auto handler_type =
          expected_unsubscribe_handler_type_.value_or(UnsubscribeReceivedDetails::HandlerType::kSubscribeTrack);
        unsubscribe_received_promise_->set_value({ .request_id = request_id, .handler_type = handler_type });
        unsubscribe_received_promise_.reset();
        expected_unsubscribe_handler_type_.reset();
    }

    return {};
}

quicr::Expected<void, quicr::Error<int>>
TestServer::NewGroupRequested(const quicr::FullTrackName& track_full_name, std::uint64_t group_id)
{
    std::lock_guard shared_lock(shared_state_->mutex);
    const auto th = quicr::TrackHash(track_full_name);

    // The publisher's SubscribeTrackHandler may live on a different connection/session
    // than the subscriber that requested the new group, so this must go through the
    // shared relay state.
    auto it = shared_state_->pub_subscribes.find(th.track_fullname_hash);
    if (it == shared_state_->pub_subscribes.end()) {
        return {};
    }

    auto& [track_alias, sub_handler] = *it;
    if (sub_handler) {
        sub_handler->RequestNewGroup(group_id);
    }

    return {};
}
